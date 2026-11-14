#include "Characters/WandererPlayableCharacter.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"
#include "Components/Footsteps/FootContactComponent.h"
#include "Components/Footsteps/WandererFootstepVFXComponent.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Interfaces/CharacterSurfaceProvider.h"

void AWandererPlayableCharacter::PlayFootstepAudio(
    const FHitResult* ContactHit, EWandererAudioPlaybackScope Scope)
{
    if (!IsLocallyControlled() || !GetCharacterMovement()->IsMovingOnGround() || !CharacterAudio)
    {
        return;
    }

    static const FName ActionID(TEXT("Footstep"));

    PlayContactAudio(ActionID, ContactHit, Scope);
}

void AWandererPlayableCharacter::PlayLandingFootAudio(
    const FHitResult& Hit, EWandererAudioPlaybackScope Scope)
{
    if (!Hit.bBlockingHit || GetCharacterMovement()->IsSwimming())
    {
        return;
    }
    static const FName ActionID(TEXT("Jump.Land"));
    PlayContactAudio(ActionID, &Hit, Scope);
}

void AWandererPlayableCharacter::PlayJumpStartAudio()
{
    static const FName ActionID(TEXT("Jump.Start"));
    PlayLocomotionAudio(ActionID);
}

void AWandererPlayableCharacter::OnFootContact(const FHitResult& Hit, bool bLeftFoot, bool bLanding)
{
    const TWeakObjectPtr<AController> ControllerWeakPtr(GetController());
    const EWandererAudioPlaybackScope Scope =
        bLeftFoot ? EWandererAudioPlaybackScope::LeftFoot : EWandererAudioPlaybackScope::RightFoot;
    if (bLanding)
    {
        PlayLandingFootAudio(Hit, Scope);
    }
    else
    {
        PlayFootstepAudio(&Hit, Scope);
    }
    if (IsValid(this) && !IsActorBeingDestroyed() && IsLocallyControlled()
        && ControllerWeakPtr.Get() == GetController() && IsValid(FootstepVFX))
    {
        FootstepVFX->EmitContact(bLeftFoot);
    }
}

void AWandererPlayableCharacter::NotifyAudioJumpStarted()
{
    if (IsLocallyControlled() && FootContacts)
    {
        FootContacts->RequestJumpStartAudio();
    }
}

void AWandererPlayableCharacter::NotifyAudioLanded(const FHitResult& Hit)
{
    if (!IsLocallyControlled() || !Hit.bBlockingHit)
    {
        return;
    }

    if (FootContacts)
    {
        FootContacts->BeginLandingContacts();
    }
}

void AWandererPlayableCharacter::NotifyAudioMovementModeChanged(EMovementMode PreviousMovementMode)
{
    if (!IsLocallyControlled())
    {
        return;
    }

    const bool bIsSwimming = GetCharacterMovement()->IsSwimming();

    // Native and legacy Blueprint notifications can both arrive for one transition.
    const bool bWasSwimming = bAudioWasSwimming;
    bAudioWasSwimming = bIsSwimming;

    if (bIsSwimming != bWasSwimming && FootContacts)
    {
        FootContacts->ResetFootstepCadence();
    }
    if (GetCharacterMovement()->IsFalling() && FootContacts)
    {
        FootContacts->StartAirborneContacts();
    }

    if (!bWasSwimming && bIsSwimming)
    {
        static const FName ActionID(TEXT("Swim.Enter"));
        PlayLocomotionAudio(ActionID);
    }
}

void AWandererPlayableCharacter::NotifyAudioSwimStroke()
{
    if (!GetCharacterMovement()->IsSwimming())
    {
        return;
    }

    static const FName ActionID(TEXT("Swim.Stroke"));
    PlayLocomotionAudio(ActionID);
}

void AWandererPlayableCharacter::OnJumped_Implementation()
{
    Super::OnJumped_Implementation();
    NotifyAudioJumpStarted();
}

void AWandererPlayableCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    NotifyAudioLanded(Hit);
}

void AWandererPlayableCharacter::OnMovementModeChanged(
    EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
    NotifyAudioMovementModeChanged(PreviousMovementMode);
}

FWandererCharacterAudioContext AWandererPlayableCharacter::MakeCharacterAudioContext(
    const FHitResult* ContactHit) const
{
    FWandererCharacterAudioContext Context;

    Context.Gait = GetCharacterMovement()->MaxWalkSpeed > (WalkingSpeed + RunningSpeed) * 0.5f
                       ? FName(TEXT("run"))
                       : FName(TEXT("walk"));

    const ICharacterSurfaceProvider* Provider = Cast<ICharacterSurfaceProvider>(GetController());
    if (!Provider)
    {
        return Context;
    }
    const FVector Feet =
        GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FCharacterSurfaceSnapshot Surface = Provider->QueryContactSurface(ContactHit, Feet);
    Context.Surface = Surface.Surface;
    Context.PhysicalSurface = Surface.PhysicalSurface;
    Context.Wetness = Surface.Wetness;

    return Context;
}

void AWandererPlayableCharacter::PlayContactAudio(
    FName ActionID, const FHitResult* Hit, EWandererAudioPlaybackScope Scope)
{
    FDiagnosticTraceScope Trace(ActionID == TEXT("Footstep"));
    if (!IsLocallyControlled() || !CharacterAudio)
    {
        return;
    }
    WANDERER_DIAGNOSTIC(this,
        EDiagnosticDomain::Events,
        TEXT("direct-audio-request"),
        false,
        TEXT("Character invokes audio component directly; not an event bus broadcast"));
    CharacterAudio->PlayAction(ActionID, MakeCharacterAudioContext(Hit), Scope);
}

void AWandererPlayableCharacter::PlayLocomotionAudio(FName InAction)
{
    if (!IsLocallyControlled() || !IsValid(CharacterAudio))
    {
        return;
    }
    FDiagnosticTraceScope Trace;
    WANDERER_DIAGNOSTIC(this,
        EDiagnosticDomain::Events,
        TEXT("direct-audio-request"),
        false,
        FString::Printf(TEXT("action=%s route=character-audio"), *InAction.ToString()));
    CharacterAudio->PlayAction(InAction, FWandererCharacterAudioContext());
}

FFootContactSettings AWandererPlayableCharacter::GetFootContactSettings() const
{
    return FootstepVFX ? FootstepVFX->GetContactSettings() : FFootContactSettings();
}
FWandererFootContactDebug AWandererPlayableCharacter::QueryFootContact(FName InBone) const
{
    return FootContacts ? FootContacts->GetFootContactDebug(InBone) : FWandererFootContactDebug();
}
int32 AWandererPlayableCharacter::GetContactCount() const
{
    return FootContacts ? FootContacts->GetFootContactCount() : 0;
}
int32 AWandererPlayableCharacter::GetStrokeCount() const
{
    return FootContacts ? FootContacts->GetSwimStrokeCount() : 0;
}
