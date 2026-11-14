#pragma once

#include "Features/AudioManager/Authoring/Sources/AudioSourceAuthoringTypes.h"

/** Reads scene/configuration and pins loaded events; never creates actors or modifies a table. */
class FAudioSourceEditPlanBuilder
{
public:
    static bool Build(const FAudioSourceRequest& InRequest,
        FAudioSourcePlan& OutPlan, FAudioSourceResult& InOutResult);
    static bool CanContinue(const FAudioSourceRequest& InRequest);
    static FString ValidateItem(const FAudioSourceRequest& InRequest,
        const FAudioSourcePlanItem& InItem,
        const UWandererAudioSourceComponent* InCreatedSource = nullptr);
    static TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> GatherSources(
        UWorld* InWorld, const TArray<TWeakObjectPtr<AActor>>& InTargets);
    static FWandererWorldAudioProfileRow MergeProfile(
        const FWandererWorldAudioProfileRow& InOriginal, const FAudioSourceRequest& InRequest);

private:
    static void CollectCreateItems(const FAudioSourceRequest& InRequest,
        FAudioSourcePlan& InOutPlan, FAudioSourceResult& InOutResult);
    static void CollectEditItems(const FAudioSourceRequest& InRequest,
        FAudioSourcePlan& InOutPlan, FAudioSourceResult& InOutResult);
    static void AddValidatedItem(const FAudioSourceRequest& InRequest,
        FAudioSourcePlanItem InItem, FAudioSourcePlan& InOutPlan, FAudioSourceResult& InOutResult);
    static FString LoadProfileEvents(const FWandererWorldAudioProfileRow& InRow,
        FAudioSourcePlan& InOutPlan);
    static FString AssignProfileNames(const FAudioSourceRequest& InRequest,
        FAudioSourcePlan& InOutPlan);
    static FString FindAvailableProfileName(const UDataTable& InTable, FName InBase,
        int32& InOutSuffix, FName& OutName);
    static FString ValidateRoom(AAkSpatialAudioVolume* InRoom);
    static FString FindRoomGroup(AAkSpatialAudioVolume* InRoom, FName& OutGroup);
    static FString ValidateCreate(const FAudioSourcePlanItem& InItem,
        const UWandererAudioSourceComponent* InCreatedSource);
    static FString ValidateEdit(const FAudioSourcePlanItem& InItem);
    static void GatherRoomSources(AAkSpatialAudioVolume& InRoom,
        TArray<TWeakObjectPtr<UWandererAudioSourceComponent>>& InOutSources);
};
