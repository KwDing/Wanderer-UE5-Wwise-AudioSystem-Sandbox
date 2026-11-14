#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

#include "Data/Audio/WandererWorldAudioTypes.h"

class AAkSpatialAudioVolume;
class UAkAudioEvent;
class UWandererAudioSourceComponent;

enum class EAudioSourceAction : uint8
{
    Create,
    Edit,
    ImportGeometry
};

struct FAudioSourceRequest
{
    EAudioSourceAction Action = EAudioSourceAction::Create;
    TWeakObjectPtr<UWorld> World;
    TArray<TWeakObjectPtr<AActor>> Targets;
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Sources;
    FSoftObjectPath TablePath;
    FWandererWorldAudioProfileRow Profile;
    TSet<FName> ChangedFields;
    bool bApplyAll = false;
    FName ResourceGroup;
    FName ProfileRowName;
    /** Optional game-thread liveness query; must not mutate the authoring request. */
    TFunction<bool()> CanContinue;
};

struct FAudioSourceResult
{
    int32 Applied = 0;
    int32 Skipped = 0;
    TArray<FString> Errors;
    FString Summary() const;
};


struct FAudioSourcePlanItem
{
    TWeakObjectPtr<AActor> TargetWeakPtr;
    TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr;
    TWeakObjectPtr<AAkSpatialAudioVolume> RoomWeakPtr;
    FWandererWorldAudioProfileRow Profile;
    TOptional<FWandererWorldAudioProfileRow> OriginalProfile;
    FDataTableRowHandle OriginalHandle;
    FName ResourceGroup;
    FName ProfileRowName;
    FString Label;
    bool bReuseExistingSource = false;
};

struct FAudioSourcePlan
{
    TArray<FAudioSourcePlanItem> Items;
    TStrongObjectPtr<UDataTable> Table;
    TArray<TStrongObjectPtr<UAkAudioEvent>> LoadedEvents;
};
