#include "Features/AudioManager/AudioSourceAuthoringOperation.h"

#include "Editor.h"
#include "Templates/UnrealTemplate.h"

#include "AkAudioEvent.h"

#include "Features/AudioManager/Authoring/Sources/AudioSourceCommitOperation.h"
#include "Features/AudioManager/Authoring/Sources/AudioSourceEditPlanBuilder.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

namespace
{
    bool bAuthoringActive = false;
}

FString FAudioSourceResult::Summary() const
{
    return FString::Printf(TEXT("完成 %d 个，跳过 %d 个。"), Applied, Skipped)
        + (Errors.IsEmpty() ? FString() : TEXT("\n") + FString::Join(Errors, TEXT("\n")));
}

FAudioSourceResult FAudioSourceAuthoringOperation::Apply(const FAudioSourceRequest& InRequest)
{
    FAudioSourceResult Result;
    if (!IsInGameThread() || bAuthoringActive)
    {
        Result.Errors.Add(TEXT("声源编辑只能在游戏线程执行，且不能重入。"));
        return Result;
    }
    const TGuardValue<bool> ActiveGuard(bAuthoringActive, true);
    // Freeze the draft before loads or editor notifications can reenter UI code.
    const FAudioSourceRequest Request = InRequest;
    if (Request.Action == EAudioSourceAction::ImportGeometry)
    {
        return FInteriorFitGeometryImportOperation::Create(Request);
    }
    if (!GEditor || GEditor->IsTransactionActive())
    {
        Result.Errors.Add(TEXT("已有编辑事务正在执行，请完成后再编辑声源。"));
        return Result;
    }
    FAudioSourcePlan Plan;
    if (!FAudioSourceEditPlanBuilder::Build(Request, Plan, Result))
    {
        return Result;
    }
    FAudioSourceCommitOperation Commit(Request, Plan);
    Commit.Execute(Result);
    return Result;
}

TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> FAudioSourceAuthoringOperation::GatherSources(
    UWorld* InWorld, const TArray<TWeakObjectPtr<AActor>>& InTargets)
{
    return FAudioSourceEditPlanBuilder::GatherSources(InWorld, InTargets);
}

FDataTableRowHandle FAudioSourceAuthoringOperation::FindOrAddProfile(UDataTable* InTable,
    const FWandererWorldAudioProfileRow& InRow, UAkAudioEvent* InEvent, FName InRowName)
{
    FName CreatedName;
    return FWorldAudioProfileRepository::FindOrAdd(InTable, InRow, InEvent, InRowName, CreatedName);
}

FWandererWorldAudioProfileRow FAudioSourceAuthoringOperation::MergeProfile(
    const FWandererWorldAudioProfileRow& InOriginal, const FAudioSourceRequest& InRequest)
{
    return FAudioSourceEditPlanBuilder::MergeProfile(InOriginal, InRequest);
}
