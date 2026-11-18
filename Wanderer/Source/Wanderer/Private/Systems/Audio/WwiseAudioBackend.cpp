#include "Systems/Audio/WwiseAudioBackend.h"

#include "Engine/World.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkRtpc.h"
#include "AkStateValue.h"
#include "AkSwitchValue.h"
#include "AkTrigger.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Data/Audio/WandererAudioActionsAsset.h"
#include "Interfaces/Audio/AudioOutputService.h"
#include "Interfaces/Audio/AudioPlaybackService.h"

DEFINE_LOG_CATEGORY_STATIC(LogWandererAudioOutput, Log, All);

namespace
{
    using EActionType = EWandererAudioActionType;
    using EParameterScope = EWandererAudioParameterScope;

    struct FResolvedWwiseAction
    {
        const FPreparedAudioAction* Action = nullptr;
        AkGameObjectID GameObjectID = AK_INVALID_GAME_OBJECT;
        AkUniqueID ObjectID = 0;
        AkUniqueID GroupID = 0;
    };

    bool IsGlobalWwiseAction(const FPreparedAudioAction& InAction)
    {
        return InAction.Type == EActionType::SetState
            || ((InAction.Type == EActionType::SetParameter || InAction.Type == EActionType::ResetParameter)
                && InAction.Scope == EParameterScope::Global);
    }

    AkGameObjectID GetRegisteredWwiseTarget(const UWorld& InWorld, const UAkComponent* InTarget)
    {
        return InTarget && InTarget->GetWorld() == &InWorld && InTarget->IsRegistered()
                && InTarget->IsRegisteredWithWwise()
            ? InTarget->GetAkGameObjectID() : AK_INVALID_GAME_OBJECT;
    }

    bool ResolveWwiseAction(IWwiseSoundEngineAPI& InEngine, const UWorld& InWorld,
        const FPreparedAudioAction& InAction, FResolvedWwiseAction& OutAction)
    {
        UAkAudioType* Asset = InAction.AssetWeakPtr.Get();
        if (!Asset)
        {
            return false;
        }
        OutAction.Action = &InAction;
        if (!IsGlobalWwiseAction(InAction))
        {
            OutAction.GameObjectID = GetRegisteredWwiseTarget(InWorld, InAction.TargetWeakPtr.Get());
            if (!OutAction.GameObjectID || OutAction.GameObjectID == AK_INVALID_GAME_OBJECT)
            {
                return false;
            }
        }
        switch (InAction.Type)
        {
        case EActionType::PostEvent:
            return Asset->IsA<UAkAudioEvent>();
        case EActionType::SetParameter:
        case EActionType::ResetParameter:
            if (!Asset->IsA<UAkRtpc>() || !FMath::IsFinite(InAction.Value))
            {
                return false;
            }
            if (InAction.Scope == EParameterScope::ExistingPlayback
                && (!InAction.ExistingPlayingID || !InEngine.Query
                    || InEngine.Query->GetGameObjectFromPlayingID(InAction.ExistingPlayingID)
                        != OutAction.GameObjectID))
            {
                return false;
            }
            break;
        case EActionType::SetSwitch:
        case EActionType::SetState:
            if (const UAkGroupValue* Group = InAction.Type == EActionType::SetSwitch
                    ? static_cast<UAkGroupValue*>(Cast<UAkSwitchValue>(Asset))
                    : Cast<UAkStateValue>(Asset))
            {
                OutAction.GroupID = Group->GetGroupID();
            }
            if (!OutAction.GroupID)
            {
                return false;
            }
            break;
        case EActionType::PostTrigger:
            if (!Asset->IsA<UAkTrigger>())
            {
                return false;
            }
            break;
        default:
            return false;
        }
        OutAction.ObjectID = Asset->GetShortID();
        return OutAction.ObjectID != 0;
    }

    AKRESULT SendWwiseAction(IWwiseSoundEngineAPI& InEngine, const UWorld& InWorld,
        const FResolvedWwiseAction& InResolved, AkPlayingID& OutPlayingID)
    {
        const FPreparedAudioAction& Action = *InResolved.Action;
        UAkComponent* Target = Action.TargetWeakPtr.Get();
        if (InWorld.bIsTearingDown || (InResolved.GameObjectID != AK_INVALID_GAME_OBJECT
            && GetRegisteredWwiseTarget(InWorld, Target) != InResolved.GameObjectID))
        {
            return AK_Fail;
        }
        switch (Action.Type)
        {
        case EActionType::PostEvent:
            if (UAkAudioEvent* Event = Cast<UAkAudioEvent>(Action.AssetWeakPtr.Get()))
            {
                OutPlayingID = Target->PostAkEvent(Event);
            }
            return OutPlayingID != AK_INVALID_PLAYING_ID ? AK_Success : AK_Fail;
        case EActionType::SetParameter:
            return Action.Scope == EParameterScope::ExistingPlayback
                ? InEngine.SetRTPCValueByPlayingID(InResolved.ObjectID, Action.Value, Action.ExistingPlayingID)
                : InEngine.SetRTPCValue(InResolved.ObjectID, Action.Value, InResolved.GameObjectID,
                    0, AkCurveInterpolation_Linear, Action.Scope == EParameterScope::ThisExecution);
        case EActionType::ResetParameter:
            return Action.Scope == EParameterScope::ExistingPlayback
                ? InEngine.ResetRTPCValueByPlayingID(InResolved.ObjectID, Action.ExistingPlayingID)
                : InEngine.ResetRTPCValue(InResolved.ObjectID, InResolved.GameObjectID);
        case EActionType::SetSwitch:
            return InEngine.SetSwitch(InResolved.GroupID, InResolved.ObjectID, InResolved.GameObjectID);
        case EActionType::SetState:
            return InEngine.SetState(InResolved.GroupID, InResolved.ObjectID);
        case EActionType::PostTrigger:
            return InEngine.PostTrigger(InResolved.ObjectID, InResolved.GameObjectID);
        default:
            return AK_Fail;
        }
    }

    bool PinWwiseSnapshot(IWwiseSoundEngineAPI& InEngine, const FResolvedWwiseAction& InResolved,
        AkPlayingID InPlayingID)
    {
        const FPreparedAudioAction& Action = *InResolved.Action;
        return Action.Type != EActionType::SetParameter || Action.Scope != EParameterScope::ThisExecution
            || InEngine.SetRTPCValueByPlayingID(InResolved.ObjectID, Action.Value, InPlayingID,
                0, AkCurveInterpolation_Linear, true) == AK_Success;
    }

    void ReportWwiseActionResult(const FResolvedWwiseAction& InResolved, AKRESULT InCode, AkPlayingID InPlayingID)
    {
        const FPreparedAudioAction& Action = *InResolved.Action;
        const UAkComponent* Target = Action.TargetWeakPtr.Get();
        WANDERER_DIAGNOSTIC(Target,
            EDiagnosticDomain::Audio,
            TEXT("wwise-action-result"),
            InCode != AK_Success,
            FString::Printf(
                TEXT("action=%s type=%d event=%s objectID=%u groupID=%u "
                     "value=%.3f gameObjectID=%llu result=%d playingID=%u positionCm=%s"),
                *Action.ActionID.ToString(), static_cast<int32>(Action.Type),
                *GetPathNameSafe(Cast<UAkAudioEvent>(Action.AssetWeakPtr.Get())),
                InResolved.ObjectID, InResolved.GroupID, Action.Value, InResolved.GameObjectID,
                static_cast<int32>(InCode), InPlayingID,
                Target ? *Target->GetComponentLocation().ToCompactString() : TEXT("global")));
    }

    constexpr const char* HeadphoneParameter = "UsingHeadphone";
    constexpr float HeadphoneEnabled = 1.0f;
    constexpr float HeadphoneDisabled = 0.0f;
}

bool WandererAudioPlayback::IsReady(const UWorld& InWorld)
{
    return IsInGameThread() && IsValid(&InWorld) && !InWorld.bIsTearingDown
        && WandererAudioBackend::IsInitialized();
}

FWandererAudioExecutionResult WandererAudioPlayback::ApplyPreparedActions(const UWorld& InWorld,
    TConstArrayView<FPreparedAudioAction> InActions, bool& bOutAttempted)
{
    FWandererAudioExecutionResult Result;
    bOutAttempted = false;
    IWwiseSoundEngineAPI* Engine = IsReady(InWorld) ? IWwiseSoundEngineAPI::Get() : nullptr;
    if (!Engine)
    {
        Result.Message = TEXT("Sound engine unavailable.");
        return Result;
    }
    TArray<FResolvedWwiseAction, TInlineAllocator<8>> Resolved;
    Resolved.Reserve(InActions.Num());
    for (const FPreparedAudioAction& Action : InActions)
    {
        if (!ResolveWwiseAction(*Engine, InWorld, Action, Resolved.AddDefaulted_GetRef()))
        {
            Result.Message = FString::Printf(
                TEXT("%s: invalid audio asset, target, or expired PlayingID."), *Action.ActionID.ToString());
            return Result;
        }
    }

    bOutAttempted = true;
    for (const FResolvedWwiseAction& Item : Resolved)
    {
        AkPlayingID PlayingID = AK_INVALID_PLAYING_ID;
        const AKRESULT Code = SendWwiseAction(*Engine, InWorld, Item, PlayingID);
        ReportWwiseActionResult(Item, Code, PlayingID);
        if (Code != AK_Success)
        {
            Result.Status = Result.AcceptedAudioActions
                ? EWandererAudioExecutionStatus::PartialFailure : EWandererAudioExecutionStatus::Failed;
            Result.Message = FString::Printf(
                TEXT("%s: Wwise result=%d; previous accepted audio actions remain applied."),
                *Item.Action->ActionID.ToString(), static_cast<int32>(Code));
            return Result;
        }
        ++Result.AcceptedAudioActions;
        if (PlayingID == AK_INVALID_PLAYING_ID)
        {
            continue;
        }
        Result.PlayingID = PlayingID;
        for (const FResolvedWwiseAction& Snapshot : Resolved)
        {
            if (PinWwiseSnapshot(*Engine, Snapshot, PlayingID))
            {
                continue;
            }
            WandererAudioBackend::StopPlayback(PlayingID, 0);
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

bool WandererAudioOutput::IsReady()
{
    return IsInGameThread() && WandererAudioBackend::IsInitialized();
}

bool WandererAudioOutput::ReadHeadphoneOutput(bool& bOutEnabled)
{
    IWwiseSoundEngineAPI* Engine = IsReady() ? IWwiseSoundEngineAPI::Get() : nullptr;
    if (!Engine || !Engine->Query)
    {
        return false;
    }
    AkRtpcValue Value = HeadphoneDisabled;
    auto Scope = AK::SoundEngine::Query::RTPCValue_Global;
    if (Engine->Query->GetRTPCValue(
            HeadphoneParameter, AK_INVALID_GAME_OBJECT, AK_INVALID_PLAYING_ID, Value, Scope) != AK_Success)
    {
        return false;
    }
    bOutEnabled = Value >= HeadphoneEnabled;
    return true;
}

bool WandererAudioOutput::WriteHeadphoneOutput(bool bInEnabled)
{
    IWwiseSoundEngineAPI* Engine = IsReady() ? IWwiseSoundEngineAPI::Get() : nullptr;
    if (!Engine)
    {
        UE_LOG(LogWandererAudioOutput, Warning, TEXT("Cannot change UsingHeadphone before Wwise is ready."));
        return false;
    }
    const AKRESULT Result = Engine->SetRTPCValue(
        HeadphoneParameter, bInEnabled ? HeadphoneEnabled : HeadphoneDisabled);
    if (Result != AK_Success)
    {
        UE_LOG(LogWandererAudioOutput, Warning,
            TEXT("Setting UsingHeadphone failed (%d). Check the loaded Init bank."), Result);
        return false;
    }
    return true;
}
