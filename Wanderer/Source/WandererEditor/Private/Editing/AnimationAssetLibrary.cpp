#include "Editing/AnimationAssetLibrary.h"

#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"


bool UAnimationAssetLibrary::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
    if (!IsInGameThread() || !IsValid(BlendSpace))
    {
        return false;
    }
    BlendSpace->Modify();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    return true;
}

bool UAnimationAssetLibrary::ReplaceRetargetSourceMesh(
    USkeletalMesh* PreviousMesh, USkeletalMesh* ReplacementMesh)
{
    if (!IsInGameThread() || !IsValid(PreviousMesh) || !IsValid(ReplacementMesh))
    {
        return false;
    }
    USkeleton* Skeleton = PreviousMesh->GetSkeleton();
    if (!Skeleton || Skeleton != ReplacementMesh->GetSkeleton())
    {
        return false;
    }
    const FReferenceSkeleton& Previous = PreviousMesh->GetRefSkeleton();
    const FReferenceSkeleton& Replacement = ReplacementMesh->GetRefSkeleton();
    if (Previous.GetNum() != Replacement.GetNum())
    {
        return false;
    }
    for (int32 Index = 0; Index < Previous.GetNum(); ++Index)
    {
        if (Previous.GetBoneName(Index) != Replacement.GetBoneName(Index)
            || Previous.GetParentIndex(Index) != Replacement.GetParentIndex(Index)
            || !Previous.GetRefBonePose()[Index].Equals(Replacement.GetRefBonePose()[Index]))
        {
            return false;
        }
    }
    Skeleton->Modify();
    for (TPair<FName, FReferencePose>& Source : Skeleton->AnimRetargetSources)
    {
        if (Source.Value.SourceReferenceMesh.ToSoftObjectPath() == FSoftObjectPath(PreviousMesh))
        {
            // Keep the named reference pose itself unchanged; only its editor source changes.
            Source.Value.SourceReferenceMesh = ReplacementMesh;
        }
    }
    Skeleton->MarkPackageDirty();
    return true;
}

bool UAnimationAssetLibrary::SetAssetPreviewMesh(UObject* Asset, USkeletalMesh* PreviewMesh)
{
    if (!IsInGameThread() || !IsValid(Asset) || !IsValid(PreviewMesh))
    {
        return false;
    }
    if (USkeleton* Skeleton = Cast<USkeleton>(Asset))
    {
        if (PreviewMesh->GetSkeleton() != Skeleton)
        {
            return false;
        }
        Skeleton->Modify();
        Skeleton->SetPreviewMesh(PreviewMesh);
        return true;
    }
    if (UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(Asset))
    {
        PhysicsAsset->Modify();
        PhysicsAsset->SetPreviewMesh(PreviewMesh);
        return true;
    }
    return false;
}
