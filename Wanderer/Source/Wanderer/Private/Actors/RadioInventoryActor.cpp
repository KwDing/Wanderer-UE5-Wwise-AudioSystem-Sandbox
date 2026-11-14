#include "Actors/RadioInventoryActor.h"

#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Templates/UnrealTemplate.h"

#include "Actors/WandererSpeakerActor.h"
#include "Components/ItemPlacementComponent.h"
#include "Data/WandererSpeakerSettings.h"
#include "Framework/RadioControlModel.h"
#include "Framework/WandererGameState.h"
#include "Framework/WandererPlayerController.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

namespace RadioInteractionDefaults
{
    constexpr float UpdateIntervalSec = 0.05f;
    constexpr double CursorRayLength = 2000;
    constexpr float DebugDuration = 0.06f;
    constexpr float DebugLineWidth = 1.5f;
} // namespace RadioInteractionDefaults

ARadioInventoryActor::ARadioInventoryActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = RadioInteractionDefaults::UpdateIntervalSec;
    Placement = CreateDefaultSubobject<UItemPlacementComponent>(TEXT("ItemPlacement"));
}

void ARadioInventoryActor::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
    bShuttingDown = true;
    SetActorTickEnabled(false);
    if (ModelWeakPtr.IsValid())
    {
        ModelWeakPtr->UnregisterProvider(this);
    }
    SetRadioPreviewEnabled(false);
    StopPairPlayback();

    CancelPlacement();
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
    {
        if (SourceWeakPtr.IsValid())
        {
            SourceWeakPtr->Destroy();
        }
    }
    SourcesWeakPtrs.Empty();
    SelectedSourceWeakPtr.Reset();
    DismissedSourceWeakPtr.Reset();
    RadioMesh = nullptr;
    MountedMesh = nullptr;
    Super::EndPlay(InEndPlayReason);
}

void ARadioInventoryActor::Tick(float InDeltaTime)
{
    Super::Tick(InDeltaTime);
    UpdatePairPlayback(InDeltaTime);
    if (!IsGameplayReady())
    {
        SetInteractionOpen(false);
        SelectedSourceWeakPtr.Reset();
        return;
    }
    UpdateNearbySelection();
    if (SelectedSourceWeakPtr.IsValid())
    {
        DrawDebugBox(GetWorld(),
            SelectedSourceWeakPtr->GetActorLocation(),
            SelectedSourceWeakPtr->GetPlacementExtent(),
            SelectedSourceWeakPtr->GetActorQuat(),
            FColor(232, 190, 95),
            false,
            RadioInteractionDefaults::DebugDuration,
            0,
            RadioInteractionDefaults::DebugLineWidth);
    }
    UpdatePlacementPreview();
}

int32 ARadioInventoryActor::GetAvailableCount(EWandererSpeakerItem InItem) const
{
    const UWandererSpeakerSettings* Settings = GetDefault<UWandererSpeakerSettings>();
    if (InItem == EWandererSpeakerItem::Automatic)
    {
        return GetAvailableCount(EWandererSpeakerItem::Radio)
               + GetAvailableCount(EWandererSpeakerItem::Mounted);
    }
    int32 Count = FMath::Clamp(InItem == EWandererSpeakerItem::Radio ? Settings->RadioInventory
                                                                     : Settings->MountedInventory,
        0,
        2);
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
    {
        if (SourceWeakPtr.IsValid() && SourceWeakPtr->GetItemType() == InItem)
        {
            --Count;
        }
    }
    return FMath::Max(0, Count);
}

AWandererPlayerController* ARadioInventoryActor::GetController() const
{
    return Cast<AWandererPlayerController>(GetOwner());
}

TArray<AWandererSpeakerActor*> ARadioInventoryActor::GetSources() const
{
    TArray<AWandererSpeakerActor*> Result;
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
    {
        if (SourceWeakPtr.IsValid())
        {
            Result.Add(SourceWeakPtr.Get());
        }
    }
    return Result;
}

bool ARadioInventoryActor::IsGameplayReady() const
{
    const AWandererPlayerController* Player = GetController();
    return !bShuttingDown && !bPublishingIntent && Player && Player->IsRadioInteractionReady();
}

bool ARadioInventoryActor::IsOwnedSource(const AWandererSpeakerActor* InSource) const
{
    return IsValid(InSource)
           && SourcesWeakPtrs.ContainsByPredicate(
               [InSource](const TWeakObjectPtr<AWandererSpeakerActor>& EntryWeakPtr)
               {
                   return EntryWeakPtr.Get() == InSource;
               });
}

bool ARadioInventoryActor::RetrieveAllSources()
{
    const FName PreviousState = GetRecorderTransportState();
    if (!IsGameplayReady())
    {
        LogRecorderAction(
            TEXT("RetrieveAll"), TEXT("Rejected"), PreviousState, TEXT("Gameplay unavailable."));
        return false;
    }
    PausePairPlayback();
    CancelPlacement();
    SelectedSourceWeakPtr.Reset();
    DismissedSourceWeakPtr.Reset();
    const TArray<TWeakObjectPtr<AWandererSpeakerActor>> Snapshot = SourcesWeakPtrs;
    SourcesWeakPtrs.Reset();
    {
        TGuardValue<bool> Guard(bPublishingIntent, true);
        for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : Snapshot)
        {
            if (SourceWeakPtr.IsValid())
            {
                SourceWeakPtr->Destroy();
            }
        }
    }
    Status =
        TEXT("Playback paused. All devices retrieved; place a device and press Play to resume.");
    LogRecorderAction(TEXT("RetrieveAll"), TEXT("Accepted"), PreviousState, Status);
    return true;
}

bool ARadioInventoryActor::RetrieveSource(AWandererSpeakerActor* InSource)
{
    const FName PreviousState = GetRecorderTransportState();
    if (!IsGameplayReady() || !IsOwnedSource(InSource))
    {
        return false;
    }
    if (!CanRetrieveSource(InSource))
    {
        Status = TEXT("Move within retrieval range, with a clear line of sight.");
        return false;
    }
    if (SelectedSourceWeakPtr.Get() == InSource)
    {
        SelectedSourceWeakPtr.Reset();
    }
    SourcesWeakPtrs.RemoveAll(
        [InSource](const TWeakObjectPtr<AWandererSpeakerActor>& EntryWeakPtr)
        {
            return !EntryWeakPtr.IsValid() || EntryWeakPtr.Get() == InSource;
        });
    {
        TGuardValue<bool> Guard(bPublishingIntent, true);
        InSource->Destroy();
    }
    if (GetSources().IsEmpty())
    {
        PausePairPlayback();
    }
    UpdatePairPlayback(0);
    Status = TEXT("Retrieved. A source slot is available again.");
    LogRecorderAction(TEXT("Retrieve"), TEXT("Accepted"), PreviousState, Status);
    return true;
}

void ARadioInventoryActor::SetInteractionOpen(bool bInOpen)
{
    if (bInOpen && !IsGameplayReady())
    {
        return;
    }
    bInteractionOpen = bInOpen;
    if (!bInOpen)
    {
        CancelPlacement();
        CancelSelection();
        bCursorOverPanel = false;
    }
}

bool ARadioInventoryActor::BeginPlacement()
{
    return BeginItemPlacement(EWandererSpeakerItem::Automatic);
}

void ARadioInventoryActor::CancelSelection()
{
    DismissedSourceWeakPtr = SelectedSourceWeakPtr;
    SelectedSourceWeakPtr.Reset();
}

bool ARadioInventoryActor::CanRetrieveSource(const AWandererSpeakerActor* InSource) const
{
    return IsGameplayReady() && IsOwnedSource(InSource)
           && (SelectedSourceWeakPtr.Get() == InSource || IsSourceNearby(InSource));
}

FName ARadioInventoryActor::GetSelectedOption() const
{
    return SelectedSourceWeakPtr.IsValid() ? SelectedSourceWeakPtr->GetOptionID() : PendingOption;
}

bool ARadioInventoryActor::IsSourceNearby(const AWandererSpeakerActor* InSource) const
{
    if (!IsGameplayReady() || !IsOwnedSource(InSource))
    {
        return false;
    }
    const FVector Origin = GetController()->GetPawn()->GetActorLocation();
    if (FVector::Distance(Origin, InSource->GetActorLocation())
        > GetDefault<UWandererSpeakerSettings>()->RetrieveDistance)
    {
        return false;
    }
    FHitResult Hit;
    FCollisionQueryParams Query(
        SCENE_QUERY_STAT(SpeakerRetrieve), true, GetController()->GetPawn());
    return !GetWorld()->LineTraceSingleByChannel(
               Hit, Origin, InSource->GetActorLocation(), ECC_Visibility, Query)
           || Hit.GetActor() == InSource;
}

bool ARadioInventoryActor::SelectOption(FName InID)
{
    const FName PreviousState = GetRecorderTransportState();
    if (!IsGameplayReady())
    {
        return false;
    }
    const UWandererSpeakerSettings* Settings = GetDefault<UWandererSpeakerSettings>();
    if (!UWandererAudioSubsystem::IsRadioContentKnown(InID)
        || !Settings->Options.ContainsByPredicate(
            [InID](const FWandererSpeakerOption& Option)
            {
                return Option.ID == InID;
            }))
    {
        return false;
    }
    PendingOption = InID;
    AWandererSpeakerActor* Source = SelectedSourceWeakPtr.Get();
    if (!IsOwnedSource(Source))
    {
        Status = TEXT("Default content for next placement selected.");
        return true;
    }
    if (Source->GetOptionID() == InID)
    {
        return true;
    }
    const bool bWasPlaying = Source->IsPlaybackRequested();
    SetSourceIntent(*Source, InID, false);
    UpdatePairPlayback(0);
    const bool bSuccess = !bWasPlaying || StartPairPlayback();
    if (!bWasPlaying)
    {
        Status = TEXT("Selected device content changed. Press Play to start.");
    }
    LogRecorderAction(
        TEXT("SelectContent"), bSuccess ? TEXT("Accepted") : TEXT("Failed"), PreviousState, Status);
    return bSuccess;
}

bool ARadioInventoryActor::SelectSource(AWandererSpeakerActor* InSource)
{
    if (!IsGameplayReady() || !IsOwnedSource(InSource))
    {
        return false;
    }
    CancelPlacement();
    DismissedSourceWeakPtr.Reset();
    SelectedSourceWeakPtr = InSource;
    return true;
}

bool ARadioInventoryActor::SelectSourceAlongRay(FVector InStart, FVector InDirection)
{
    if (!IsGameplayReady() || bCursorOverPanel || IsPlacing() || InStart.ContainsNaN()
        || InDirection.ContainsNaN() || InDirection.IsNearlyZero())
    {
        return false;
    }
    FHitResult Hit;
    FCollisionQueryParams Query(
        SCENE_QUERY_STAT(SpeakerMouseSelect), true, GetController()->GetPawn());
    if (!GetWorld()->LineTraceSingleByChannel(Hit,
            InStart,
            InStart + InDirection.GetSafeNormal() * RadioInteractionDefaults::CursorRayLength,
            ECC_Visibility,
            Query))
    {
        return false;
    }
    return SelectSource(Cast<AWandererSpeakerActor>(Hit.GetActor()));
}

bool ARadioInventoryActor::SelectSourceAtCursor()
{
    if (!IsGameplayReady() || bCursorOverPanel || IsPlacing())
    {
        return false;
    }
    FVector Start;
    FVector Direction;
    if (!GetController()->DeprojectMousePositionToWorld(Start, Direction))
    {
        return false;
    }
    return SelectSourceAlongRay(Start, Direction);
}

bool ARadioInventoryActor::TogglePlaybackRequest()
{
    const FName PreviousState = GetRecorderTransportState();
    AWandererSpeakerActor* Source = SelectedSourceWeakPtr.Get();
    const FName Action = Source && Source->IsPlaybackRequested() ? TEXT("Pause") : TEXT("Play");
    if (!IsGameplayReady() || !IsOwnedSource(Source) || Source->GetOptionID().IsNone())
    {
        Status = TEXT("Choose a deployed device in the E list before Play/Pause.");
        LogRecorderAction(Action, TEXT("Rejected"), PreviousState, Status);
        return false;
    }
    bool bSuccess = false;
    if (Source->IsPlaybackRequested())
    {
        SetSourceIntent(*Source, Source->GetOptionID(), false);
        bSuccess = UpdatePairPlayback(0);
        Status = TEXT("Selected device paused. Other devices keep their state.");
    }
    else
    {
        bSuccess = StartPairPlayback();
    }
    LogRecorderAction(Action, bSuccess ? TEXT("Accepted") : TEXT("Failed"), PreviousState, Status);
    return bSuccess;
}

void ARadioInventoryActor::UpdateNearbySelection()
{
    if (DismissedSourceWeakPtr.IsValid() && !IsSourceNearby(DismissedSourceWeakPtr.Get()))
    {
        DismissedSourceWeakPtr.Reset();
    }
    if (bInteractionOpen || IsPlacing())
    {
        return;
    }
    if (CanRetrieveSource(SelectedSourceWeakPtr.Get()))
    {
        return;
    }
    SelectedSourceWeakPtr.Reset();
    double Distance = GetDefault<UWandererSpeakerSettings>()->RetrieveDistance;
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
    {
        if (SourceWeakPtr == DismissedSourceWeakPtr || !IsSourceNearby(SourceWeakPtr.Get()))
        {
            continue;
        }
        const double Candidate = FVector::Distance(
            GetController()->GetPawn()->GetActorLocation(), SourceWeakPtr->GetActorLocation());
        if (Candidate < Distance)
        {
            SelectedSourceWeakPtr = SourceWeakPtr;
            Distance = Candidate;
        }
    }
}
