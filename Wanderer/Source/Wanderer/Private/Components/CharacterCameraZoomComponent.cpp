#include "Components/CharacterCameraZoomComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

void UCharacterCameraZoomComponent::Initialize(
    USpringArmComponent* InArm, const FCharacterCameraConfig& InConfig)
{
    Stop();
    if (!IsValid(InArm) || InArm->GetOwner() != GetOwner())
    {
        return;
    }
    ArmWeakPtr = InArm;
    Config = InConfig;
    Config.MinimumDistance =
        FMath::Max(CharacterPresentationSettings::MinimumCameraDistance, Config.MinimumDistance);
    Config.MaximumDistance = FMath::Max(Config.MinimumDistance, Config.MaximumDistance);
    Config.ZoomStep = FMath::Max(CharacterPresentationSettings::MinimumZoomStep, Config.ZoomStep);
    Config.InterpolationSpeed =
        FMath::Max(CharacterPresentationSettings::MinimumZoomSpeed, Config.InterpolationSpeed);
    TargetDistance =
        FMath::Clamp(InArm->TargetArmLength, Config.MinimumDistance, Config.MaximumDistance);
    InArm->TargetArmLength = TargetDistance;
}

void UCharacterCameraZoomComponent::SetConfig(const FCharacterCameraConfig& InConfig)
{
    Config = InConfig;
}

void UCharacterCameraZoomComponent::ApplyWheelInput(float InDelta)
{
    const APawn* Pawn = Cast<APawn>(GetOwner());
    const APlayerController* Player =
        Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (!ArmWeakPtr.IsValid() || !IsValid(Pawn) || !Player || !Player->IsLocalController()
        || Player->IsLookInputIgnored() || !GetWorld() || GetWorld()->IsPaused()
        || !FMath::IsFinite(InDelta) || FMath::IsNearlyZero(InDelta))
    {
        return;
    }
    TargetDistance = FMath::Clamp(
        TargetDistance - InDelta * Config.ZoomStep, Config.MinimumDistance, Config.MaximumDistance);
}

void UCharacterCameraZoomComponent::Update(float InDeltaSeconds)
{
    const APawn* Pawn = Cast<APawn>(GetOwner());
    USpringArmComponent* Arm = ArmWeakPtr.Get();
    if (!Arm || !IsValid(Pawn) || !Pawn->IsLocallyControlled())
    {
        return;
    }
    Arm->TargetArmLength = FMath::FInterpTo(
        Arm->TargetArmLength, TargetDistance, InDeltaSeconds, Config.InterpolationSpeed);
}

void UCharacterCameraZoomComponent::Stop()
{
    ArmWeakPtr.Reset();
}

float UCharacterCameraZoomComponent::GetTargetDistance() const
{
    return TargetDistance;
}

const FCharacterCameraConfig& UCharacterCameraZoomComponent::GetConfig() const
{
    return Config;
}

void UCharacterCameraZoomComponent::EndPlay(const EEndPlayReason::Type InReason)
{
    Stop();
    Super::EndPlay(InReason);
}
