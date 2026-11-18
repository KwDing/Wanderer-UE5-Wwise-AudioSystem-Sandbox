#include "Features/AudioManager/InteriorFitAudioSyncService.h"

#include "Algo/Count.h"
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

    using FSourceDigests = TMap<TWeakObjectPtr<AActor>, FString>;

    struct FInteriorFitSyncSnapshot
    {
        FSourceDigests Digests;
        TMap<FString, FGeometryGroup> Groups;
        TMap<FString, TArray<FString>> Signatures;
    };

    constexpr double InteriorFitFailedRetryIntervalSec = 5.0;

    FString AddPair(FAudioGeometryPlan& InOutPlan, AActor* InSource,
                    UWandererAudioSourceComponent* InAudio, const FString& InKey, const FString& InDigest)
    {
        if (InAudio && !InAudio->bGeometryAutoSync)
        {
            return FString();
        }
        if (InDigest.IsEmpty() && InAudio)
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
        Item.Metadata.Digest = InDigest;
        const bool bHasEvent
            = InAudio && InAudio->GetProfile() && !InAudio->GetProfile()->PlayEvent.IsNull();
        FAudioGeometryPlanBuilder::SetStatus(
            Item.Metadata, bHasEvent ? TEXT("IF 已同步") : TEXT("IF 已同步 · 待配置 Event"),
            false);
        if (InAudio && InAudio->GeometryImportSource == Item.SourcePath
            && InAudio->GeometryImportDigest == InDigest)
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
                          const FSourceDigests& InDigests, FAudioGeometryPlan& InOutPlan)
    {
        for (int32 Index = InOutGroup.Sources.Num() - 1; Index >= 0; --Index)
        {
            AActor* Source = InOutGroup.Sources[Index].Get();
            if (!IsValid(Source))
            {
                return TEXT("IF 来源在配对时失效。");
            }
            const FString Path = Source->GetPathName();
            const FString Digest = InDigests.FindRef(Source);
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
            if (Matches[0]->GeometryImportSource != Path)
            {
                const int32 EqualSources = Algo::CountIf(InOutGroup.Sources,
                    [&InDigests, &Digest](const TWeakObjectPtr<AActor>& Candidate)
                    {
                        return InDigests.FindRef(Candidate) == Digest;
                    });
                if (EqualSources != 1)
                {
                    continue;
                }
            }
            const FString Error = AddPair(InOutPlan, Source, Matches[0].Get(), InKey, Digest);
            if (!Error.IsEmpty())
            {
                return Error;
            }
            InOutGroup.Targets.Remove(Matches[0]);
            InOutGroup.Sources.RemoveAt(Index);
        }
        return FString();
    }

    FString BuildGroup(FGeometryGroup InGroup, const FString& InKey,
        const FSourceDigests& InDigests, FAudioGeometryPlan& InOutPlan)
    {
        const int32 SourceCount = InGroup.Sources.Num();
        const int32 TargetCount = InGroup.Targets.Num();
        const FString Error = MatchExisting(InGroup, InKey, InDigests, InOutPlan);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        if (InGroup.Sources.Num() == 1 && InGroup.Targets.Num() == 1 && SourceCount == 1
            && TargetCount == 1)
        {
            return AddPair(InOutPlan, InGroup.Sources[0].Get(), InGroup.Targets[0].Get(), InKey,
                InDigests.FindRef(InGroup.Sources[0]));
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
                const FString PairError = AddPair(InOutPlan, Source.Get(), nullptr, InKey, InDigests.FindRef(Source));
                if (!PairError.IsEmpty())
                {
                    return PairError;
                }
            }
        }
        return FString();
    }

    FInteriorFitSyncSnapshot Collect(UWorld& InWorld, const FSourceDigests* InConfirmedDigests = nullptr)
    {
        FInteriorFitSyncSnapshot Snapshot;
        TMap<FString, FString> SourceKeys;
        TArray<UWandererAudioSourceComponent*> Targets;
        for (TActorIterator<AActor> It(&InWorld); It; ++It)
        {
            AActor* Actor = *It;
            if (FLevelUtils::IsLevelLocked(Actor->GetLevel()))
            {
                continue;
            }
            if (FInteriorFitGeometryImportOperation::IsSource(Actor))
            {
                const FString Path = Actor->GetPathName();
                const FString Key = FInteriorFitGeometryImportOperation::GetSourceGroup(*Actor);
                const FString* Cached = InConfirmedDigests ? InConfirmedDigests->Find(Actor) : nullptr;
                const FString Digest = Cached ? *Cached : FInteriorFitGeometryImportOperation::Fingerprint(Actor);
                Snapshot.Digests.Add(Actor, Digest);
                Snapshot.Groups.FindOrAdd(Key).Sources.Add(Actor);
                Snapshot.Signatures.FindOrAdd(Key).Add(TEXT("Source:") + Path + TEXT(":") + Digest);
                SourceKeys.Add(Path, Key);
            }
            else if (auto* Audio = Actor->FindComponentByClass<UWandererAudioSourceComponent>();
                Audio && !Audio->GeometryImportSource.IsEmpty())
            {
                Targets.Add(Audio);
            }
        }
        for (UWandererAudioSourceComponent* Audio : Targets)
        {
            const FAudioGeometryMetadata Metadata = AudioGeometryUtils::ReadMetadata(*Audio);
            const FString* CurrentKey = SourceKeys.Find(Metadata.Source);
            const FString Key = CurrentKey ? *CurrentKey
                : Metadata.Group.IsEmpty() ? TEXT("LegacyMissing:") + Metadata.Source : Metadata.Group;
            const auto* Profile = Audio->GetProfile();
            Snapshot.Groups.FindOrAdd(Key).Targets.Add(Audio);
            Snapshot.Signatures.FindOrAdd(Key).Add(FString::Printf(
                TEXT("Target:%s|%s|%s|%s|%s|%d%d%d%d%d"),
                *Audio->GetPathName(), *Metadata.Source, *Metadata.Digest, *Metadata.Group,
                *Metadata.Status, Metadata.bEnabled, Metadata.bAutoSync, Metadata.bSuspended,
                Metadata.bWasEnabled, Profile && !Profile->PlayEvent.IsNull()));
        }
        for (auto& Pair : Snapshot.Signatures)
        {
            Pair.Value.Sort();
        }
        return Snapshot;
    }

    void ReconcileInteriorFitGroups(const FAudioGeometryPlan& InContext,
        const FInteriorFitSyncSnapshot& InSnapshot, TFunctionRef<bool(const FString&)> InShouldApply,
        TSet<FString>& OutSucceeded)
    {
        if (!FAudioGeometryPlanBuilder::CanContinue(InContext) || bReconciling
            || GEditor->IsTransactionActive())
        {
            return;
        }
        const TGuardValue<bool> Guard(bReconciling, true);
        FString FailureSummary;
        for (const auto& Pair : InSnapshot.Groups)
        {
            if (!InShouldApply(Pair.Key))
            {
                continue;
            }
            FAudioGeometryPlan Plan;
            Plan.WorldWeakPtr = InContext.WorldWeakPtr;
            Plan.CanContinue = InContext.CanContinue;
            if (!FAudioGeometryPlanBuilder::CanContinue(Plan))
            {
                return;
            }
            const FString Error = BuildGroup(Pair.Value, Pair.Key, InSnapshot.Digests, Plan);
            if (Plan.Items.ContainsByPredicate([](const FAudioGeometryPlanItem& InItem)
                {
                    return InItem.SourceWeakPtr.IsValid() && InItem.SourceDigest != InItem.Metadata.Digest;
                }))
            {
                FailureSummary = TEXT("IF 来源在确认后的读取阶段发生变化，等待重新采样。");
                continue;
            }
            if (!Error.IsEmpty())
            {
                FailureSummary = Error;
                Plan.Items.Reset();
                for (const auto& Target : Pair.Value.Targets)
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
            if (Result.Errors.IsEmpty() && (Error.IsEmpty() || !Pair.Value.Targets.IsEmpty()))
            {
                OutSucceeded.Add(Pair.Key);
            }
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
        }
        if (!FailureSummary.IsEmpty() && FailureSummary != LastFailure)
        {
            UE_LOG(LogWandererAuthoring, Warning, TEXT("IF audio sync: %s"), *FailureSummary);
        }
        LastFailure = FailureSummary;
    }
} // namespace

void FInteriorFitAudioSyncService::Reconcile(UWorld* InWorld, TFunction<bool()> InCanContinue)
{
    FAudioGeometryPlan Context;
    Context.WorldWeakPtr = InWorld;
    Context.CanContinue = MoveTemp(InCanContinue);
    if (!FAudioGeometryPlanBuilder::CanContinue(Context) || bReconciling
        || GEditor->IsTransactionActive())
    {
        return;
    }
    TSet<FString> Succeeded;
    ReconcileInteriorFitGroups(Context, Collect(*InWorld), [](const FString&) { return true; }, Succeeded);
}

void FInteriorFitAudioSyncService::Sample(UWorld& InWorld, double InNowSec)
{
#if WITH_DEV_AUTOMATION_TESTS
    ++SampleCount;
#endif
    const FInteriorFitSyncSnapshot Snapshot = Collect(InWorld);
    if (!bHasSample || !LastSample.OrderIndependentCompareEqual(Snapshot.Signatures))
    {
        LastSample = Snapshot.Signatures;
        bHasSample = true;
        bNeedsConfirmation = true;
        RetryAtSec = 0;
        return;
    }
    bNeedsConfirmation = false;
    for (auto It = AppliedGroups.CreateIterator(); It; ++It)
    {
        if (!Snapshot.Signatures.Contains(It.Key()))
        {
            It.RemoveCurrent();
        }
    }
    TSet<FString> Pending;
    for (const auto& Pair : Snapshot.Signatures)
    {
        const auto* Applied = AppliedGroups.Find(Pair.Key);
        if (!Applied || *Applied != Pair.Value)
        {
            Pending.Add(Pair.Key);
        }
    }
    if (Pending.IsEmpty())
    {
        RetryAtSec = 0;
        return;
    }
    if (RetryAtSec > InNowSec)
    {
        return;
    }

#if WITH_DEV_AUTOMATION_TESTS
    ++ReconcileCount;
    ReconciledGroupCount += Pending.Num();
#endif
    FAudioGeometryPlan Context;
    Context.WorldWeakPtr = &InWorld;
    const uint64 ActiveGeneration = Generation;
    Context.CanContinue = [WeakService = TWeakPtr<FInteriorFitAudioSyncService>(AsShared()), ActiveGeneration]
    {
        const auto Service = WeakService.Pin();
        return Service && !Service->bStopped && !Service->bDirty
            && Service->Generation == ActiveGeneration;
    };
    const TGuardValue<bool> Applying(bApplying, true);
    TSet<FString> Succeeded;
    const uint64 BeforeRevision = SyncRevision;
    ReconcileInteriorFitGroups(Context, Snapshot,
        [&Pending](const FString& InKey) { return Pending.Contains(InKey); }, Succeeded);
    if (!FAudioGeometryPlanBuilder::CanContinue(Context))
    {
        // A stale or cancelled pass must be sampled again.
        if (!bStopped && Generation == ActiveGeneration)
        {
            bNeedsConfirmation = true;
            bHasSample = false;
        }
        return;
    }
    // Cache the metadata written by this pass without hashing source geometry again.
    LastSample = SyncRevision == BeforeRevision ? Snapshot.Signatures : Collect(InWorld, &Snapshot.Digests).Signatures;
    for (const FString& Key : Succeeded)
    {
        if (const auto* Signature = LastSample.Find(Key))
        {
            AppliedGroups.Add(Key, *Signature);
        }
    }
    RetryAtSec = Succeeded.Num() == Pending.Num() ? 0 : InNowSec + InteriorFitFailedRetryIntervalSec;
}

uint64 FInteriorFitAudioSyncService::GetRevision()
{
    return SyncRevision;
}
