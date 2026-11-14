#include "Features/GameLog/GameLogSnapshotOperation.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"

#include "AkAcousticPortal.h"
#include "AkComponent.h"
#include "AkRoomComponent.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"

namespace
{
    constexpr int32 MaximumSnapshotEntries = 128;

    template <typename ComponentType, typename DescribeType>
    void AppendComponents(UWorld* InWorld, TArray<FString>& OutLines, DescribeType Describe)
    {
        const FString Focus = FDiagnosticRecorder::GetFocus();
        for (TObjectIterator<ComponentType> It; It && OutLines.Num() < MaximumSnapshotEntries; ++It)
        {
            ComponentType* Component = *It;
            if (!IsValid(Component) || Component->GetWorld() != InWorld)
            {
                continue;
            }
            if (!Focus.IsEmpty() && !Component->GetPathName().Contains(Focus))
            {
                continue;
            }
            OutLines.Add(Describe(Component));
        }
    }

    void AppendWorld(UWorld* InWorld, TArray<FString>& OutLines)
    {
        AppendComponents<UWandererAudioSourceComponent>(InWorld,
            OutLines,
            [](UWandererAudioSourceComponent* Source)
            {
                FVector Position;
                const bool bPosition = Source->TryGetPlaybackPosition(Position);
                return FString::Printf(
                    TEXT("[Source] %s profile=%s group=%s enabled=%d "
                         "state=%s event=%s id=%d distanceM=%.2f priority=%.2f room=%s "
                         "actualPositionCm=%s"),
                    *Source->GetPathName(),
                    *Source->Profile.RowName.ToString(),
                    *Source->ResourceGroup.ToString(),
                    Source->bEnabled,
                    *Source->GetStatus(),
                    *Source->GetActiveEventName(),
                    Source->GetPlayingID(),
                    Source->GetDistanceM(),
                    Source->GetEffectivePriority(),
                    *Source->AcousticRoom.ToString(),
                    bPosition ? *Position.ToCompactString() : TEXT("unavailable/room-object"));
            });
        AppendComponents<UAkComponent>(InWorld,
            OutLines,
            [](UAkComponent* Source)
            {
                return FString::Printf(TEXT("[Emitter] %s registered=%d positionCm=%s room=%s"),
                    *Source->GetPathName(),
                    Source->IsRegisteredWithWwise(),
                    *Source->GetComponentLocation().ToCompactString(),
                    *GetPathNameSafe(Source->GetSpatialAudioRoom().Get()));
            });
        AppendComponents<UAkRoomComponent>(InWorld,
            OutLines,
            [](UAkRoomComponent* Room)
            {
                return FString::Printf(TEXT("[Room] %s enabled=%d autoPost=%d auxSend=%.3f "
                                            "reverbZone=%d positionCm=%s"),
                    *Room->GetPathName(),
                    Room->bEnable,
                    Room->AutoPost,
                    Room->AuxSendLevel,
                    Room->bEnableReverbZone,
                    *Room->GetComponentLocation().ToCompactString());
            });
        AppendComponents<UAkPortalComponent>(InWorld,
            OutLines,
            [](UAkPortalComponent* Portal)
            {
                return FString::Printf(TEXT("[Portal] %s state=%d front=%s back=%s positionCm=%s"),
                    *Portal->GetPathName(),
                    static_cast<int32>(Portal->GetCurrentState()),
                    *GetPathNameSafe(Portal->GetFrontRoomComponent().Get()),
                    *GetPathNameSafe(Portal->GetBackRoomComponent().Get()),
                    *Portal->GetComponentLocation().ToCompactString());
            });
    }
} // namespace

TArray<FString> FGameLogSnapshotOperation::Capture()
{
    check(IsInGameThread());
    TArray<FString> Lines;
    Lines.Add(FString::Printf(
        TEXT("[Snapshot %s] UE state only; validate voice/effects/aux sends "
             "in Wwise Profiler. Outdoor portal endpoints may legitimately have no room."),
        *FDateTime::Now().ToString()));
    if (!GEngine)
    {
        return Lines;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (!World || World->WorldType != EWorldType::PIE || World->bIsTearingDown)
        {
            continue;
        }
        if (Lines.Num() >= MaximumSnapshotEntries)
        {
            break;
        }
        Lines.Add(FString::Printf(TEXT("[World] %s"), *World->GetPathName()));
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            if (Lines.Num() >= MaximumSnapshotEntries)
            {
                break;
            }
            APlayerController* Controller = It->Get();
            if (!IsValid(Controller) || !Controller->IsLocalController())
            {
                continue;
            }
            FVector Location;
            FRotator Rotation;
            Controller->GetPlayerViewPoint(Location, Rotation);
            Lines.Add(
                FString::Printf(TEXT("[Listener view] %s positionCm=%s rotation=%s "
                                     "(camera view, Wwise listener overrides require Profiler)"),
                    *Controller->GetPathName(),
                    *Location.ToCompactString(),
                    *Rotation.ToCompactString()));
        }
        AppendWorld(World, Lines);
    }
    if (Lines.Num() >= MaximumSnapshotEntries)
    {
        Lines.Last() = TEXT("[Snapshot truncated] Narrow source filter and capture again.");
    }
    return Lines;
}
