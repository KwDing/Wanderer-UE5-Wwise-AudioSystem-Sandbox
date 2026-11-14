#pragma once

#include "Features/AudioManager/Authoring/Geometry/AudioGeometryTypes.h"

/** Read-only scene preflight. Asset loads may reenter the editor; no transaction is started here.
 */
class FAudioGeometryPlanBuilder
{
  public:
    static bool CanContinue(const FAudioGeometryPlan& InPlan);
    static FString Add(FAudioGeometryPlan& InOutPlan, AActor* InSource, AActor* InTarget,
                       bool bInKeepTransform = false);
    static FString Import(const FAudioSourceRequest& InRequest, FAudioGeometryPlan& OutPlan);
    static FString Validate(const FAudioGeometryPlan& InPlan, const FAudioGeometryPlanItem& InItem,
                            bool bInCheckTarget);
    static FString Suspend(FAudioGeometryPlan& InOutPlan, UWandererAudioSourceComponent& InAudio,
                           const FString& InStatus);
    static void SetStatus(FAudioGeometryMetadata& InOutMetadata, const FString& InStatus,
                          bool bInSuspend);

  private:
    static FString ReadTarget(FAudioGeometryPlanItem& InOutItem, AActor& InTarget);
};
