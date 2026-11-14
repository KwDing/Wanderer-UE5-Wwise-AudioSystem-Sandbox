#include "Components/Footsteps/FootContactComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Interfaces/LocomotionAudioReceiver.h"
#include "Interfaces/SwimmingPoseProvider.h"
#include "Systems/Audio/WandererAudioDiagnostics.h"

namespace
{
    constexpr float ForwardAccelerationSquaredMin = 1.0f;
}

void UFootContactComponent::UpdateSwimmingStroke(ACharacter& Character, float DeltaSeconds)
{
    USkeletalMeshComponent* Mesh = Character.GetMesh();
    if (!Mesh)
    {
        bHasSwimSample = false;
        bSwimStrokeArmed = false;
        return;
    }
    const ISwimmingPoseProvider* Animation = Cast<ISwimmingPoseProvider>(Mesh->GetAnimInstance());
    const bool bForwardPose =
        Animation
            ? Animation->IsForwardSwimmingPose()
            : Character.GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D()
                  > ForwardAccelerationSquaredMin;
    if (bHasSwimSample && bForwardPose != bWasForwardSwimmingPose)
    {
        // A blend between poses is not itself a new hand pull.
        bHasSwimSample = false;
        bSwimStrokeArmed = false;
    }
    bWasForwardSwimmingPose = bForwardPose;
    if (!Mesh->DoesSocketExist(TEXT("hand_l")) || !Mesh->DoesSocketExist(TEXT("hand_r")))
    {
        if (!bWarnedMissingSwimHands)
        {
            bWarnedMissingSwimHands = true;
            UE_LOG(LogWandererAudio,
                Warning,
                TEXT("%s: Swim.Stroke disabled; hand_l/hand_r bones are required."),
                *GetNameSafe(GetOwner()));
        }
        return;
    }
    const FVector Hands =
        (Mesh->GetSocketLocation(TEXT("hand_l")) + Mesh->GetSocketLocation(TEXT("hand_r"))) * 0.5;
    const float Forward = FVector::DotProduct(
        Hands - Character.GetActorLocation(), Character.GetActorForwardVector());
    const float HalfHeight = Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float ReachRatio =
        bForwardPose ? Settings.SwimStrokeReachRatio : Settings.TreadingReachRatio;
    const float Speed = bHasSwimSample && DeltaSeconds > UE_SMALL_NUMBER
                            ? (Forward - PreviousSwimHandForward) / DeltaSeconds
                            : 0.0f;
    PreviousSwimHandForward = Forward;
    bHasSwimSample = true;
    // Only a new forward recovery can rearm: the pull begins while hands are still near full reach.
    const float HandSpeedMinCmPerSec = FMath::Max(Settings.MinimumSwimHandSpeed, UE_SMALL_NUMBER);
    if (Forward >= ReachRatio * HalfHeight && Speed >= HandSpeedMinCmPerSec)
    {
        bSwimStrokeArmed = true;
    }
    if (!bSwimStrokeArmed || Speed > -HandSpeedMinCmPerSec)
    {
        return;
    }
    bSwimStrokeArmed = false;
    ++SwimStrokeCount;
    if (FWandererAudioDiagnostics::IsEnabled(EWandererAudioLogChannel::Actions))
    {
        FWandererAudioDiagnostics::Publish(this,
            EWandererAudioLogChannel::Actions,
            TEXT("SwimPhase"),
            FString::Printf(TEXT("%s: Swim.Stroke %s hand pull; forward=%.1fcm speed=%.1fcm/s"),
                *Character.GetName(),
                bForwardPose ? TEXT("forward") : TEXT("treading"),
                Forward,
                Speed));
    }
    if (!CanContinue(&Character))
    {
        return;
    }
    if (ILocomotionAudioReceiver* Receiver = Cast<ILocomotionAudioReceiver>(&Character))
    {
        Receiver->NotifyAudioSwimStroke();
    }
}
