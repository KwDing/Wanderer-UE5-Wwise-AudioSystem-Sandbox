#pragma once

#include "Components/ActorComponent.h"

#include "Data/CharacterPresentationData.h"

#include "CharacterCameraZoomComponent.generated.h"

class USpringArmComponent;

/** Pawn-owned zoom state. The actor drives updates; the arm is only observed. */
UCLASS()
class UCharacterCameraZoomComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    void Initialize(USpringArmComponent* InArm, const FCharacterCameraConfig& InConfig);
    void SetConfig(const FCharacterCameraConfig& InConfig);
    void ApplyWheelInput(float InDelta);
    void Update(float InDeltaSeconds);
    void Stop();
    float GetTargetDistance() const;
    const FCharacterCameraConfig& GetConfig() const;

  protected:
    virtual void EndPlay(const EEndPlayReason::Type InReason) override;

  private:
    TWeakObjectPtr<USpringArmComponent> ArmWeakPtr;
    FCharacterCameraConfig Config;
    float TargetDistance = CharacterPresentationSettings::InitialCameraDistance;
};
