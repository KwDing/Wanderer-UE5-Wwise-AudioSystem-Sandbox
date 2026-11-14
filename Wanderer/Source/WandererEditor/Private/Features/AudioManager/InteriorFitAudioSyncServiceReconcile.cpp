#include "Features/AudioManager/InteriorFitAudioSyncService.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "Templates/UnrealTemplate.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryCommitOperation.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryPlanBuilder.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryUtils.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"
#include "Foundation/AuthoringLog.h"

namespace
{
    uint64 SyncRevision = 0;
    bool bReconciling = false;
    FString LastFailure;

    struct FGeometryGroup
    {
        TArray<TWeakObjectPtr<AActor>> Sources;
        TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Targets;
    };

    FString AddPair(FAudioGeometryPlan& InOutPlan, AActor* InSource,
                    UWandererAudioSourceComponent* InAudio, const FString& InKey)
    {
        if (InAudio && !InAudio->bGeometryAutoSync)
        {
            return FString();
        }
        const FString Digest = FInteriorFitGeometryImportOperation::Fingerprint(InSource);
        if (Digest.IsEmpty() && InAudio)
        {
            return FAudioGeometryPlanBuilder::Suspend(InOutPlan, *InAudio,
                                                      TEXT("IF 几何无效，已暂停"));
        }
        const FString Error = FAudioGeometryPlanBuilder::Add(
            InOutPlan, InSource, InAudio ? InAudio->GetOwner() : nullptr);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        FAudioGeometryPlanItem& Item = InOutPlan.Items.Last();
        Item.Metadata.Group = InKey;
        // Source digest drives pairing even when guide extents differ from preserved audio extents.
        Item.Metadata.Digest = Digest;
        const bool bHasEvent
            = InAudio && InAudio->GetProfile() && !InAudio->GetProfile()->PlayEvent.IsNull();
        FAudioGeometryPlanBuilder::SetStatus(
            Item.Metadata, bHasEvent ? TEXT("IF 已同步") : TEXT("IF 已同步 · 待配置 Event"),
            false);
        if (InAudio && InAudio->GeometryImportSource == Item.SourcePath
            && InAudio->GeometryImportDigest == Digest)
        {
            Item.bWriteGeometry = false;
            if (AudioGeometryUtils::Matches(Item.Metadata, Item.PreviousMetadata))
            {
                InOutPlan.Items.Pop();
            }
        }
        return FString();
    }

    FString MatchExisting(FGeometryGroup& InOutGroup, const FString& InKey,
                          FAudioGeometryPlan& InOutPlan)
    {
        for (int32 Index = InOutGroup.Sources.Num() - 1; Index >= 0; --Index)
        {
            AActor* Source = InOutGroup.Sources[Index].Get();
            if (!IsValid(Source))
            {
                return TEXT("IF 来源在配对时失效。");
            }
            const FString Path = Source->GetPathName();
            const FString Digest = FInteriorFitGeometryImportOperation::Fingerprint(Source);
            TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Matches;
            for (const TWeakObjectPtr<UWandererAudioSourceComponent>& Target : InOutGroup.Targets)
            {
                if (Target.IsValid() && Target->GeometryImportSource == Path)
                {
                    Matches.Add(Target);
                }
            }
            if (Matches.IsEmpty() && !Digest.IsEmpty())
            {
                Matches = InOutGroup.Targets.FilterByPredicate(
                    [&Digest](const auto& Target)
                    { return Target.IsValid() && Target->GeometryImportDigest == Digest; });
            }
            if (Matches.Num() != 1)
            {
                continue;
            }
            const int32 EqualSources
                = InOutGroup.Sources
                      .FilterByPredicate(
                          [&Digest](const TWeakObjectPtr<AActor>& Candidate)
                          {
                              return FInteriorFitGeometryImportOperation::Fingerprint(
                                         Candidate.Get())
                                     == Digest;
                          })
                      .Num();
            if (Matches[0]->GeometryImportSource != Path && EqualSources != 1)
            {
                continue;
            }
            const FString Error = AddPair(InOutPlan, Source, Matches[0].Get(), InKey);
            if (!Error.IsEmpty())
            {
                return Error;
            }
            InOutGroup.Targets.Remove(Matches[0]);
            InOutGroup.Sources.RemoveAt(Index);
        }
        return FString();
    }

    FString BuildGroup(FGeometryGroup InGroup, const FString& InKey, FAudioGeometryPlan& InOutPlan)
    {
        const int32 SourceCount = InGroup.Sources.Num();
        const int32 TargetCount = InGroup.Targets.Num();
        const FString Error = MatchExisting(InGroup, InKey, InOutPlan);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        if (InGroup.Sources.Num() == 1 && InGroup.Targets.Num() == 1 && SourceCount == 1
            && TargetCount == 1)
        {
            return AddPair(InOutPlan, InGroup.Sources[0].Get(), InGroup.Targets[0].Get(), InKey);
        }
        if (!InGroup.Targets.IsEmpty())
        {
            const FString Status = InGroup.Sources.IsEmpty()
                                       ? TEXT("IF 来源缺失或重复绑定，已暂停")
                                       : TEXT("IF 分支配对不唯一，已暂停；请指定来源");
            for (const TWeakObjectPtr<UWandererAudioSourceComponent>& Target : InGroup.Targets)
            {
                if (Target.IsValid() && Target->bGeometryAutoSync)
                {
                    FAudioGeometryPlanBuilder::Suspend(InOutPlan, *Target, Status);
                }
            }
            return FString();
        }
        if (TargetCount == 0)
        {
            for (const TWeakObjectPtr<AActor>& Source : InGroup.Sources)
            {
                const FString PairError = AddPair(InOutPlan, Source.Get(), nullptr, InKey);
                if (!PairError.IsEmpty())
                {
                    return PairError;
                }
            }
        }
        return FString();
    }

    TMap<FString, FGeometryGroup> Collect(UWorld& InWorld)
    {
        TMap<FString, FGeometryGroup> Groups;
        TMap<FString, FString> SourceKeys;
        for (TActorIterator<AActor> It(&InWorld); It; ++It)
        {
            if (FLevelUtils::IsLevelLocked(It->GetLevel())
                || !FInteriorFitGeometryImportOperation::IsSource(*It))
            {
                continue;
            }
            const FString Key = FInteriorFitGeometryImportOperation::GetSourceGroup(**It);
            Groups.FindOrAdd(Key).Sources.Add(*It);
            SourceKeys.Add(It->GetPathName(), Key);
        }
        for (TActorIterator<AActor> It(&InWorld); It; ++It)
        {
            UWandererAudioSourceComponent* Audio
                = It->FindComponentByClass<UWandererAudioSourceComponent>();
            if (FLevelUtils::IsLevelLocked(It->GetLevel()) || !Audio
                || Audio->GeometryImportSource.IsEmpty())
            {
                continue;
            }
            const FString* CurrentKey = SourceKeys.Find(Audio->GeometryImportSource);
            const FString Key = CurrentKey ? *CurrentKey
                                : Audio->GeometryImportGroup.IsEmpty()
                                    ? TEXT("LegacyMissing:") + Audio->GeometryImportSource
                                    : Audio->GeometryImportGroup;
            Groups.FindOrAdd(Key).Targets.Add(Audio);
        }
        return Groups;
    }
} // namespace

void FInteriorFitAudioSyncService::Reconcile(UWorld* InWorld, TFunction<bool()> InCanContinue)
{
    FAudioGeometryPlan Context;
    Context.WorldWeakPtr = InWorld;
    Context.CanContinue = InCanContinue;
    if (!FAudioGeometryPlanBuilder::CanContinue(Context) || bReconciling
        || GEditor->IsTransactionActive())
    {
        return;
    }
    const TGuardValue<bool> Guard(bReconciling, true);
    TMap<FString, FGeometryGroup> Groups = Collect(*InWorld);
    FString FailureSummary;
    for (const TPair<FString, FGeometryGroup>& Pair : Groups)
    {
        FAudioGeometryPlan Plan;
        Plan.WorldWeakPtr = Context.WorldWeakPtr;
        Plan.CanContinue = InCanContinue;
        if (!FAudioGeometryPlanBuilder::CanContinue(Plan))
        {
            return;
        }
        const FString Error = BuildGroup(Pair.Value, Pair.Key, Plan);
        if (!Error.IsEmpty())
        {
            FailureSummary = Error;
            Plan.Items.Reset();
            for (const TWeakObjectPtr<UWandererAudioSourceComponent>& Target : Pair.Value.Targets)
            {
                if (Target.IsValid() && Target->bGeometryAutoSync)
                {
                    FAudioGeometryPlanBuilder::Suspend(Plan, *Target, Error);
                }
            }
        }
        FAudioSourceResult Result;
        FAudioGeometryCommitOperation Commit(Plan);
        Commit.Execute(Result);
        if (Result.Applied > 0)
        {
            ++SyncRevision;
            if (Pair.Value.Targets.IsEmpty())
            {
                FInteriorFitGeometryImportOperation::ClearImportedPreview(Plan, Result);
            }
        }
        if (!Result.Errors.IsEmpty())
        {
            FailureSummary = Result.Summary();
        }
        // Rejected groups remain eligible on the next stable poll; never cache a failed commit.
    }
    if (!FailureSummary.IsEmpty() && FailureSummary != LastFailure)
    {
        UE_LOG(LogWandererAuthoring, Warning, TEXT("IF audio sync: %s"), *FailureSummary);
    }
    LastFailure = FailureSummary;
}

uint64 FInteriorFitAudioSyncService::GetRevision()
{
    return SyncRevision;
}
