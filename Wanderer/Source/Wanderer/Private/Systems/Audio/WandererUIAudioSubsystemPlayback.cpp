#include "Systems/Audio/WandererUIAudioSubsystem.h"

#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Data/Audio/WandererUIAudioProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogWandererUIAudio, Log, All);

namespace
{
    FName GetActionID(EWandererUIAudioAction Action)
    {
        switch (Action)
        {
        case EWandererUIAudioAction::Open:
            return TEXT("Open");
        case EWandererUIAudioAction::Choose:
            return TEXT("Choose");
        case EWandererUIAudioAction::Cancel:
            return TEXT("Cancel");
        case EWandererUIAudioAction::Close:
            return TEXT("Close");
        default:
            return NAME_None;
        }
    }

    const TArray<FWandererAudioAction>* GetAudioActions(const FWandererUIAudioActionRow& Row)
    {
        if (Row.AudioActionSource == EWandererAudioActionSource::Inline)
        {
            return &Row.InlineAudioActions;
        }
        if (Row.AudioActionSource == EWandererAudioActionSource::Asset
            && IsValid(Row.AudioActionsAsset))
        {
            return &Row.AudioActionsAsset->AudioActions;
        }
        return nullptr;
    }

    bool ValidateUIActions(const TArray<FWandererAudioAction>& Actions)
    {
        // UI currently supports finite ambient PostEvent only. Ducking needs a separate
        // balanced screen lifetime/mix owner before global parameter/state operations are enabled.
        if (Actions.Num() != 1)
        {
            return false;
        }
        const FWandererAudioAction& Action = Actions[0];
        return !Action.AudioActionID.IsNone()
               && Action.AudioActionType == EWandererAudioActionType::PostEvent
               && Action.Target == EWandererAudioTarget::Owner && IsValid(Action.Event);
    }
    UAkAudioEvent* ResolveConfiguredEvent(
        const UWandererUIAudioProfile* Profile, const UClass* InScreenClass, FName ActionID)
    {
        FWandererUIAudioActionRow Row;
        FString Error;
        if (!IsValid(Profile) || !Profile->FindAction(ActionID, Row, Error))
        {
            UE_LOG(LogWandererUIAudio,
                Error,
                TEXT("%s/%s: %s"),
                *InScreenClass->GetName(),
                *ActionID.ToString(),
                IsValid(Profile) ? *Error : TEXT("UI Profile is missing."));
            return nullptr;
        }
        if (!Row.bEnabled)
        {
            return nullptr;
        }
        const TArray<FWandererAudioAction>* Actions = GetAudioActions(Row);
        if (!Actions || !ValidateUIActions(*Actions))
        {
            UE_LOG(LogWandererUIAudio,
                Error,
                TEXT("%s/%s: configure one valid PostEvent AudioAction (Owner target). "
                     "Global mix operations are not enabled for UI yet."),
                *Profile->GetName(),
                *ActionID.ToString());
            return nullptr;
        }
        return (*Actions)[0].Event;
    }
} // namespace

int32 UWandererUIAudioSubsystem::PlayAction(
    UWorld* InWorld, const UClass* InScreenClass, EWandererUIAudioAction InAction)
{
    if (!IsInGameThread() || !CanPlay(InWorld) || !IsValid(InScreenClass)
        || InAction == EWandererUIAudioAction::Silent)
    {
        return 0;
    }
    const FName ActionID = GetActionID(InAction);
    if (ActionID.IsNone())
    {
        return 0;
    }
    const TStrongObjectPtr<UWandererUIAudioSubsystem> KeepAlive(this);
    const TWeakObjectPtr<UWorld> WorldWeakPtr(InWorld);
    const TWeakObjectPtr<const UClass> ScreenClassWeakPtr(InScreenClass);
    FDiagnosticTraceScope Trace;
    WANDERER_DIAGNOSTIC(InWorld,
        EDiagnosticDomain::Audio,
        TEXT("ui-request-received"),
        false,
        FString::Printf(TEXT("screen=%s action=%d"),
            *GetPathNameSafe(InScreenClass),
            static_cast<int32>(InAction)));
    const TStrongObjectPtr<UWandererUIAudioProfile> Profile(ResolveProfile(InScreenClass));
    if (!CanPlay(WorldWeakPtr.Get()) || !ScreenClassWeakPtr.IsValid())
    {
        return 0;
    }
    const TStrongObjectPtr<UAkAudioEvent> Event(
        ResolveConfiguredEvent(Profile.Get(), ScreenClassWeakPtr.Get(), ActionID));
    if (!Event)
    {
        WANDERER_DIAGNOSTIC(WorldWeakPtr.Get(),
            EDiagnosticDomain::Audio,
            TEXT("ui-rejected"),
            true,
            TEXT("Profile missing/invalid or action disabled; see UI domain log."));
        return 0;
    }
    const int32 PlayingID = PostFiniteEvent(WorldWeakPtr.Get(), Event.Get());
    WANDERER_DIAGNOSTIC(WorldWeakPtr.Get(),
        EDiagnosticDomain::Audio,
        TEXT("ui-post-result"),
        !PlayingID,
        FString::Printf(TEXT("event=%s playingID=%d route=ambient; SDK acceptance only"),
            *GetPathNameSafe(Event.Get()),
            PlayingID));
    UE_LOG(LogWandererUIAudio,
        Verbose,
        TEXT("Screen=%s Action=%s Profile=%s Event=%s PlayingID=%d"),
        *GetNameSafe(ScreenClassWeakPtr.Get()),
        *ActionID.ToString(),
        *GetNameSafe(Profile.Get()),
        *GetNameSafe(Event.Get()),
        PlayingID);
    return PlayingID;
}

int32 UWandererUIAudioSubsystem::PostFiniteEvent(UWorld* InWorld, UAkAudioEvent* InEvent)
{
    if (!CanPlay(InWorld) || !IsValid(InEvent) || !FAkAudioDevice::IsInitialized())
    {
        return 0;
    }
    // Assets may first load before PIE while the editor resource loader is disabled.
    const TWeakObjectPtr<UWorld> WorldWeakPtr(InWorld);
    if (!InEvent->IsLoaded())
    {
        InEvent->LoadData();
    }
    if (!CanPlay(WorldWeakPtr.Get()) || !IsValid(InEvent))
    {
        return 0;
    }
    if (!InEvent->IsLoaded() || !InEvent->GetShortID() || InEvent->IsInfinite)
    {
        UE_LOG(LogWandererUIAudio,
            Error,
            TEXT("%s: UI requires a loaded finite Event with generated data."),
            *GetNameSafe(InEvent));
        return 0;
    }
    const int32 PlayingID = static_cast<int32>(
        InEvent->PostAmbient(nullptr, nullptr, nullptr, static_cast<AkCallbackType>(0), nullptr));
    if (!PlayingID)
    {
        UE_LOG(LogWandererUIAudio,
            Warning,
            TEXT("UI event %s failed; check generated SoundBanks."),
            *GetNameSafe(InEvent));
    }
    return PlayingID;
}
