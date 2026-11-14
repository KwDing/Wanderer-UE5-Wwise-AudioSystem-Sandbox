#include "Components/WandererSwimmingAnimInstance.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UWandererSwimmingAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
    const UCharacterMovementComponent* Movement =
        Character ? Character->GetCharacterMovement() : nullptr;
    bSwimmingPose = Movement && Movement->IsSwimming();
    // Passive buoyancy must not keep an otherwise stationary swimmer in the forward stroke.
    const bool bSwimmingInput = Movement && Movement->GetCurrentAcceleration().SizeSquared() > 1.0f;
    // The swim blend space's full stroke sample is at 200, regardless of actual travel speed.
    const float DesiredSpeed = bSwimmingPose && bSwimmingInput ? 200.0f : 0.0f;
    SwimmingPoseSpeed = FMath::FInterpTo(SwimmingPoseSpeed, DesiredSpeed, DeltaSeconds, 6.0f);
}

bool UWandererSwimmingAnimInstance::IsForwardSwimmingPose() const
{
    constexpr float ForwardPoseThreshold = 50.0f;
    return SwimmingPoseSpeed >= ForwardPoseThreshold;
}
