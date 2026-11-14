// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/WandererCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

#include "Components/WandererSwimmingMovementComponent.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AWandererCharacter

AWandererCharacter::AWandererCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UWandererSwimmingMovementComponent>(
          ACharacter::CharacterMovementComponentName))
{
    // Set size for collision capsule
    GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

    // Don't rotate when the controller rotates. Let that just affect the camera.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    // Configure character movement
    GetCharacterMovement()->bOrientRotationToMovement =
        true; // Character moves in the direction of input...
    GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

    // Note: For faster iteration times these variables, and many more, can be tweaked in the
    // Character Blueprint instead of recompiling to adjust them
    GetCharacterMovement()->JumpZVelocity = 700.f;
    GetCharacterMovement()->AirControl = 0.35f;
    GetCharacterMovement()->MaxWalkSpeed = 500.f;
    GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
    GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

    // Create a camera boom (pulls in towards the player if there is a collision)
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength =
        400.0f; // The camera follows at this distance behind the character
    CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

    // Create a follow camera
    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom,
        USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the
                                          // boom adjust to match the controller orientation
    FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

    // Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from
    // Character) are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct
    // content references in C++)
}

void AWandererCharacter::Move(const FInputActionValue& Value)
{
    // input is a Vector2D
    FVector2D MovementVector = Value.Get<FVector2D>();

    if (!Controller)
    {
        return;
    }
    // find out which way is forward
    const FRotator Rotation = Controller->GetControlRotation();
    const FRotator YawRotation(0.0f, Rotation.Yaw, 0);

    // get forward vector
    const FVector ForwardDirection = GetCharacterMovement()->IsSwimming()
                                         ? Rotation.Vector()
                                         : FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

    // get right vector
    const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

    // add movement
    AddMovementInput(ForwardDirection, MovementVector.Y);
    AddMovementInput(RightDirection, MovementVector.X);
}

void AWandererCharacter::Look(const FInputActionValue& Value)
{
    // input is a Vector2D
    FVector2D LookAxisVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        // add yaw and pitch input to controller
        AddControllerYawInput(LookAxisVector.X);
        AddControllerPitchInput(LookAxisVector.Y);
    }
}

void AWandererCharacter::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();

    // Add Input Mapping Context
    if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
                    PlayerController->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
            }
        }
    }
}

void AWandererCharacter::UnPossessed()
{
    if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
                    PlayerController->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->RemoveMappingContext(DefaultMappingContext);
            }
        }
    }
    Super::UnPossessed();
}

void AWandererCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    UEnhancedInputComponent* EnhancedInputComponent =
        Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EnhancedInputComponent)
    {
        UE_LOG(LogTemplateCharacter,
            Error,
            TEXT("'%s' requires an Enhanced Input component."),
            *GetNameSafe(this));
        return;
    }
    // Jumping
    EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
    EnhancedInputComponent->BindAction(
        JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

    // Moving
    EnhancedInputComponent->BindAction(
        MoveAction, ETriggerEvent::Triggered, this, &AWandererCharacter::Move);

    // Looking
    EnhancedInputComponent->BindAction(
        LookAction, ETriggerEvent::Triggered, this, &AWandererCharacter::Look);
}

UCameraComponent* AWandererCharacter::GetCharacterCamera() const
{
    return GetFollowCamera();
}
USkeletalMeshComponent* AWandererCharacter::GetPresentationMesh() const
{
    return GetMesh();
}
const TArray<FWandererAppearancePart>& AWandererCharacter::GetAppearanceParts() const
{
    static const TArray<FWandererAppearancePart> Empty;
    return Empty;
}
void AWandererCharacter::PrepareForMenu()
{
    GetCharacterMovement()->StopMovementImmediately();
}
