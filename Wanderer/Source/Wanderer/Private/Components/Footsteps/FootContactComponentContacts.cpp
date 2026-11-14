#include "Components/Footsteps/FootContactComponent.h"

#include "AnimationRuntime.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Interfaces/LocomotionAudioReceiver.h"
#include "Systems/Audio/WandererAudioDiagnostics.h"

namespace
{
    constexpr float StepSpeedMinCmPerSec = 10.0f;
    constexpr float StepSpeedSquaredMin = FMath::Square(StepSpeedMinCmPerSec);
    constexpr float ReleaseGapMinCm = 1.0f;
    constexpr double ContactRejectionLogIntervalSec = 1.0;
}

void UFootContactComponent::ResetFootstepCadence()
{
    LeftFootContact.Reset();
    RightFootContact.Reset();
    bWasFalling = false;
    bJumpAudioPending = false;
    bHasSwimSample = false;
    bSwimStrokeArmed = false;
}

void UFootContactComponent::RequestJumpStartAudio()
{
    if (bStopped)
    {
        return;
    }
    bJumpAudioPending = true;
}

void UFootContactComponent::StartAirborneContacts()
{
    if (bStopped || bWasFalling)
    {
        return;
    }
    bWasFalling = true;
    LeftFootContact.bArmed = false;
    RightFootContact.bArmed = false;
    LeftFootContact.ContactSince = -1.0;
    RightFootContact.ContactSince = -1.0;
    LeftFootContact.bLandingPending = true;
    RightFootContact.bLandingPending = true;
    LeftFootContact.bHasLanded = false;
    RightFootContact.bHasLanded = false;
}

void UFootContactComponent::BeginLandingContacts()
{
    if (bStopped)
    {
        return;
    }
    bWasFalling = false;
    // A foot may already have reached support before capsule landing was reported.
    if (!LeftFootContact.bHasLanded)
    {
        LeftFootContact.bLandingPending = true;
        LeftFootContact.bArmed = true;
    }
    if (!RightFootContact.bHasLanded)
    {
        RightFootContact.bLandingPending = true;
        RightFootContact.bArmed = true;
    }
}

FWandererFootContactDebug UFootContactComponent::GetFootContactDebug(FName FootBone) const
{
    FWandererFootContactDebug Result;
    const FFootContactState* State = FootBone == Settings.LeftFootBone    ? &LeftFootContact
                                     : FootBone == Settings.RightFootBone ? &RightFootContact
                                                                          : nullptr;
    if (!State)
    {
        return Result;
    }
    Result.bValid = true;
    Result.bArmed = State->bArmed;
    Result.bLandingPending = State->bLandingPending;
    Result.SoleOffset = State->SoleOffset;
    Result.LastStepTime = State->LastStepTime;
    return Result;
}

bool UFootContactComponent::IsFootReadyForTrace(const ACharacter& Character,
    FName FootBone,
    FFootContactState& State,
    float PoseHeight,
    float Release)
{
    const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
    if (State.bLandingPending && Movement->IsMovingOnGround()
        && Character.GetVelocity().SizeSquared2D() > StepSpeedSquaredMin
        && PoseHeight >= Release)
    {
        // Once grounded locomotion lifts this foot into a new swing, do not keep
        // imposing the old jump's strict landing clearance on every later step.
        State.bLandingPending = false;
        State.bHasLanded = true;
    }
    const bool bLanding = State.bLandingPending;
    // Terrain height must not manufacture an animation lift or consume a step early.
    if (!bLanding && PoseHeight >= Release)
    {
        State.bArmed = true;
        State.ContactSince = -1.0;
        return false;
    }
    // Once contact begins, retain it through the low stance until the release
    // height. A one-frame crossing of Settings.ContactHeight must survive confirmation.
    const float PoseContactLimit = State.ContactSince >= 0.0 ? Release : Settings.ContactHeight;
    if (!bLanding && (!State.bArmed || PoseHeight > PoseContactLimit))
    {
        ReportContactRejection(FootBone,
            State,
            State.bArmed ? TEXT("PoseAboveContact") : TEXT("WaitingForLift"),
            PoseHeight);
        if (CanContinue(&Character))
        {
            State.ContactSince = -1.0;
        }
        return false;
    }
    return true;
}

void UFootContactComponent::UpdateFootContact(
    ACharacter& Character, FName FootBone, FFootContactState& State, float StepGateSec)
{
    USkeletalMeshComponent* Mesh = Character.GetMesh();
    if (!Mesh || !GetWorld())
    {
        return;
    }
    const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
    const FVector Foot = Mesh->GetSocketLocation(FootBone);
    const float CapsuleBottom = Character.GetActorLocation().Z
                                - Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float PoseHeight = Foot.Z - CapsuleBottom - State.SoleOffset;
    const float Release =
        FMath::Max(Settings.ReleaseHeight, Settings.ContactHeight + ReleaseGapMinCm);
    if (!IsFootReadyForTrace(Character, FootBone, State, PoseHeight, Release))
    {
        return;
    }
    const bool bLanding = State.bLandingPending;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WandererFootContact), true, &Character);
    Query.bReturnPhysicalMaterial = true;
    Query.bReturnFaceIndex = true;
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit,
        Foot + FVector(0, 0, Settings.GroundTraceHeight),
        Foot - FVector(0, 0, Settings.GroundTraceDepth),
        ECC_Visibility,
        Query);
    if (!bHit || !Movement->IsWalkable(Hit))
    {
        ReportContactRejection(
            FootBone, State, bHit ? TEXT("SupportNotWalkable") : TEXT("NoFootSupport"), PoseHeight);
        if (!CanContinue(&Character))
        {
            return;
        }
        if (bLanding)
        {
            State.bArmed = true;
        }
        State.ContactSince = -1.0;
        return;
    }
    // CharacterMovement intentionally keeps a small capsule/floor gap while grounded.
    const float SupportGap =
        Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor()
            ? FMath::Max(0.0f, Movement->CurrentFloor.GetDistanceToFloor())
            : 0.0f;
    const float Height = Foot.Z - Hit.ImpactPoint.Z - State.SoleOffset - SupportGap;
    if (bLanding && Height >= Release)
    {
        State.bArmed = true;
        State.ContactSince = -1.0;
        return;
    }
    // An ascending jump can still have an animated foot near support. Keep its
    // landing pending until descent rather than consuming it during takeoff.
    const bool bAscending = bLanding && Movement->IsFalling() && Movement->Velocity.Z > 0.0f;
    const bool bContact =
        State.bArmed && !bAscending
        && Height <= (bLanding ? Settings.ContactHeight
                               : GetGroundSupportTolerance(*Movement, Hit, CapsuleBottom));
    if (!bContact)
    {
        ReportContactRejection(FootBone, State, TEXT("SupportHeightOrAirborne"), Height);
    }
    if (!CanContinue(&Character)
        || !ConfirmFootContact(State, bContact, GetWorld()->GetTimeSeconds()))
    {
        return;
    }
    State.bLandingPending = false;
    if (bLanding)
    {
        State.bHasLanded = true;
    }
    PlayFootContact(Character, FootBone, Hit, bLanding, StepGateSec);
}

float UFootContactComponent::GetGroundSupportTolerance(
    const UCharacterMovementComponent& Movement, const FHitResult& Hit, float CapsuleBottom) const
{
    const float BaseTolerance = FMath::Max(0.0f, Settings.GroundSupportTolerance);
    if (!Movement.IsMovingOnGround() || !Movement.CurrentFloor.IsWalkableFloor())
    {
        return BaseTolerance;
    }
    // Without foot IK, the capsule follows the higher tread while a planted pose
    // can project onto an adjacent lower one. This is support tolerance, not a new
    // contact edge: the pose must still lift/rearm and confirm exactly once.
    // A capsule perched on a tread corner has a low impact point even though its
    // bottom has already risen. Use the movement-supported sole plane instead.
    const float SupportPlane =
        CapsuleBottom - FMath::Max(0.0f, Movement.CurrentFloor.GetDistanceToFloor());
    const float LowerTreadHeight = SupportPlane - Hit.ImpactPoint.Z;
    return BaseTolerance
           + FMath::Clamp(LowerTreadHeight, 0.0f, FMath::Max(0.0f, Movement.MaxStepHeight));
}

bool UFootContactComponent::ConfirmFootContact(FFootContactState& State, bool bContact, double Now)
{
    if (!bContact)
    {
        State.ContactSince = -1.0;
        return false;
    }
    if (State.ContactSince < 0.0)
    {
        State.ContactSince = Now;
    }
    if (Now - State.ContactSince < FMath::Max(0.0f, Settings.ContactConfirmationSeconds))
    {
        return false;
    }
    // Consume even a gated edge. A foot staying down must never retry later.
    State.bArmed = false;
    State.ContactSince = -1.0;
    return true;
}

void UFootContactComponent::ReportContactRejection(
    FName FootBone, FFootContactState& State, const TCHAR* Reason, float PoseHeight)
{
    if (!FWandererAudioDiagnostics::IsEnabled(EWandererAudioLogChannel::Footsteps) || !GetWorld())
    {
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    const AActor* Owner = GetOwner();
    if (Now < State.NextDiagnosticTime || !IsValid(Owner)
        || Owner->GetVelocity().SizeSquared2D() <= StepSpeedSquaredMin)
    {
        return;
    }
    State.NextDiagnosticTime = Now + ContactRejectionLogIntervalSec;
    FWandererAudioDiagnostics::Publish(this,
        EWandererAudioLogChannel::Footsteps,
        TEXT("ContactCheck"),
        FString::Printf(TEXT("%s: %s %s; height=%.2fcm armed=%d confirming=%d time=%.3fs"),
            *GetNameSafe(GetOwner()),
            *FootBone.ToString(),
            Reason,
            PoseHeight,
            State.bArmed,
            State.ContactSince >= 0.0,
            Now));
}

void UFootContactComponent::PlayFootContact(
    ACharacter& Character, FName FootBone, const FHitResult& Hit, bool bLanding, float StepGateSec)
{
    if (!bLanding
        && (Character.GetVelocity().SizeSquared2D() <= StepSpeedSquaredMin
            || !Character.GetCharacterMovement()->IsMovingOnGround()))
    {
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    FFootContactState& State =
        FootBone == Settings.LeftFootBone ? LeftFootContact : RightFootContact;
    // Each pending landing foot is independent; simultaneous feet must not block each other.
    if (!bLanding && State.LastStepTime >= 0.0
        && Now - State.LastStepTime < FMath::Max(0.0f, StepGateSec))
    {
        ReportContactRejection(FootBone, State, TEXT("SameFootGate"), 0.0f);
        return;
    }
    State.LastStepTime = Now;
    ++FootContactCount;
    const bool bLeftFoot = FootBone == Settings.LeftFootBone;
    if (FWandererAudioDiagnostics::IsEnabled(EWandererAudioLogChannel::Footsteps))
    {
        FWandererAudioDiagnostics::Publish(this,
            EWandererAudioLogChannel::Footsteps,
            TEXT("Contact"),
            FString::Printf(TEXT("%s: %s %s; confirm=%.3fs gate=%.3fs time=%.3fs"),
                *Character.GetName(),
                *FootBone.ToString(),
                bLanding ? TEXT("landing") : TEXT("contact"),
                Settings.ContactConfirmationSeconds,
                StepGateSec,
                Now));
    }
    if (!CanContinue(&Character))
    {
        return;
    }
    if (ILocomotionAudioReceiver* Receiver = Cast<ILocomotionAudioReceiver>(&Character))
    {
        Receiver->OnFootContact(Hit, bLeftFoot, bLanding);
    }
}

float UFootContactComponent::CalculateSoleOffset(const ACharacter& Character, FName FootBone) const
{
    const USkeletalMeshComponent* Mesh = Character.GetMesh();
    const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset)
    {
        return 0.0f;
    }
    const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
    const int32 BoneIndex = Skeleton.FindBoneIndex(FootBone);
    if (BoneIndex == INDEX_NONE)
    {
        return 0.0f;
    }
    const FTransform Reference =
        FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, BoneIndex);
    const FVector WorldFoot =
        Mesh->GetComponentTransform().TransformPosition(Reference.GetLocation());
    const float FloorHeight = Character.GetActorLocation().Z
                              - Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return FMath::Max(0.0f, static_cast<float>(WorldFoot.Z - FloorHeight));
}
