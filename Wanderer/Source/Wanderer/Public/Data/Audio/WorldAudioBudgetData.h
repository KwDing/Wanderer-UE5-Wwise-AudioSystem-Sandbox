#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "WorldAudioBudgetData.generated.h"

class AActor;
class UAkAudioEvent;

/** Shared regional policy. Map-specific bounds and spatial membership stay on the budget actor. */
USTRUCT(BlueprintType)
struct WANDERER_API FWandererAudioResourcePolicyRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources")
    bool bAlwaysResident = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance",
              meta = (ClampMin = "0", Units = "m"))
    float LoadDistanceM = 50;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance",
              meta = (ClampMin = "0", Units = "m"))
    float UnloadDistanceM = 70;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance",
              meta = (ClampMin = "0", Units = "s", DisplayName = "UnloadDelaySeconds"))
    float UnloadDelaySec = 2;
    /** 0 = no regional cap; map and profile caps still apply. Protected sources may overflow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "0"))
    int32 MaxEvents = 0;
};

/** Regional residency and soft Event quota; does not change acoustic gain or Room membership. */
USTRUCT(BlueprintType)
struct WANDERER_API FWandererAudioResourceGroup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources")
    FName Name;
    /** Optional shared policy; when set, replaces inline distance/residency/budget values at Play
     * start. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy",
              meta = (RowType = "/Script/Wanderer.WandererAudioResourcePolicyRow"))
    FDataTableRowHandle Policy;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "0"))
    int32 MaxEvents = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources")
    bool bAlwaysResident = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (Units = "cm"))
    FVector BoundsCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance",
              meta = (ClampMin = "0", Units = "cm"))
    FVector BoundsExtent = FVector(100);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance",
              meta = (ClampMin = "0", Units = "m"))
    float LoadDistanceM = 50;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance",
              meta = (ClampMin = "0", Units = "m"))
    float UnloadDistanceM = 70;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance",
              meta = (ClampMin = "0", Units = "s", DisplayName = "UnloadDelaySeconds"))
    float UnloadDelaySec = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources")
    TArray<TSoftObjectPtr<UAkAudioEvent>> PreloadEvents;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Spatial Audio",
              meta = (AllowedClasses = "/Script/AkAudio.AkSpatialAudioVolume"))
    TArray<TSoftObjectPtr<AActor>> Rooms;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Spatial Audio",
              meta = (AllowedClasses = "/Script/AkAudio.AkAcousticPortal"))
    TArray<TSoftObjectPtr<AActor>> Portals;
    /** Keep cross-group propagation dependencies resident while this group is resident. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spatial Audio")
    TArray<FName> Dependencies;

    bool ApplyPolicy(FString& OutError);

    /** Validate names, bounds and dependency references after applying each group's policy. */
    static FString ValidateLayout(TConstArrayView<FWandererAudioResourceGroup> InGroups);
};
