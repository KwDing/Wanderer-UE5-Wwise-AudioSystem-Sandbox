#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"

#include "Engine/World.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkGroupValue.h"
#include "AkRtpc.h"
#include "AkStateValue.h"
#include "AkSwitchValue.h"
#include "AkTrigger.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

#include "Components/CharacterAudio/CharacterAudioActionUtils.h"
#include "Components/CharacterAudio/CharacterAudioDiagnosticUtils.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Systems/Audio/WandererAudioDiagnostics.h"

namespace
{
    using namespace WandererCharacterActions;
    struct FPreparedAudioAction
    {
        const FWandererAudioAction* Definition = nullptr;
        UAkComponent* Target = nullptr; // Synchronous game-thread observation only.
        uint32 ObjectID = 0;
        uint32 GroupID = 0;
        float Value = 0.0f;
        AkGameObjectID GameObjectID = AK_INVALID_GAME_OBJECT;
        AkPlayingID ExistingPlayingID = AK_INVALID_PLAYING_ID;
    };

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

    const UAkGroupValue* ResolveSwitch(const FWandererAudioAction& AudioAction,
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

    const UAkGroupValue* ResolveGroup(const FWandererAudioAction& AudioAction,
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

    bool PrepareParameter(
        FPreparedAudioAction& Prepared, const FWandererCharacterAudioContext& Context)
    {
        const FWandererAudioAction& AudioAction = *Prepared.Definition;
        using EType = EWandererAudioActionType;
        if (!IsValid(AudioAction.Parameter))
        {
            return false;
        }
        Prepared.ObjectID = AudioAction.Parameter->GetShortID();
        if (AudioAction.Scope == EWandererAudioParameterScope::ExistingPlayback)
        {
            if (Context.ExistingPlayingID <= 0 || Context.ExistingPlayingID > MAX_uint32)
            {
                return false;
            }
            Prepared.ExistingPlayingID = static_cast<AkPlayingID>(Context.ExistingPlayingID);
            auto* SoundEngine = IWwiseSoundEngineAPI::Get();
            if (!SoundEngine || !SoundEngine->Query
                || SoundEngine->Query->GetGameObjectFromPlayingID(Prepared.ExistingPlayingID)
                       != Prepared.GameObjectID)
            {
                return false;
            }
        }
        return Prepared.ObjectID
               && (AudioAction.AudioActionType != EType::SetParameter
                   || ResolveNumber(AudioAction, Context, Prepared.Value));
    }

    bool PrepareAudioAction(FPreparedAudioAction& Prepared,
        const FWandererCharacterAudioContext& Context,
        UAkComponent* OwnerEmitter,
        const FSurfaceValues& Surfaces)
    {
        const FWandererAudioAction& AudioAction = *Prepared.Definition;
        Prepared.Target = AudioAction.Target == EWandererAudioTarget::Owner
                              ? OwnerEmitter
                              : Context.TargetEmitter.Get();
        if (!IsGlobalAudioAction(AudioAction))
        {
            if (!IsValid(Prepared.Target) || !Prepared.Target->IsRegistered()
                || !Prepared.Target->IsRegisteredWithWwise() || !IsValid(OwnerEmitter)
                || Prepared.Target->GetWorld() != OwnerEmitter->GetWorld())
            {
                return false;
            }
            Prepared.GameObjectID = Prepared.Target->GetAkGameObjectID();
            if (!Prepared.GameObjectID || Prepared.GameObjectID == AK_INVALID_GAME_OBJECT)
            {
                return false;
            }
        }
        using EType = EWandererAudioActionType;
        if (IsParameterAudioAction(AudioAction))
        {
            return PrepareParameter(Prepared, Context);
        }
        if (AudioAction.AudioActionType == EType::SetSwitch
            || AudioAction.AudioActionType == EType::SetState)
        {
            const UAkGroupValue* Value = ResolveGroup(AudioAction, Context, Surfaces);
            if (!IsValid(Value))
            {
                return false;
            }
            Prepared.ObjectID = Value->GetShortID();
            Prepared.GroupID = Value->GetGroupID();
            return Prepared.ObjectID && Prepared.GroupID;
        }
        if (AudioAction.AudioActionType == EType::PostTrigger)
        {
            Prepared.ObjectID =
                IsValid(AudioAction.Trigger) ? AudioAction.Trigger->GetShortID() : 0;
            return Prepared.ObjectID != 0;
        }
        return AudioAction.AudioActionType == EType::PostEvent && IsValid(AudioAction.Event);
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
            Item.Definition = &AudioAction;
            if (!PrepareAudioAction(Item, Context, InEmitter, InSurfaces))
            {
                OutError = FString::Printf(
                    TEXT("%s: missing context/mapping, invalid target, or expired PlayingID."),
                    *AudioAction.AudioActionID.ToString());
                return false;
            }
        }
        return true;
    }

    AKRESULT SendAudioAction(IWwiseSoundEngineAPI& SoundEngine,
        const FPreparedAudioAction& Prepared,
        AkPlayingID& OutPlayingID)
    {
        const FWandererAudioAction& AudioAction = *Prepared.Definition;
        using EType = EWandererAudioActionType;
        using EScope = EWandererAudioParameterScope;
        switch (AudioAction.AudioActionType)
        {
        case EType::PostEvent:
            OutPlayingID = Prepared.Target->PostAkEvent(AudioAction.Event.Get());
            return OutPlayingID != AK_INVALID_PLAYING_ID ? AK_Success : AK_Fail;
        case EType::SetParameter:
            if (AudioAction.Scope == EScope::ExistingPlayback)
            {
                return SoundEngine.SetRTPCValueByPlayingID(
                    Prepared.ObjectID, Prepared.Value, Prepared.ExistingPlayingID);
            }
            return SoundEngine.SetRTPCValue(Prepared.ObjectID,
                Prepared.Value,
                Prepared.GameObjectID,
                0,
                AkCurveInterpolation_Linear,
                AudioAction.Scope == EScope::ThisExecution);
        case EType::SetSwitch:
            return SoundEngine.SetSwitch(
                Prepared.GroupID, Prepared.ObjectID, Prepared.GameObjectID);
        case EType::SetState:
            return SoundEngine.SetState(Prepared.GroupID, Prepared.ObjectID);
        case EType::PostTrigger:
            return SoundEngine.PostTrigger(Prepared.ObjectID, Prepared.GameObjectID);
        case EType::ResetParameter:
            if (AudioAction.Scope == EScope::ExistingPlayback)
            {
                return SoundEngine.ResetRTPCValueByPlayingID(
                    Prepared.ObjectID, Prepared.ExistingPlayingID);
            }
            return SoundEngine.ResetRTPCValue(Prepared.ObjectID, Prepared.GameObjectID);
        default:
            return AK_Fail;
        }
    }

    bool PinSnapshot(IWwiseSoundEngineAPI& SoundEngine,
        const FPreparedAudioAction& Prepared,
        AkPlayingID PlayingID)
    {
        const FWandererAudioAction& AudioAction = *Prepared.Definition;
        if (AudioAction.AudioActionType != EWandererAudioActionType::SetParameter
            || AudioAction.Scope != EWandererAudioParameterScope::ThisExecution)
        {
            return true;
        }
        return SoundEngine.SetRTPCValueByPlayingID(Prepared.ObjectID,
                   Prepared.Value,
                   PlayingID,
                   0,
                   AkCurveInterpolation_Linear,
                   true)
               == AK_Success;
    }

    void ReportSdkResult(const FPreparedAudioAction& Item, AKRESULT Code, AkPlayingID PlayingID)
    {
        WANDERER_DIAGNOSTIC(Item.Target,
            EDiagnosticDomain::Audio,
            TEXT("wwise-action-result"),
            Code != AK_Success,
            FString::Printf(
                TEXT("action=%s type=%d event=%s objectID=%u groupID=%u "
                     "value=%.3f gameObjectID=%llu result=%d playingID=%u positionCm=%s"),
                *Item.Definition->AudioActionID.ToString(),
                static_cast<int32>(Item.Definition->AudioActionType),
                *GetPathNameSafe(Item.Definition->Event.Get()),
                Item.ObjectID,
                Item.GroupID,
                Item.Value,
                Item.GameObjectID,
                static_cast<int32>(Code),
                PlayingID,
                Item.Target ? *Item.Target->GetComponentLocation().ToCompactString()
                            : TEXT("global")));
    }

    FWandererAudioExecutionResult SendPreparedAudioActions(
        IWwiseSoundEngineAPI& SoundEngine, const FPreparedAudioActions& Prepared)
    {
        FWandererAudioExecutionResult Result;
        for (const FPreparedAudioAction& Item : Prepared)
        {
            AkPlayingID PlayingID = AK_INVALID_PLAYING_ID;
            const AKRESULT Code = SendAudioAction(SoundEngine, Item, PlayingID);
            ReportSdkResult(Item, Code, PlayingID);
            if (Code != AK_Success)
            {
                Result.Status = Result.AcceptedAudioActions
                                    ? EWandererAudioExecutionStatus::PartialFailure
                                    : EWandererAudioExecutionStatus::Failed;
                Result.Message = FString::Printf(
                    TEXT("%s: Wwise result=%d; previous accepted audio actions are not "
                         "rolled back."),
                    *Item.Definition->AudioActionID.ToString(),
                    static_cast<int32>(Code));
                return Result;
            }
            ++Result.AcceptedAudioActions;
            if (PlayingID == AK_INVALID_PLAYING_ID)
            {
                continue;
            }
            Result.PlayingID = PlayingID;
            for (const FPreparedAudioAction& Snapshot : Prepared)
            {
                if (PinSnapshot(SoundEngine, Snapshot, PlayingID))
                {
                    continue;
                }
                if (FAkAudioDevice* Device = FAkAudioDevice::Get())
                {
                    Device->StopPlayingID(PlayingID);
                }
                Result.Status = EWandererAudioExecutionStatus::PartialFailure;
                Result.PlayingID = 0;
                Result.Message = TEXT("Snapshot failed; stopped only the new Event. Earlier "
                                      "parameter/state audio actions remain applied.");
                return Result;
            }
        }
        Result.Status = EWandererAudioExecutionStatus::Accepted;
        return Result;
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
    if (!bReady || !IsActive() || !IsValid(World) || World->bIsTearingDown
        || !FAkAudioDevice::IsInitialized())
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
    IWwiseSoundEngineAPI* SoundEngine = IWwiseSoundEngineAPI::Get();
    if (!AudioActions || !SoundEngine)
    {
        Result.Message = TEXT("AudioAction configuration or sound engine missing.");
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
    bOutAttempted = true;
    return SendPreparedAudioActions(*SoundEngine, Prepared);
}
