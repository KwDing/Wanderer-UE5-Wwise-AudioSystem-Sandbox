#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"

#include "InteriorFitArea.h"
#include "InteriorFitLibrary.h"
#include "InteriorFitSplineGuide.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryCommitOperation.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryPlanBuilder.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryUtils.h"

namespace
{
    FAudioSourceResult Commit(FAudioGeometryPlan& InPlan, const FString& InError)
    {
        FAudioSourceResult Result;
        if (!InError.IsEmpty())
        {
            Result.Errors.Add(InError);
            return Result;
        }
        FAudioGeometryCommitOperation Operation(InPlan);
        Operation.Execute(Result);
        return Result;
    }
} // namespace

FAudioSourceResult FInteriorFitGeometryImportOperation::Create(const FAudioSourceRequest& InRequest)
{
    // Copy the UI draft before synchronous loads can reenter its owner.
    const FAudioSourceRequest Request = InRequest;
    FAudioGeometryPlan Plan;
    const FString Error = FAudioGeometryPlanBuilder::Import(Request, Plan);
    FAudioSourceResult Result = Commit(Plan, Error);
    if (Result.Applied > 0)
    {
        // Line previews are editor display state and should not reappear when audio is undone.
        ClearImportedPreview(Plan, Result);
    }
    return Result;
}

FAudioSourceResult FInteriorFitGeometryImportOperation::Update(AActor* InSource, AActor* InTarget,
                                                               EGeometryImportTransform InPolicy,
                                                               TFunction<bool()> InCanContinue)
{
    FAudioGeometryPlan Plan;
    Plan.WorldWeakPtr = IsValid(InSource) ? InSource->GetWorld() : nullptr;
    Plan.CanContinue = MoveTemp(InCanContinue);
    const FString Error
        = !IsValid(InTarget)
              ? TEXT("请选择有效的目标声源。")
              : FAudioGeometryPlanBuilder::Add(Plan, InSource, InTarget,
                                               InPolicy == EGeometryImportTransform::KeepTarget);
    return Commit(Plan, Error);
}

FString FInteriorFitGeometryImportOperation::Fingerprint(AActor* InSource)
{
    if (!IsSource(InSource))
    {
        return FString();
    }
    FAudioGeometrySnapshot Geometry;
    return AudioGeometryUtils::Read(InSource, Geometry).IsEmpty()
               ? AudioGeometryUtils::Digest(Geometry)
               : FString();
}

FString FInteriorFitGeometryImportOperation::GetSourceGroup(const AActor& InSource)
{
    const FString Kind = InSource.IsA<AInteriorFitArea>() ? TEXT("Area:") : TEXT("Spline:");
    const FString Prefix = InSource.GetLevel()->GetPathName() + TEXT(":") + Kind;
    for (const FName Tag : InSource.Tags)
    {
        const FString Name = Tag.ToString();
        if (Name.StartsWith(TEXT("InteriorFitProof_")))
        {
            return Prefix + Name;
        }
    }
    return Prefix + InSource.GetActorGuid().ToString();
}

AActor* FInteriorFitGeometryImportOperation::CreatePlaceholder(AActor* InSource)
{
    FAudioGeometryPlan Plan;
    Plan.WorldWeakPtr = IsValid(InSource) ? InSource->GetWorld() : nullptr;
    if (!FAudioGeometryPlanBuilder::Add(Plan, InSource, nullptr).IsEmpty())
    {
        return nullptr;
    }
    FAudioSourceResult Result;
    FAudioGeometryCommitOperation Operation(Plan);
    Operation.Execute(Result);
    return Result.Applied == 1 ? Operation.GetCreatedActor() : nullptr;
}

bool FInteriorFitGeometryImportOperation::IsSource(const AActor* InActor)
{
    if (!IsValid(InActor) || InActor->IsActorBeingDestroyed()
        || InActor->FindComponentByClass<UWandererAudioSourceComponent>())
    {
        return false;
    }
    return InActor->IsA<AInteriorFitArea>() || InActor->IsA<AInteriorFitSplineGuide>();
}

void FInteriorFitGeometryImportOperation::ClearImportedPreview(
    const FAudioGeometryPlan& InPlan, FAudioSourceResult& InOutResult)
{
    UWorld* World = InPlan.WorldWeakPtr.Get();
    if (!World || !GEditor || GEditor->PlayWorld || World->bIsTearingDown
        || World != GEditor->GetEditorWorldContext().World())
    {
        InOutResult.Errors.Add(TEXT("声源已接管；编辑世界已切换，未清理 IF 预览。"));
        return;
    }

    const bool bHasArea = InPlan.Items.ContainsByPredicate(
        [](const FAudioGeometryPlanItem& InItem) { return InItem.Geometry.bArea; });
    const bool bHasSpline = InPlan.Items.ContainsByPredicate(
        [](const FAudioGeometryPlanItem& InItem) { return !InItem.Geometry.bArea; });
    const bool bAreaCleared = !bHasArea || UInteriorFitLibrary::DrawAreaPreview(World, FString());
    const bool bSplineCleared = !bHasSpline || UInteriorFitLibrary::DrawSplinePreviewWithStyle(
        World, {}, EInteriorFitSplineClosure::Open, EInteriorFitSplineBudgetStatus::WithinBudget);
    if (!bAreaCleared || !bSplineCleared)
    {
        InOutResult.Errors.Add(TEXT("声源已接管；部分 IF 预览未清除，可用插件的清除预览。"));
    }
}
