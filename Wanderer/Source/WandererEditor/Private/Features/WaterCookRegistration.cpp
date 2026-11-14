#include "GameDelegates.h"

#include "Foundation/EditorFeatureRegistration.h"
#include "Components/SurfaceInteraction/WandererWaterInteractionComponent.h"

namespace
{
    FDelegateHandle CookHandle;

    void AppendWaterPackages(TConstArrayView<const ITargetPlatform*> InPlatforms,
        TArray<FName>& InOutPackages,
        TArray<FName>& InOutNeverCookPackages)
    {
        GetDefault<UWandererWaterInteractionComponent>()->AppendWaterPackagesToCook(InOutPackages);
    }

    void StartWaterCooking()
    {
        CookHandle = FGameDelegates::Get().GetModifyCookDelegate().AddStatic(&AppendWaterPackages);
    }

    void StopWaterCooking()
    {
        FGameDelegates::Get().GetModifyCookDelegate().Remove(CookHandle);
        CookHandle.Reset();
    }

    FEditorFeatureRegistration Registration(
        TEXT("WaterCooking"), &StartWaterCooking, &StopWaterCooking, true);
} // namespace
