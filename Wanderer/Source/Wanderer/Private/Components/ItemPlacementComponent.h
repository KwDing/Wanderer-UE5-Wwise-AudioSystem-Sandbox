#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "ItemPlacementComponent.generated.h"

class AActor;

/** Reusable geometry queries. No inventory, audio, input or UI knowledge. */
UCLASS()
class UItemPlacementComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    void ConfigureQuery(AActor* InPawn, AActor* InPreview, UClass* InExcludedClass);
    bool FindSurface(FVector InStart, FVector InEnd, FHitResult& OutHit) const;
    bool IsBlocked(const FTransform& InTransform, FVector InExtent, AActor* InPreview) const;
    bool ValidateSupport(
        const FHitResult& InHit, const FTransform& InTransform, FVector InExtent) const;

private:
    UPROPERTY(Transient)
    TSubclassOf<AActor> ExcludedSurfaceClass;

    TWeakObjectPtr<AActor> PawnWeakPtr;
    TWeakObjectPtr<AActor> PreviewWeakPtr;
};
