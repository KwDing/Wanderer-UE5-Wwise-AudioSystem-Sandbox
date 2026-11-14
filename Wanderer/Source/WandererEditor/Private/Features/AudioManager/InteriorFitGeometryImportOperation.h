#pragma once

#include "CoreMinimal.h"

#include "Features/AudioManager/AudioSourceAuthoringOperation.h"

struct FAudioGeometryPlan;

enum class EGeometryImportTransform : uint8
{
    AdoptSource,
    KeepTarget
};

/** Game-thread editor facade; source geometry is copied, never retained by runtime emitters. */
class FInteriorFitGeometryImportOperation
{
  public:
    static FAudioSourceResult Create(const FAudioSourceRequest& InRequest);
    static FAudioSourceResult Update(AActor* InSource, AActor* InTarget,
                                     EGeometryImportTransform InPolicy,
                                     TFunction<bool()> InCanContinue = {});
    static FString Fingerprint(AActor* InSource);
    static FString GetSourceGroup(const AActor& InSource);
    static AActor* CreatePlaceholder(AActor* InSource);
    static bool IsSource(const AActor* InActor);
    /** Clear successful handoff previews in the current editor world. */
    static void ClearImportedPreview(const FAudioGeometryPlan& InPlan,
                                     FAudioSourceResult& InOutResult);
};
