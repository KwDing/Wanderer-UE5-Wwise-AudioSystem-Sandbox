#include "Components/Footsteps/FootContactComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Interfaces/FootContactHost.h"
#include "Interfaces/LocomotionAudioReceiver.h"

namespace
{
    constexpr float ContactSampleRateHz = 60.0f;
}

UFootContactComponent::UFootContactComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 1.0f / ContactSampleRateHz;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}
void UFootContactComponent::Stop()
{
    bStopped = true;
    SetComponentTickEnabled(false);
    if (MeshWeakPtr.IsValid())
    {
        RemoveTickPrerequisiteComponent(MeshWeakPtr.Get());
    }
    if (MovementWeakPtr.IsValid())
    {
        RemoveTickPrerequisiteComponent(MovementWeakPtr.Get());
    }
    MeshWeakPtr.Reset();
    MovementWeakPtr.Reset();
    ResetFootstepCadence();
}
void UFootContactComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshSettings();
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
    if (!Mesh || !Mesh->DoesSocketExist(Settings.LeftFootBone)
        || !Mesh->DoesSocketExist(Settings.RightFootBone))
    {
        UE_LOG(LogWandererAudio,
            Warning,
            TEXT("%s: foot contacts disabled; missing foot bones."),
            *GetNameSafe(GetOwner()));
        Stop();
        return;
    }
    MeshWeakPtr = Mesh;
    MovementWeakPtr = Character->GetCharacterMovement();
    AddTickPrerequisiteComponent(Mesh);
    AddTickPrerequisiteComponent(MovementWeakPtr.Get());
    LeftFootContact.SoleOffset = CalculateSoleOffset(*Character, Settings.LeftFootBone);
    RightFootContact.SoleOffset = CalculateSoleOffset(*Character, Settings.RightFootBone);
}
void UFootContactComponent::EndPlay(const EEndPlayReason::Type InReason)
{
    Stop();
    Super::EndPlay(InReason);
}
void UFootContactComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!CanContinue(Character))
    {
        ResetFootstepCadence();
        return;
    }
    RefreshSettings();
    const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
    if (Movement && Movement->IsSwimming())
    {
        UpdateSwimmingStroke(*Character, DeltaTime);
        return;
    }
    bHasSwimSample = false;
    bSwimStrokeArmed = false;
    if (!Movement || (!Movement->IsMovingOnGround() && !Movement->IsFalling()))
    {
        ResetFootstepCadence();
        return;
    }
    if (Movement->IsFalling() && !bWasFalling)
    {
        StartAirborneContacts();
    }
    bWasFalling = Movement->IsFalling();
    const float StepGateSec = Character->GetVelocity().Size2D() >= Settings.RunningSpeedThreshold
                                  ? Settings.RunningGateSeconds
                                  : Settings.WalkingGateSeconds;
    UpdateFootContact(*Character, Settings.LeftFootBone, LeftFootContact, StepGateSec);
    if (!CanContinue(Character))
    {
        return;
    }
    UpdateFootContact(*Character, Settings.RightFootBone, RightFootContact, StepGateSec);
    if (!CanContinue(Character))
    {
        return;
    }
    if (bJumpAudioPending && LeftFootContact.bArmed && RightFootContact.bArmed)
    {
        bJumpAudioPending = false;
        if (ILocomotionAudioReceiver* Receiver = Cast<ILocomotionAudioReceiver>(Character))
        {
            Receiver->PlayJumpStartAudio();
        }
    }
}
void UFootContactComponent::RefreshSettings()
{
    if (const IFootContactHost* Host = Cast<IFootContactHost>(GetOwner()))
    {
        Settings = Host->GetFootContactSettings();
    }
}

bool UFootContactComponent::CanContinue(const ACharacter* InCharacter) const
{
    return !bStopped && IsValid(this) && IsValid(InCharacter) && GetOwner() == InCharacter
           && !InCharacter->IsActorBeingDestroyed() && InCharacter->IsLocallyControlled()
           && IsValid(GetWorld()) && !GetWorld()->bIsTearingDown
           && GetWorld() == InCharacter->GetWorld();
}

void UFootContactComponent::FFootContactState::Reset()
{
    bArmed = false;
    bLandingPending = false;
    bHasLanded = false;
    ContactSince = -1.0;
    LastStepTime = -1.0;
    NextDiagnosticTime = 0.0;
}
