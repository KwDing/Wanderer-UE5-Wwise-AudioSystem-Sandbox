#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "WandererNaturalInteractionComponent.generated.h"

class UMaterialParameterCollection;

/** Local visual interaction only; does not change movement, weather, or audio state. */
UCLASS()
class WANDERER_API UWandererNaturalInteractionComponent : public UActorComponent
{

    GENERATED_BODY()

  public:
    UWandererNaturalInteractionComponent();

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type InEndPlayReason) override;
    virtual void TickComponent(float InDeltaTime,
        ELevelTick InTickType,
        FActorComponentTickFunction* InTickFunction) override;

  private:
    UPROPERTY(EditDefaultsOnly, Category = "Interaction")
    TSoftObjectPtr<UMaterialParameterCollection> GrassParameterAsset =
        TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT(
            "/Game/Materials/Collections/Vegetation/MPC_Foliage_Reactive.MPC_Foliage_Reactive")));

    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> GrassParameters;
    UPROPERTY(Transient)
    TObjectPtr<UActorComponent> SnowDisplacementComponent;

    void InitializeSnow();
    bool CanInteract() const;

    bool bStopped = true;
};
