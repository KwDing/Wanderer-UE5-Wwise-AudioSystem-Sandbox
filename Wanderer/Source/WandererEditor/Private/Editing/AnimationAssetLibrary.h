#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimationAssetLibrary.generated.h"

class UBlendSpace;
class USkeletalMesh;

/**
 * Game-thread editor mutations; callers own transactions and saving. Legacy pin names are
 * retained.
 */
UCLASS()
class UAnimationAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool RebuildBlendSpace(UBlendSpace* BlendSpace);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ReplaceRetargetSourceMesh(
        USkeletalMesh* PreviousMesh, USkeletalMesh* ReplacementMesh);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool SetAssetPreviewMesh(UObject* Asset, USkeletalMesh* PreviewMesh);
};
