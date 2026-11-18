#include "Actors/RadioInventoryActor.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"

#include "Actors/WandererSpeakerActor.h"
#include "Framework/RadioControlModel.h"
#include "Framework/WandererPlayerController.h"
#include "Systems/Audio/WandererAudioDiagnostics.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogWandererRecorder, Log, All);
void ARadioInventoryActor::BeginPlay()
{
    Super::BeginPlay();
    AWandererPlayerController* Player = GetController();
    URadioControlModel* Model = Player ? Player->GetRadioControlModel() : nullptr;
    if (!Model || !Model->RegisterProvider(this))
    {
        Destroy();
        return;
    }
    ModelWeakPtr = Model;
}

TArray<FRadioEmitterSnapshot> ARadioInventoryActor::CollectEmitterSnapshots() const
{
    TArray<FRadioEmitterSnapshot> Result;
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
    {
        const AWandererSpeakerActor* Source = SourceWeakPtr.Get();
        if (!Source || !Source->GetEmitterHandle().IsSet())
        {
            continue;
        }
        FRadioEmitterSnapshot& Item = Result.AddDefaulted_GetRef();
        Item.ContentID = Source->GetOptionID();
        Item.Number = Source->GetDeviceNumber();
        Item.bPlaying = Source->IsPlaybackRequested();
        Item.EmitterHandle = Source->GetEmitterHandle();
    }
    return Result;
}

int32 ARadioInventoryActor::GetDeviceChannelMask(const AWandererSpeakerActor* InSource) const
{
    UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    return Audio && IsOwnedSource(InSource)
               ? Audio->GetRadioChannelMask(const_cast<ARadioInventoryActor*>(this),
                     InSource->GetDeviceNumber(),
                     InSource->GetOptionID(),
                     CollectEmitterSnapshots())
               : 0;
}

int32 ARadioInventoryActor::GetDevicePlaybackPositionMs(const AWandererSpeakerActor* InSource) const
{
    UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    return Audio && IsOwnedSource(InSource)
               ? Audio->GetRadioPosition(const_cast<ARadioInventoryActor*>(this),
                     InSource->GetDeviceNumber(),
                     InSource->GetOptionID())
               : -1;
}

FName ARadioInventoryActor::GetRecorderTransportState() const
{
    const AWandererSpeakerActor* Source = SelectedSourceWeakPtr.Get();
    if (!Source)
    {
        return TEXT("NoSelection");
    }
    if (Source->IsPlaybackRequested())
    {
        const UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
        const auto Phase = Audio ? Audio->GetRadioPlaybackPhase(
            const_cast<ARadioInventoryActor*>(this), Source->GetOptionID()) : ERadioPlaybackPhase::Stopped;
        if (Phase == ERadioPlaybackPhase::LoadingMedia)
        {
            return TEXT("LoadingMedia");
        }
        if (Phase == ERadioPlaybackPhase::WaitingForEmitters)
        {
            return TEXT("WaitingForEmitters");
        }
        if (Phase == ERadioPlaybackPhase::WaitingForListener)
        {
            return TEXT("WaitingForListener");
        }
        return TEXT("Playing");
    }
    return (GetWorld()->GetSubsystem<UWandererAudioSubsystem>()
               && GetWorld()->GetSubsystem<UWandererAudioSubsystem>()->HasRadioContent(
                   const_cast<ARadioInventoryActor*>(this), Source->GetOptionID()))
               ? TEXT("Paused")
               : TEXT("Stopped");
}

void ARadioInventoryActor::LogRecorderAction(
    FName InAction, FName InResult, FName InPreviousState, const FString& InDetail)
{
    if (bPublishingIntent || !FWandererAudioDiagnostics::IsEnabled(EWandererAudioLogChannel::Amb))
    {
        return;
    }
    int32 Recorders = 0;
    int32 Monitors = 0;
    FString Devices;
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
    {
        if (!SourceWeakPtr.IsValid())
        {
            continue;
        }
        if (SourceWeakPtr->GetItemType() == EWandererSpeakerItem::Radio)
        {
            ++Recorders;
        }
        else
        {
            ++Monitors;
        }
        Devices += FString::Printf(TEXT(" #%d:%s:%s"),
            SourceWeakPtr->GetDeviceNumber(),
            *SourceWeakPtr->GetOptionID().ToString(),
            SourceWeakPtr->IsPlaybackRequested() ? TEXT("Playing") : TEXT("Paused/Stopped"));
    }
    const FString Text = FString::Printf(
        TEXT("UserAction=%s Result=%s Speakers=%d Recorders=%d Monitors=%d ")
        TEXT("Selected=%d State=%s->%s Devices=[%s] | %s"),
        *InAction.ToString(),
        *InResult.ToString(),
        Recorders + Monitors,
        Recorders,
        Monitors,
        SelectedSourceWeakPtr.IsValid() ? SelectedSourceWeakPtr->GetDeviceNumber() : 0,
        *InPreviousState.ToString(),
        *GetRecorderTransportState().ToString(),
        *Devices,
        *InDetail);
    TGuardValue<bool> Guard(bPublishingIntent, true);
    FWandererAudioDiagnostics::Publish(
        this, EWandererAudioLogChannel::Amb, InResult, Text, TEXT("Speaker"));
    UE_LOG(LogWandererRecorder, Log, TEXT("[Audio/Amb][Speaker] %s"), *Text);
}

bool ARadioInventoryActor::PausePairPlayback()
{
    const TArray<TWeakObjectPtr<AWandererSpeakerActor>> Snapshot = SourcesWeakPtrs;
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : Snapshot)
    {
        if (SourceWeakPtr.IsValid())
        {
            SetSourceIntent(*SourceWeakPtr, SourceWeakPtr->GetOptionID(), false);
        }
    }
    return UpdatePairPlayback(0);
}

void ARadioInventoryActor::SetSourceIntent(
    AWandererSpeakerActor& InOutSource, FName InContentID, bool bInPlaying)
{
    TGuardValue<bool> Guard(bPublishingIntent, true);
    InOutSource.RequestPlayback(InContentID, bInPlaying);
}

bool ARadioInventoryActor::StartPairPlayback()
{
    AWandererSpeakerActor* Source = SelectedSourceWeakPtr.Get();
    UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    if (!Audio || !IsOwnedSource(Source) || bShuttingDown || bPublishingIntent)
    {
        return false;
    }
    const TWeakObjectPtr<AWandererSpeakerActor> SourceWeakPtr(Source);
    const FName ContentID = Source->GetOptionID();
    if (!Audio->StartRadioPlayback(this, ContentID, CollectEmitterSnapshots(), Status))
    {
        return false;
    }
    Source = SourceWeakPtr.Get();
    if (bShuttingDown || SelectedSourceWeakPtr != SourceWeakPtr || !IsOwnedSource(Source)
        || Source->GetOptionID() != ContentID)
    {
        return false;
    }
    SetSourceIntent(*Source, ContentID, true);
    const bool bUpdated = UpdatePairPlayback(0);
    if (bUpdated)
    {
        Status = TEXT("Selected device playback requested; matching content stays in sync.");
    }
    return bUpdated;
}

void ARadioInventoryActor::StopPairPlayback()
{
    const TArray<TWeakObjectPtr<AWandererSpeakerActor>> Snapshot = SourcesWeakPtrs;
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : Snapshot)
    {
        if (SourceWeakPtr.IsValid())
        {
            SetSourceIntent(*SourceWeakPtr, SourceWeakPtr->GetOptionID(), false);
        }
    }
    if (UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>())
    {
        Audio->ReleaseRadioPlayback(this);
    }
}

bool ARadioInventoryActor::UpdatePairPlayback(float InDeltaTime)
{
    UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    if (!Audio || bShuttingDown)
    {
        return false;
    }
    TArray<FName> Failed;
    const auto Submitted = CollectEmitterSnapshots();
    const bool bUpdated = Audio->UpdateRadioPlayback(this, Submitted, InDeltaTime, Failed);
    if (Failed.IsEmpty() || bShuttingDown)
    {
        return bUpdated;
    }
    bool bCurrentFailure = false;
    const TArray<TWeakObjectPtr<AWandererSpeakerActor>> Snapshot = SourcesWeakPtrs;
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : Snapshot)
    {
        auto* Source = SourceWeakPtr.Get();
        if (!Source || !Failed.Contains(Source->GetOptionID()))
        {
            continue;
        }
        const auto& Handle = Source->GetEmitterHandle();
        const FName ContentID = Source->GetOptionID();
        if (Submitted.ContainsByPredicate([&Handle, ContentID](const FRadioEmitterSnapshot& Item)
            { return Item.ContentID == ContentID && Item.EmitterHandle.Generation == Handle.Generation; }))
        {
            bCurrentFailure = true;
            SetSourceIntent(*Source, ContentID, false);
        }
    }
    if (bCurrentFailure && !bShuttingDown)
    {
        Status = TEXT("Radio audio failed to load or playback ended. Select Play to retry.");
        LogRecorderAction(TEXT("SystemStop"), TEXT("Failed"), TEXT("Playing"), Status);
    }
    return bUpdated;
}
