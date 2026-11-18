#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"

#include "Engine/World.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkGroupValue.h"
#include "AkRtpc.h"
#include "AkStateValue.h"
#include "AkSwitchValue.h"
#include "AkTrigger.h"

#include "Components/CharacterAudio/CharacterAudioActionUtils.h"
#include "Components/CharacterAudio/CharacterAudioDiagnosticUtils.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Interfaces/Audio/AudioPlaybackService.h"

namespace
{
    using namespace WandererCharacterActions;
    using FPreparedAudioActions = TArray<FPreparedAudioAction, TInlineAllocator<8>>;

    bool IsGlobalAudioAction(const FWandererAudioAction& AudioAction)
    {
        return AudioAction.AudioActionType == EWandererAudioActionType::SetState
               || (IsParameterAudioAction(AudioAction)
                   && AudioAction.Scope == EWandererAudioParameterScope::Global);
    }

    FName ReadSemantic(const FWandererCharacterAudioContext& Context, FName Key)
    {
        if (Key == TEXT("Surface"))
        {
            return Context.Surface;
        }
        if (Key == TEXT("Gait"))
        {
            return Context.Gait;
        }
        if (Key == TEXT("Wetness"))
        {
            return Context.Wetness;
        }
        const FName* Value = Context.SemanticValues.Find(Key);
        return Value ? *Value : NAME_None;
    }

    template <typename ValueType>
    const ValueType* FindMapped(const TMap<FName, ValueType>& Values,
        const FWandererAudioAction& AudioAction,
        const FWandererCharacterAudioContext& Context)
    {
        const ValueType* Value = Values.Find(ReadSemantic(Context, AudioAction.ContextKey));
        return !Value && AudioAction.bUseFallback ? Values.Find(AudioAction.FallbackKey) : Value;
    }

    bool ResolveNumber(const FWandererAudioAction& AudioAction,
        const FWandererCharacterAudioContext& Context,
        float& OutValue)
    {
        switch (AudioAction.ValueSource)
        {
        case EWandererAudioValueSource::Constant:
            OutValue = AudioAction.ConstantValue;
            return FMath::IsFinite(OutValue);
        case EWandererAudioValueSource::NumericContext:
            if (const float* Value = Context.NumericValues.Find(AudioAction.ContextKey))
            {
                OutValue = *Value;
            }
            else if (AudioAction.bUseFallback)
            {
                OutValue = AudioAction.FallbackNumber;
            }
            else
            {
                return false;
            }
            return FMath::IsFinite(OutValue);
        case EWandererAudioValueSource::SemanticMapping:
            if (!IsValid(AudioAction.NumberMapping))
            {
                return false;
            }
            if (const float* Value =
                    FindMapped(AudioAction.NumberMapping->Values, AudioAction, Context))
            {
                OutValue = *Value;
                return FMath::IsFinite(OutValue);
            }
            return false;
        default:
            return false;
        }
    }

    UAkGroupValue* ResolveSwitch(const FWandererAudioAction& AudioAction,
        const FWandererCharacterAudioContext& Context,
        const FSurfaceValues& Surfaces)
    {

        if (AudioAction.ValueSource == EWandererAudioValueSource::Constant)
        {
            return AudioAction.SwitchValue.Get();
        }
        if (AudioAction.ValueSource == EWandererAudioValueSource::ProfileSurface)
        {
            const auto* Value = Surfaces.Find(Context.PhysicalSurface);
            if (!Value && AudioAction.bUseFallback)
            {
                Value = Surfaces.Find(SurfaceType_Default);
            }
            return Value ? Value->Get() : nullptr;
        }
        if (!IsValid(AudioAction.SwitchMapping))
        {
            return nullptr;
        }
        const auto* Value = FindMapped(AudioAction.SwitchMapping->Values, AudioAction, Context);
        return Value ? Value->Get() : nullptr;
    }

    UAkGroupValue* ResolveGroup(const FWandererAudioAction& AudioAction,
        const FWandererCharacterAudioContext& Context,
        const FSurfaceValues& Surfaces)
    {
        if (AudioAction.AudioActionType == EWandererAudioActionType::SetSwitch)
        {
            return ResolveSwitch(AudioAction, Context, Surfaces);
        }
        if (AudioAction.ValueSource == EWandererAudioValueSource::Constant)
        {
            return AudioAction.StateValue.Get();
        }
        if (!IsValid(AudioAction.StateMapping))
        {
            return nullptr;
        }
        const auto* Value = FindMapped(AudioAction.StateMapping->Values, AudioAction, Context);
        return Value ? Value->Get() : nullptr;
    }

    bool PrepareAudioAction(const FWandererAudioAction& InAction,
        const FWandererCharacterAudioContext& InContext,
        UAkComponent* InOwnerEmitter,
        const FSurfaceValues& InSurfaces,
        FPreparedAudioAction& OutAction)
    {
        if (!IsGlobalAudioAction(InAction) && !IsValid(InOwnerEmitter))
        {
            return false;
        }
        OutAction.ActionID = InAction.AudioActionID;
        OutAction.Type = InAction.AudioActionType;
        OutAction.Scope = InAction.Scope;
        OutAction.TargetWeakPtr = InAction.Target == EWandererAudioTarget::Owner
            ? InOwnerEmitter : InContext.TargetEmitter.Get();
        using EType = EWandererAudioActionType;
        if (IsParameterAudioAction(InAction))
        {
            OutAction.AssetWeakPtr = InAction.Parameter.Get();
            if (InAction.Scope == EWandererAudioParameterScope::ExistingPlayback)
            {
                if (InContext.ExistingPlayingID <= 0 || InContext.ExistingPlayingID > MAX_uint32)
                {
                    return false;
                }
                OutAction.ExistingPlayingID = static_cast<uint32>(InContext.ExistingPlayingID);
            }
            return InAction.AudioActionType != EType::SetParameter
                || ResolveNumber(InAction, InContext, OutAction.Value);
        }
        switch (InAction.AudioActionType)
        {
        case EType::SetSwitch:
        case EType::SetState:
            OutAction.AssetWeakPtr = ResolveGroup(InAction, InContext, InSurfaces);
            break;
        case EType::PostTrigger:
            OutAction.AssetWeakPtr = InAction.Trigger.Get();
            break;
        case EType::PostEvent:
            OutAction.AssetWeakPtr = InAction.Event.Get();
            break;
        default:
            return false;
        }
        return OutAction.AssetWeakPtr.IsValid();
    }

    bool PrepareAudioActions(const TArray<FWandererAudioAction>& InActions,
        const FWandererCharacterAudioContext& Context,
        UAkComponent* InEmitter,
        const FSurfaceValues& InSurfaces,
        FPreparedAudioActions& Prepared,
        FString& OutError)
    {
        for (const FWandererAudioAction& AudioAction : InActions)
        {
            FPreparedAudioAction& Item = Prepared.AddDefaulted_GetRef();
            if (!PrepareAudioAction(AudioAction, Context, InEmitter, InSurfaces, Item))
            {
                OutError = FString::Printf(
                    TEXT("%s: missing context/mapping, invalid target, or expired PlayingID."),
                    *AudioAction.AudioActionID.ToString());
                return false;
            }
        }
        return true;
    }

} // namespace

FWandererAudioExecutionResult UWandererCharacterAudioComponent::ExecuteAction(FName ActionID,
    const FWandererCharacterAudioContext& Context,
    EWandererAudioPlaybackScope Scope)
{
    FWandererAudioExecutionResult Result;
    if (!IsInGameThread())
    {
        Result.Message = TEXT("ExecuteAction requires the game thread.");
        return Result;
    }
    if (bStopped)
    {
        Result.Message = TEXT("Character audio has ended play.");
        return Result;
    }
    FDiagnosticTraceScope Trace(ActionID == TEXT("Footstep"));
    CharacterAudioDiagnosticUtils::ReportRequest(this, ActionID, Context, Scope);
    UWorld* World = GetWorld();
    if (!bReady || !IsActive() || !IsValid(World) || !WandererAudioPlayback::IsReady(*World))
    {
        Result.Message = TEXT("Audio not ready/active, World missing, or Wwise unavailable.");
        ReportSkip(ActionID, TEXT("NotReady"), *Result.Message);
        return CharacterAudioDiagnosticUtils::ReportResult(this, ActionID, Result);
    }
    const FWandererCharacterAudioActionRow* Row = CachedActions.Find(ActionID);
    if (!Row)
    {
        Result.Message = TEXT("Action is absent from Profile.");
        WarnOnce(ActionID, *Result.Message);
        return CharacterAudioDiagnosticUtils::ReportResult(this, ActionID, Result);
    }
    FActionCooldowns* Cooldowns = NextAllowedTimes.Find(ActionID);
    if (!Cooldowns)
    {
        Result.Message = TEXT("Action cooldown cache is missing.");
        return CharacterAudioDiagnosticUtils::ReportResult(this, ActionID, Result);
    }
    double& NextTime = Cooldowns->Get(Scope);
    const double Now = World->GetTimeSeconds();
    if (!Row->bEnabled || Now < NextTime)
    {
        Result.Status = EWandererAudioExecutionStatus::Skipped;
        Result.Message = TEXT("Action disabled or cooldown active.");
        ReportSkip(ActionID, TEXT("DisabledOrCooldown"), *Result.Message);
        return CharacterAudioDiagnosticUtils::ReportResult(this, ActionID, Result);
    }
    bool bAttempted = false;
    Result = ExecuteAudioActions(ActionID, *Row, Context, bAttempted);
    if (!bAttempted)
    {
        return CharacterAudioDiagnosticUtils::ReportResult(this, ActionID, Result);
    }
    // Retain cooldown on partial failure; do not replay already accepted audio actions.
    if (Result.AcceptedAudioActions > 0)
    {
        NextTime = Now + Row->MinimumInterval;
    }
    if (Result.Status != EWandererAudioExecutionStatus::Accepted)
    {
        WarnOnce(ActionID, *Result.Message);
        return CharacterAudioDiagnosticUtils::ReportResult(this, ActionID, Result);
    }
    LastSkipReasons.Remove(ActionID);
    return CharacterAudioDiagnosticUtils::ReportResult(this, ActionID, Result);
}

FWandererAudioExecutionResult UWandererCharacterAudioComponent::ExecuteAudioActions(FName ActionID,
    const FWandererCharacterAudioActionRow& InRow,
    const FWandererCharacterAudioContext& Context,
    bool& bOutAttempted)
{
    bOutAttempted = false;
    FWandererAudioExecutionResult Result;
    const TArray<FWandererAudioAction>* AudioActions =
        WandererCharacterActions::GetAudioActions(InRow);
    const UWorld* World = GetWorld();
    if (!AudioActions || !IsValid(World))
    {
        Result.Message = TEXT("AudioAction configuration or World missing.");
        return Result;
    }
    // Preflight every audio action before the first SDK side effect.
    FPreparedAudioActions Prepared;
    Prepared.Reserve(AudioActions->Num());
    if (!PrepareAudioActions(*AudioActions,
            Context,
            EmitterWeakPtr.Get(),
            SurfaceSwitches,
            Prepared,
            Result.Message))
    {
        WarnOnce(ActionID, *Result.Message);
        return Result;
    }
    Result = WandererAudioPlayback::ApplyPreparedActions(*World, Prepared, bOutAttempted);
    if (!bOutAttempted)
    {
        WarnOnce(ActionID, *Result.Message);
    }
    return Result;
}
