#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "AkComponent.h"
#include "AkSwitchValue.h"

#include "Data/Audio/WandererCharacterAudioProfile.h"
#include "Systems/Audio/WandererAudioDiagnostics.h"

DEFINE_LOG_CATEGORY_STATIC(LogWandererCharacterAudio, Log, All);

namespace
{
    EWandererAudioLogChannel GetActionChannel(FName ActionID)
    {
        static const FName Footstep(TEXT("Footstep"));
        return ActionID == Footstep ? EWandererAudioLogChannel::Footsteps
                                    : EWandererAudioLogChannel::Actions;
    }
} // namespace

UWandererCharacterAudioComponent::UWandererCharacterAudioComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bAutoActivate = true;
}

bool UWandererCharacterAudioComponent::InitializeAudio(UAkComponent* InEmitter)
{
    if (!IsInGameThread() || bStopped)
    {
        return false;
    }
    bReady = false;
    StopPlayback();
    EmitterWeakPtr.Reset();
    ResetProfileCache();

    if (!IsValid(InEmitter) || InEmitter->GetOwner() != GetOwner() || !InEmitter->IsRegistered())
    {
        return FailConfiguration(TEXT("Audio emitter must be registered and owned by this actor."));
    }

    EmitterWeakPtr = InEmitter;
    bReady = BuildProfileCache();
    if (!bReady)
    {
        ResetProfileCache();
        return false;
    }
    if (FWandererAudioDiagnostics::IsEnabled(EWandererAudioLogChannel::Configuration))
    {
        FWandererAudioDiagnostics::Publish(this,
            EWandererAudioLogChannel::Configuration,
            TEXT("Info"),
            FString::Printf(
                TEXT("%s: ready; Profile=%s, emitter=%s, active=%s, actions=%d, switches=%d/%d/%d"),
                *GetNameSafe(GetOwner()),
                *GetNameSafe(Profile.Get()),
                *GetNameSafe(InEmitter),
                IsActive() ? TEXT("true") : TEXT("false"),
                CachedActions.Num(),
                SurfaceSwitches.Num(),
                GaitSwitches.Num(),
                WetnessSwitches.Num()));
    }
    return bReady;
}

int32 UWandererCharacterAudioComponent::PlayAction(FName ActionID,
    const FWandererCharacterAudioContext& Context,
    EWandererAudioPlaybackScope Scope)
{
    const FWandererAudioExecutionResult Result = ExecuteAction(ActionID, Context, Scope);
    return static_cast<int32>(static_cast<uint32>(Result.PlayingID));
}

void UWandererCharacterAudioComponent::StopPlayback()
{
    if (UAkComponent* AudioEmitter = EmitterWeakPtr.Get();
        IsValid(AudioEmitter) && AudioEmitter->IsRegistered())
    {
        AudioEmitter->Stop();
    }

    for (auto& Entry : NextAllowedTimes)
    {
        Entry.Value = FActionCooldowns{};
    }
}

void UWandererCharacterAudioComponent::BeginPlay()
{
    bStopped = false;
    Super::BeginPlay();
}

void UWandererCharacterAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bStopped = true;
    bReady = false;
    StopPlayback();
    EmitterWeakPtr.Reset();

    ResetProfileCache();

    Super::EndPlay(EndPlayReason);
}

bool UWandererCharacterAudioComponent::FailConfiguration(const FString& Message) const
{
    UE_LOG(LogWandererCharacterAudio, Error, TEXT("%s: %s"), *GetNameSafe(GetOwner()), *Message);
    FWandererAudioDiagnostics::Publish(this,
        EWandererAudioLogChannel::Configuration,
        TEXT("Error"),
        FString::Printf(TEXT("%s: %s"), *GetNameSafe(GetOwner()), *Message));
    return false;
}

void UWandererCharacterAudioComponent::ReportSkip(
    FName ActionID, const TCHAR* Reason, const TCHAR* Message)
{
    const EWandererAudioLogChannel Channel = GetActionChannel(ActionID);
    if (!FWandererAudioDiagnostics::IsEnabled(Channel))
    {
        return;
    }
    const FName ReasonID(Reason);
    if (const FName* Previous = LastSkipReasons.Find(ActionID); Previous && *Previous == ReasonID)
    {
        return;
    }
    LastSkipReasons.Add(ActionID, ReasonID);
    FWandererAudioDiagnostics::Publish(this,
        Channel,
        TEXT("Skip"),
        FString::Printf(
            TEXT("%s / %s: %s"), *GetNameSafe(GetOwner()), *ActionID.ToString(), Message));
}

void UWandererCharacterAudioComponent::WarnOnce(FName ActionID, const TCHAR* Message)
{
    if (WarnedActions.Contains(ActionID))
    {
        return;
    }

    WarnedActions.Add(ActionID);

    UE_LOG(LogWandererCharacterAudio,
        Warning,
        TEXT("%s / %s: %s"),
        *GetNameSafe(GetOwner()),
        *ActionID.ToString(),
        Message);
    FWandererAudioDiagnostics::Publish(this,
        EWandererAudioLogChannel::Playback,
        TEXT("Warning"),
        FString::Printf(
            TEXT("%s / %s: %s"), *GetNameSafe(GetOwner()), *ActionID.ToString(), Message));
}

double& UWandererCharacterAudioComponent::FActionCooldowns::Get(EWandererAudioPlaybackScope Scope)
{
    if (Scope == EWandererAudioPlaybackScope::LeftFoot)
    {
        return LeftFoot;
    }
    if (Scope == EWandererAudioPlaybackScope::RightFoot)
    {
        return RightFoot;
    }
    return Default;
}
