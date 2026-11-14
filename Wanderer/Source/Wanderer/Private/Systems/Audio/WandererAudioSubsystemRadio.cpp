#include "Systems/Audio/WandererAudioSubsystem.h"

#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

#include "Systems/Audio/RadioChannelLayout.h"
#include "Systems/Audio/RadioPlaybackManager.h"

bool UWandererAudioSubsystem::IsRadioContentKnown(FName InContentID)
{
    return URadioPlaybackManager::ResolveRadioSwitch(InContentID) != nullptr;
}

bool UWandererAudioSubsystem::StartRadioPlayback(AActor* InOwner,
    FName InContentID,
    const TArray<FRadioEmitterSnapshot>& InSources,
    FString& OutStatus)
{
    check(IsInGameThread());
    if (bDeinitializing || !IsValid(InOwner) || InOwner->GetWorld() != GetWorld())
    {
        return false;
    }
    const TObjectPtr<URadioPlaybackManager>* Registered = RadioSessions.Find(InOwner);
    if (Registered)
    {
        const TStrongObjectPtr<URadioPlaybackManager> Session(Registered->Get());
        return Session->Start(InContentID, InSources, OutStatus);
    }
    const TWeakObjectPtr<AActor> OwnerWeakPtr(InOwner);
    const TStrongObjectPtr<URadioPlaybackManager> Session(NewObject<URadioPlaybackManager>(this));
    RadioSessions.Add(OwnerWeakPtr, Session.Get());
    if (!Session->Initialize(InOwner) || !OwnerWeakPtr.IsValid() || bDeinitializing
        || RadioSessions.FindRef(OwnerWeakPtr) != Session.Get())
    {
        if (RadioSessions.FindRef(OwnerWeakPtr) == Session.Get())
        {
            RadioSessions.Remove(OwnerWeakPtr);
        }
        Session->Shutdown();
        return false;
    }
    return Session->Start(InContentID, InSources, OutStatus);
}

bool UWandererAudioSubsystem::UpdateRadioPlayback(AActor* InOwner,
    const TArray<FRadioEmitterSnapshot>& InSources,
    float InDeltaTime,
    TArray<FName>& OutFailed)
{
    check(IsInGameThread());
    if (bDeinitializing || !IsValid(InOwner) || InOwner->GetWorld() != GetWorld())
    {
        return false;
    }
    const TObjectPtr<URadioPlaybackManager>* Registered = RadioSessions.Find(InOwner);
    if (!Registered)
    {
        return true;
    }
    const TStrongObjectPtr<URadioPlaybackManager> Session(Registered->Get());
    return Session->Update(InSources, InDeltaTime, OutFailed);
}

void UWandererAudioSubsystem::ReleaseRadioPlayback(AActor* InOwner)
{
    check(IsInGameThread());
    TObjectPtr<URadioPlaybackManager> Session;
    if (RadioSessions.RemoveAndCopyValue(InOwner, Session) && Session)
    {
        const TStrongObjectPtr<URadioPlaybackManager> Active(Session.Get());
        Active->Shutdown();
    }
}

int32 UWandererAudioSubsystem::GetRadioPosition(
    AActor* InOwner, int32 InNumber, FName InContentID) const
{
    const TObjectPtr<URadioPlaybackManager>* Session = RadioSessions.Find(InOwner);
    return Session ? (*Session)->GetPosition(InNumber, InContentID) : -1;
}

int32 UWandererAudioSubsystem::GetRadioChannelMask(AActor* InOwner,
    int32 InNumber,
    FName InContentID,
    const TArray<FRadioEmitterSnapshot>& InSources) const
{
    const TObjectPtr<URadioPlaybackManager>* Session = RadioSessions.Find(InOwner);
    return Session ? (*Session)->GetChannelMask(InNumber, InContentID, InSources)
                   : FRadioChannelLayout(InSources, InContentID, true).GetChannelMask(InNumber - 1);
}

bool UWandererAudioSubsystem::HasRadioContent(AActor* InOwner, FName InContentID) const
{
    const TObjectPtr<URadioPlaybackManager>* Session = RadioSessions.Find(InOwner);
    return Session && (*Session)->HasContent(InContentID);
}

void UWandererAudioSubsystem::PruneRadioSessions()
{
    TArray<TStrongObjectPtr<URadioPlaybackManager>> Expired;
    for (auto It = RadioSessions.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            Expired.Emplace(It.Value().Get());
            It.RemoveCurrent();
        }
    }
    for (const TStrongObjectPtr<URadioPlaybackManager>& Session : Expired)
    {
        Session->Shutdown();
    }
}

void UWandererAudioSubsystem::ShutdownRadioSessions()
{
    TArray<TStrongObjectPtr<URadioPlaybackManager>> Sessions;
    Sessions.Reserve(RadioSessions.Num());
    for (const TPair<TWeakObjectPtr<AActor>, TObjectPtr<URadioPlaybackManager>>& Entry :
        RadioSessions)
    {
        Sessions.Emplace(Entry.Value.Get());
    }
    RadioSessions.Reset();
    for (const TStrongObjectPtr<URadioPlaybackManager>& Session : Sessions)
    {
        Session->Shutdown();
    }
}
