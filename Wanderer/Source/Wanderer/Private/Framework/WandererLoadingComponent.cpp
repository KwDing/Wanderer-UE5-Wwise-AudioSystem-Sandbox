#include "Framework/WandererLoadingComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/SceneEntryProvider.h"
#include "Data/SceneEntryData.h"
#include "PCGSubsystem.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "ContentStreaming.h"
#include "ShaderPipelineCache.h"
#include "PipelineStateCache.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

UWandererLoadingComponent::UWandererLoadingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    Status = FText::FromString(TEXT("Preparing scene…"));
}

void UWandererLoadingComponent::StartLoading()
{
    if (bStopped || bOwnsInputLock)
    {
        return;
    }
    APlayerController* Player = Cast<APlayerController>(GetOwner());
    if (!Player || !Player->IsLocalController())
    {
        SetComponentTickEnabled(false);
        return;
    }
    SetComponentTickEnabled(true);
    StartedAt = FPlatformTime::Seconds();
    NextStreamingCheckAt = StartedAt;
    bLoading = true;
    bFailed = false;
    bStartedMapScan = false;
    bStreamingReady = false;
    ReadyFrames = 0;
    Progress = 0;
    ISceneEntryProvider* Host = Cast<ISceneEntryProvider>(Player);
    if (!Host)
    {
        SetComponentTickEnabled(false);
        return;
    }
    Host->SetEntryMapScanEnabled(false);
    if (bStopped || !IsValid(Player))
    {
        return;
    }
    Player->SetIgnoreMoveInput(true);
    Player->SetIgnoreLookInput(true);
    Player->SetInputMode(FInputModeUIOnly());
    bOwnsInputLock = true;
    Host->SetEntryLoadingState(true);
}

void UWandererLoadingComponent::Stop()
{
    bStopped = true;
    SetComponentTickEnabled(false);
    ReleaseInput();
}

void UWandererLoadingComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bStopped || !bLoading || bFailed || !bOwnsInputLock)
    {
        return;
    }
    ReadyFrames = IsSceneReady() ? ReadyFrames + 1 : 0;
    if (ReadyFrames >= SceneEntrySettings::RequiredReadyFrames && FinishVisibleResourceStreaming())
    {
        CompleteEntry();
    }
    else if (FPlatformTime::Seconds() - StartedAt > SceneEntrySettings::TimeoutSeconds)
    {
        bFailed = true;
        UE_LOG(LogTemp,
            Error,
            TEXT("Wanderer LoadMap timeout; entry remains locked. Last readiness stage: %s"),
            *Status.ToString());
        Status =
            FText::FromString(TEXT("Scene preparation failed. Check the log and restart the map."));
    }
}

void UWandererLoadingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Stop();
    Super::EndPlay(EndPlayReason);
}

bool UWandererLoadingComponent::IsSceneReady()
{
    ISceneEntryProvider* Host = Cast<ISceneEntryProvider>(GetOwner());
    if (!Host || !Host->AreEntryCharactersReady())
    {
        return false;
    }
    Progress = SceneEntrySettings::CharacterProgress;
    Status = FText::FromString(TEXT("Loading scene and character assets…"));
    if (!AreWorldResourcesReady())
    {
        return false;
    }
    return UpdateStreamingReadiness() && UpdateMapReadiness();
}

bool UWandererLoadingComponent::AreWorldResourcesReady()
{
    if (IsAsyncLoading())
    {
        return false;
    }
    if (UPCGSubsystem* ProceduralScene = GetWorld()->GetSubsystem<UPCGSubsystem>())
    {
        if (ProceduralScene->IsAnyGraphCurrentlyExecuting())
        {
            return false;
        }
    }
    for (const ULevelStreaming* Level : GetWorld()->GetStreamingLevels())
    {
        if (!Level)
        {
            continue;
        }
        if (Level->ShouldBeLoaded() && !Level->IsLevelLoaded())
        {
            return false;
        }
        if (Level->ShouldBeVisible() && !Level->IsLevelVisible())
        {
            return false;
        }
    }
    if (UWorldPartitionSubsystem* Partition = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>())
    {
        if (!Partition->IsAllStreamingCompleted())
        {
            return false;
        }
    }
#if WITH_EDITOR
    FAssetCompilingManager::Get().ProcessAsyncTasks(true);
    if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0)
    {
        return false;
    }
#endif
    return true;
}

bool UWandererLoadingComponent::UpdateStreamingReadiness()
{
    Progress = SceneEntrySettings::StreamingProgress;
    Status = FText::FromString(TEXT("Streaming visible textures…"));
    const double Now = FPlatformTime::Seconds();
    if (Now >= NextStreamingCheckAt)
    {
        // Pump current-view requests with a bounded I/O wait, respecting texture-pool budgets.
        bStreamingReady = FinishVisibleResourceStreaming();
        NextStreamingCheckAt = Now + SceneEntrySettings::StreamingCheckSeconds;
    }
    if (!bStreamingReady)
    {
        return false;
    }
    if (FShaderPipelineCache::NumPrecompilesRemaining() > 0
        || PipelineStateCache::NumActivePrecacheRequests() > 0)
    {
        return false;
    }
    return true;
}

bool UWandererLoadingComponent::UpdateMapReadiness()
{
    ISceneEntryProvider* Host = Cast<ISceneEntryProvider>(GetOwner());
    if (!Host)
    {
        return false;
    }
    if (!bStartedMapScan)
    {
        Host->StartEntryMapScan();
        if (bStopped || !IsValid(GetOwner()))
        {
            return false;
        }
        bStartedMapScan = true;
    }
    Host = Cast<ISceneEntryProvider>(GetOwner());
    if (bStopped || !Host)
    {
        return false;
    }
    Progress = SceneEntrySettings::MapProgress;
    Status = FText::FromString(TEXT("Drawing connected paths…"));
    if (!Host->IsEntryMapReady())
    {
        return false;
    }
    Progress = SceneEntrySettings::FinishingProgress;
    Status = FText::FromString(TEXT("Finishing scene preparation…"));
    return true;
}

void UWandererLoadingComponent::CompleteEntry()
{
    ISceneEntryProvider* Host = Cast<ISceneEntryProvider>(GetOwner());
    bLoading = false;
    const bool bAccepted = Host && Host->SetEntryLoadingState(false);
    if (bStopped || !IsValid(GetOwner()))
    {
        return;
    }
    if (!bAccepted)
    {
        bLoading = true;
        return;
    }
    Progress = 1.0f;
    ReleaseInput();
    SetComponentTickEnabled(false);
    UE_LOG(
        LogTemp, Display, TEXT("Wanderer scene entry ready: characters, streaming and map scan."));
}

bool UWandererLoadingComponent::FinishVisibleResourceStreaming()
{
    APlayerController* Player = Cast<APlayerController>(GetOwner());
    if (!Player || !Player->PlayerCameraManager)
    {
        return false;
    }
    FVector Location;
    FRotator Rotation;
    Player->GetPlayerViewPoint(Location, Rotation);
    int32 Width = 0;
    int32 Height = 0;
    Player->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0)
    {
        return false;
    }
    const float HalfFieldOfView =
        FMath::DegreesToRadians(Player->PlayerCameraManager->GetFOVAngle() * 0.5f);
    IStreamingManager::Get().AddViewInformation(Location,
        Width,
        Width / FMath::Max(SceneEntrySettings::MinimumViewTangent, FMath::Tan(HalfFieldOfView)));
    // The time limit bounds only I/O waiting; render command flushes may take longer.
    return IStreamingManager::Get().StreamAllResources(SceneEntrySettings::StreamingWaitSeconds)
           == 0;
}

void UWandererLoadingComponent::ReleaseInput()
{
    if (bOwnsInputLock)
    {
        if (APlayerController* Player = Cast<APlayerController>(GetOwner()))
        {
            Player->SetIgnoreMoveInput(false);
            Player->SetIgnoreLookInput(false);
        }
        bOwnsInputLock = false;
    }
}
