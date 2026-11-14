#include "Characters/WandererPlayableCharacter.h"

#include "Components/BrushComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

#include "Components/CharacterCameraZoomComponent.h"
#include "Components/WandererSwimmingMovementComponent.h"

void AWandererPlayableCharacter::StartRunning()
{
    GetCharacterMovement()->MaxWalkSpeed = RunningSpeed;
    GetCharacterMovement()->MaxSwimSpeed = RunningSpeed;
}

void AWandererPlayableCharacter::StopRunning()
{
    GetCharacterMovement()->MaxWalkSpeed = WalkingSpeed;
    GetCharacterMovement()->MaxSwimSpeed = WalkingSpeed;
}

void AWandererPlayableCharacter::ZoomCamera(float WheelDelta)
{
    RefreshCameraZoomConfig();
    CameraZoom->ApplyWheelInput(WheelDelta);
}

float AWandererPlayableCharacter::GetCameraZoomTarget() const
{
    return CameraZoom->GetTargetDistance();
}

void AWandererPlayableCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // Sprint is the existing project action mapped to Left Shift in DefaultInput.ini.
    // These UObject-bound delegates belong to the pawn's input component.
    PlayerInputComponent->BindAction(
        TEXT("Sprint"), IE_Pressed, this, &AWandererPlayableCharacter::StartRunning);
    PlayerInputComponent->BindAction(
        TEXT("Sprint"), IE_Released, this, &AWandererPlayableCharacter::StopRunning);
    PlayerInputComponent->BindAxisKey(
        EKeys::MouseWheelAxis, this, &AWandererPlayableCharacter::ZoomCamera);
}

void AWandererPlayableCharacter::UpdateSwimmingInput()
{
    const APlayerController* PlayerController = Cast<APlayerController>(GetController());
    if (!IsLocallyControlled() || !PlayerController || PlayerController->IsMoveInputIgnored()
        || !GetCharacterMovement()->IsSwimming())
    {
        return;
    }
    const float Up = PlayerController->IsInputKeyDown(EKeys::SpaceBar) ? 1.0f : 0.0f;
    const float Down = PlayerController->IsInputKeyDown(EKeys::LeftControl) ? 1.0f : 0.0f;
    AddMovementInput(FVector::UpVector, Up - Down);
}

void AWandererPlayableCharacter::UpdateSwimmingSurface(float DeltaSeconds)
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (!Movement || !Movement->IsSwimming() || Cast<UWandererSwimmingMovementComponent>(Movement))
    {
        return;
    }
    APhysicsVolume* Water = GetPhysicsVolume();
    if (!Water || !Water->bWaterVolume || !Water->ActorHasTag(TEXT("WaterBody")))
    {
        Water = nullptr;
        for (TActorIterator<APhysicsVolume> Candidate(GetWorld()); Candidate; ++Candidate)
        {
            if (Candidate->bWaterVolume && Candidate->ActorHasTag(TEXT("WaterBody"))
                && Candidate->EncompassesPoint(GetActorLocation()))
            {
                Water = *Candidate;
                break;
            }
        }
    }
    if (!Water || !Water->GetBrushComponent() || !GetMesh()->DoesSocketExist(TEXT("head")))
    {
        return;
    }
    const FVector Acceleration = Movement->GetCurrentAcceleration();
    if (Acceleration.Z < -UE_KINDA_SMALL_NUMBER)
    {
        return;
    }
    const float SurfaceHeight = Water->GetBrushComponent()->Bounds.GetBox().Max.Z;
    const float CurrentHeight = GetActorLocation().Z;
    const float HeadHeight = GetMesh()->GetSocketLocation(TEXT("head")).Z;
    const float DesiredClearance = Acceleration.SizeSquared2D() >= 1.0f ? 12.0f : 26.0f;
    const float TargetHeight = FMath::Min(
        CurrentHeight + SurfaceHeight + DesiredClearance - HeadHeight, SurfaceHeight - 8.0f);
    const float InterpolatedHeight =
        FMath::FInterpTo(CurrentHeight, TargetHeight, DeltaSeconds, 8.0f);
    const float Change = FMath::Clamp(
        InterpolatedHeight - CurrentHeight, -90.0f * DeltaSeconds, 300.0f * DeltaSeconds);
    FHitResult Hit;
    SetActorLocation(GetActorLocation() + FVector(0.0f, 0.0f, Change), true, &Hit);
    Movement->Velocity.Z = 0.0f;
    if (Acceleration.SizeSquared2D() < 1.0f)
    {
        Movement->Velocity.X = 0.0f;
        Movement->Velocity.Y = 0.0f;
    }
}

void AWandererPlayableCharacter::InitializeCameraZoom()
{
    const FCharacterCameraConfig Config{
        MinimumCameraDistance, MaximumCameraDistance, CameraZoomStep, CameraZoomSpeed};
    CameraZoom->Initialize(GetCameraBoom(), Config);
    const FCharacterCameraConfig& Sanitized = CameraZoom->GetConfig();
    MinimumCameraDistance = Sanitized.MinimumDistance;
    MaximumCameraDistance = Sanitized.MaximumDistance;
    CameraZoomStep = Sanitized.ZoomStep;
    CameraZoomSpeed = Sanitized.InterpolationSpeed;
}

void AWandererPlayableCharacter::RefreshCameraZoomConfig()
{
    CameraZoom->SetConfig(
        {MinimumCameraDistance, MaximumCameraDistance, CameraZoomStep, CameraZoomSpeed});
}

void AWandererPlayableCharacter::PrepareForMenu()
{
    StopRunning();
    Super::PrepareForMenu();
}
