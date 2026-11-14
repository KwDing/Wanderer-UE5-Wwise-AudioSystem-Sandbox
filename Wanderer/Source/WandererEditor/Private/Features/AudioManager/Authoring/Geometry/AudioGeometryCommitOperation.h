#pragma once

#include "Features/AudioManager/Authoring/Geometry/AudioGeometryTypes.h"

/** One import batch or pairing group owns one transaction; compensates only journaled writes. */
class FAudioGeometryCommitOperation
{
  public:
    explicit FAudioGeometryCommitOperation(const FAudioGeometryPlan& InPlan);
    void Execute(FAudioSourceResult& InOutResult);
    AActor* GetCreatedActor() const;

  private:
    struct FChange
    {
        FAudioGeometryPlanItem Item;
        TWeakObjectPtr<AActor> ActorWeakPtr;
        TWeakObjectPtr<ULevel> LevelWeakPtr;
        TWeakObjectPtr<USceneComponent> RootWeakPtr;
        TWeakObjectPtr<UWandererAudioSourceComponent> AudioWeakPtr;
        FDataTableRowHandle WrittenProfile;
        bool bGeometryWritten = false;
        bool bMetadataWritten = false;
    };
    struct FCreatedRow
    {
        FName Name;
        FWandererWorldAudioProfileRow Profile;
    };

    const FAudioGeometryPlan& Plan;
    TArray<FChange> Changes;
    TArray<FCreatedRow> CreatedRows;
    FString Failure;

    bool CommitItem(const FAudioGeometryPlanItem& InItem);
    bool CompleteItem(FChange& InOutChange);
    bool PrepareObjects(FChange& InOutChange);
    bool MatchesBeforeMetadata(const FChange& InChange) const;
    AActor* Spawn(const FAudioGeometryPlanItem& InItem);
    bool CommitProfile(FChange& InOutChange);
    bool ValidateCommitted() const;
    bool Rollback(FAudioSourceResult& InOutResult);
    void Restore(FChange& InOutChange, FAudioSourceResult& InOutResult);
    bool CanRestore(const FChange& InChange) const;
    void RemoveRows(FAudioSourceResult& InOutResult);
};
