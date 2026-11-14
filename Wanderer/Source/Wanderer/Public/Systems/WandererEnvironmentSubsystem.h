#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Interfaces/EnvironmentStateInterface.h"

#include "WandererEnvironmentSubsystem.generated.h"

/**
 * World-local day/night authority, queried through EnvironmentStateInterface by audio.
 * Game-thread only; no clock, lighting, weather simulation or cross-world persistence.
 */
UCLASS()
class WANDERER_API UWandererEnvironmentSubsystem : public UWorldSubsystem,
                                                   public IEnvironmentStateInterface
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintPure,
        Category = "Wanderer|Environment",
        meta = (WorldContext = "WorldContextObject"))
    static UWandererEnvironmentSubsystem* GetEnvironmentSubsystem(
        const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Environment")
    void SetNight(bool bInNight);

    UFUNCTION(BlueprintPure, Category = "Wanderer|Environment")
    virtual bool IsNight() const override;

    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type InWorldType) const override;

  private:
    UPROPERTY(Transient)
    bool bNight = false;

    bool bStopped = false;
};
