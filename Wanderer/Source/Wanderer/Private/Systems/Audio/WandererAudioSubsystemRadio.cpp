#include "Systems/Audio/WandererAudioSubsystem.h"

#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/RadioChannelLayout.h"
#include "Systems/Audio/RadioPlaybackManager.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"
#include "Systems/Audio/WwiseAudioBackend.h"

bool UWandererAudioSubsystem::IsRadioContentKnown(FName InContentID)
{
    return URadioPlaybackManager::ResolveRadioContent(InContentID).IsSet();
}

bool UWandererAudioSubsystem::PreloadRadioPlayback(AActor* InOwner)
{
    check(IsInGameThread());
    if (bDeinitializing || !IsValid(InOwner) || InOwner->GetWorld() != GetWorld())
    {
        return false;
    }
    if (RadioSessions.Contains(InOwner))
    {
        return true;
    }
    const TWeakObjectPtr<AActor> OwnerWeakPtr(InOwner);
    if (!EmitterPool)
    {
        EmitterPool = NewObject<UAudioEmitterPool>(this);
    }
    const TStrongObjectPtr<URadioPlaybackManager> Session(NewObject<URadioPlaybackManager>(this));
    RadioSessions.Add(OwnerWeakPtr, Session.Get());
    if (!Session->Initialize(InOwner, *EmitterPool) || !OwnerWeakPtr.IsValid() || bDeinitializing
        || RadioSessions.FindRef(OwnerWeakPtr) != Session.Get())
    {
        if (RadioSessions.FindRef(OwnerWeakPtr) == Session.Get())
        {
            RadioSessions.Remove(OwnerWeakPtr);
        }
        Session->Shutdown();
        return false;
    }
    return true;
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
    const auto Sources = ResolveRadioEmitters(*InOwner, InSources);
    if (!Sources.ContainsByPredicate([InContentID](const FRadioEmitterSnapshot& Source)
        { return Source.ContentID == InContentID; }))
    {
        OutStatus = TEXT("No current radio emitter for the requested content.");
        return false;
    }
    if (!PreloadRadioPlayback(InOwner))
    {
        return false;
    }
    const TStrongObjectPtr<URadioPlaybackManager> Session(RadioSessions.FindRef(InOwner).Get());
    bAdmissionDirty = true;
    return Session->Start(InContentID, Sources, OutStatus);
}

void UWandererAudioSubsystem::CollectRadioCandidates(
    const FVector& InListener, TArray<FAudioSourceCandidate>& OutCandidates)
{
    const auto Snapshot = RadioSessions;
    for (const auto& Pair : Snapshot)
    {
        if (Pair.Key.IsValid() && Pair.Value)
        {
            Pair.Value->CollectCandidates(InListener, OutCandidates);
        }
    }
}

int32 UWandererAudioSubsystem::GetRadioActiveCount() const
{
    int32 Count = 0;
    for (const auto& Pair : RadioSessions)
    {
        Count += Pair.Value ? Pair.Value->GetActiveEmitterCount() : 0;
    }
    return Count;
}

void UWandererAudioSubsystem::SuspendRadioAdmissions()
{
    const auto Snapshot = RadioSessions;
    for (const auto& Pair : Snapshot)
    {
        if (Pair.Value)
        {
            Pair.Value->SuspendAdmissions();
        }
    }
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
    return Session->Update(ResolveRadioEmitters(*InOwner, InSources), InDeltaTime, OutFailed);
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
    if (!IsValid(InOwner) || InOwner->GetWorld() != GetWorld())
    {
        return 0;
    }
    const TObjectPtr<URadioPlaybackManager>* Session = RadioSessions.Find(InOwner);
    const auto Sources = ResolveRadioEmitters(*InOwner, InSources);
    const ERadioChannel Channel = Session ? (*Session)->GetChannel(InNumber, InContentID, Sources)
        : FRadioChannelLayout(Sources, InContentID, true).GetChannel(InNumber - 1);
    return WandererAudioBackend::GetRadioChannelMask(Channel);
}

bool UWandererAudioSubsystem::HasRadioContent(AActor* InOwner, FName InContentID) const
{
    const TObjectPtr<URadioPlaybackManager>* Session = RadioSessions.Find(InOwner);
    return Session && (*Session)->HasContent(InContentID);
}

ERadioPlaybackPhase UWandererAudioSubsystem::GetRadioPlaybackPhase(
    AActor* InOwner, FName InContentID) const
{
    const TObjectPtr<URadioPlaybackManager>* Session = RadioSessions.Find(InOwner);
    return Session ? (*Session)->GetPlaybackPhase(InContentID) : ERadioPlaybackPhase::Stopped;
}

TArray<FRadioEmitterSnapshot> UWandererAudioSubsystem::ResolveRadioEmitters(const AActor& InOwner,
    const TArray<FRadioEmitterSnapshot>& InSources) const
{
    TArray<FRadioEmitterSnapshot> Sources;
    Sources.Reserve(InSources.Num());
    for (const FRadioEmitterSnapshot& Source : InSources)
    {
        FTransform Transform = Source.Transform;
        // Direct C++ sessions can still supply value-only spatial sources.
        if (!Source.EmitterHandle.IsSet() || ResolveObjectEmitter(Source.EmitterHandle, Transform, &InOwner))
        {
            Sources.Emplace_GetRef(Source).Transform = Transform;
        }
    }
    return Sources;
}

void UWandererAudioSubsystem::PruneRadioSessions()
{
    TArray<TStrongObjectPtr<URadioPlaybackManager>> Expired;
    for (auto It = RadioSessions.CreateIterator(); It; ++It)
    {
        const AActor* Owner = It.Key().Get();
        if (!Owner || Owner->IsActorBeingDestroyed() || Owner->GetWorld() != GetWorld())
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
