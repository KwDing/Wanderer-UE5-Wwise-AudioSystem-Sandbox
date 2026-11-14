#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"

#include "Features/AudioManager/Authoring/Sources/AudioSourceAuthoringTypes.h"

struct FAudioGeometrySnapshot
{
    FTransform Transform;
    TArray<FSplinePoint> Points;
    TArray<FVector> Cells;
    FVector UpVector = FVector::UpVector;
    float CellSize = 100.0f;
    float Above = 200.0f;
    float Below = 500.0f;
    bool bArea = false;
    bool bClosed = false;
    bool bStationary = false;
    bool bSplineEdited = true;
    TOptional<uint8> AudioShape;
};

struct FAudioGeometryMetadata
{
    FString Source;
    FString Digest;
    FString Group;
    FString Status;
    FGuid Batch;
    bool bEnabled = true;
    bool bAutoSync = true;
    bool bSuspended = false;
    bool bWasEnabled = true;
};

struct FAudioGeometryPlanItem
{
    TWeakObjectPtr<AActor> SourceWeakPtr;
    TWeakObjectPtr<AActor> TargetWeakPtr;
    TWeakObjectPtr<UWandererAudioSourceComponent> AudioWeakPtr;
    TWeakObjectPtr<ULevel> TargetLevelWeakPtr;
    FString SourceDigest;
    FString SourcePath;
    FAudioGeometrySnapshot Geometry;
    FAudioGeometrySnapshot Previous;
    FAudioGeometryMetadata Metadata;
    FAudioGeometryMetadata PreviousMetadata;
    FWandererWorldAudioProfileRow Profile;
    FDataTableRowHandle PreviousProfile;
    bool bCreate = false;
    bool bWriteGeometry = true;
    bool bWriteProfile = false;
};

/** Scoped immutable plan: assets are pinned, scene identities remain weak and revalidated. */
struct FAudioGeometryPlan
{
    TWeakObjectPtr<UWorld> WorldWeakPtr;
    TArray<FAudioGeometryPlanItem> Items;
    TStrongObjectPtr<UDataTable> Table;
    TArray<TStrongObjectPtr<UAkAudioEvent>> Events;
    FName ResourceGroup;
    FName ProfileRowName;
    TFunction<bool()> CanContinue;
};
