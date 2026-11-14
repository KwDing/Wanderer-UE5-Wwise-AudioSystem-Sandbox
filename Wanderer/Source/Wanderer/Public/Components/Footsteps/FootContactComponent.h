#pragma once
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "Types/FootContactTypes.h"

#include "FootContactComponent.generated.h"
class ACharacter;
class UCharacterMovementComponent;
class USkeletalMeshComponent;

/** Pose/contact edge detector; the owner decides how contact is presented. */
UCLASS()
class WANDERER_API UFootContactComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    UFootContactComponent();
    void ResetFootstepCadence();
    void RequestJumpStartAudio();
    void StartAirborneContacts();
    void BeginLandingContacts();
    void Stop();
    FWandererFootContactDebug GetFootContactDebug(FName InBone) const;
    int32 GetFootContactCount() const
    {
        return FootContactCount;
    }
    int32 GetSwimStrokeCount() const
    {
        return SwimStrokeCount;
    }

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type InReason) override;
    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

  private:
    friend class FFootContactGateTest;
    struct FFootContactState
    {
        bool bArmed = false;
        bool bLandingPending = false;
        bool bHasLanded = false;
        float SoleOffset = 0.0f;
        double ContactSince = -1.0;
        double LastStepTime = -1.0;
        double NextDiagnosticTime = 0.0;
        void Reset();
    };

    bool IsFootReadyForTrace(const ACharacter& Character,
        FName FootBone,
        FFootContactState& State,
        float PoseHeight,
        float Release);
    void UpdateFootContact(
        ACharacter& Character, FName FootBone, FFootContactState& State, float StepGateSec);
    float GetGroundSupportTolerance(const UCharacterMovementComponent& Movement,
        const FHitResult& Hit,
        float CapsuleBottom) const;
    bool ConfirmFootContact(FFootContactState& State, bool bContact, double Now);
    void ReportContactRejection(
        FName FootBone, FFootContactState& State, const TCHAR* Reason, float PoseHeight);
    void PlayFootContact(ACharacter& Character,
        FName FootBone,
        const FHitResult& Hit,
        bool bLanding,
        float StepGateSec);
    void UpdateSwimmingStroke(ACharacter& Character, float DeltaSeconds);
    float CalculateSoleOffset(const ACharacter& Character, FName FootBone) const;
    FFootContactState LeftFootContact;
    FFootContactState RightFootContact;
    int32 FootContactCount = 0;
    bool bWasFalling = false;
    bool bJumpAudioPending = false;
    float PreviousSwimHandForward = 0.0f;
    int32 SwimStrokeCount = 0;
    bool bHasSwimSample = false;
    bool bWasForwardSwimmingPose = false;
    bool bSwimStrokeArmed = false;
    bool bWarnedMissingSwimHands = false;

    FFootContactSettings Settings;
    bool bStopped = false;
    TWeakObjectPtr<USkeletalMeshComponent> MeshWeakPtr;
    TWeakObjectPtr<UCharacterMovementComponent> MovementWeakPtr;
    void RefreshSettings();
    bool CanContinue(const ACharacter* InCharacter) const;
};
