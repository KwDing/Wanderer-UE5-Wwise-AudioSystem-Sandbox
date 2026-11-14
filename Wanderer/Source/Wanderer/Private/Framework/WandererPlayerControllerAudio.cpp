#include "Framework/WandererPlayerController.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

#include "AkAudioEvent.h"

#include "Interfaces/SwimmingStateProvider.h"
#include "Systems/Audio/ListenerAudioManager.h"

namespace
{
    constexpr float WaterSampleIntervalSec = 0.1f;
} // namespace

void AWandererPlayerController::GetAudioListenerPosition(
    FVector& OutLocation, FVector& OutFrontDir, FVector& OutRightDir) const
{
    Super::GetAudioListenerPosition(OutLocation, OutFrontDir, OutRightDir);
    const APawn* CurrentPawn = GetPawn();
    if (IsValid(CurrentPawn) && CurrentPawn->GetWorld() == GetWorld() && IsLocalController())
    {
        // Camera orbit must not move the acoustic listener through doors or away from nearby water.
        OutLocation = CurrentPawn->GetPawnViewLocation();
        const ACharacter* ListenerCharacter = Cast<ACharacter>(CurrentPawn);
        if (ListenerCharacter && ListenerCharacter->GetMesh()
            && ListenerCharacter->GetMesh()->DoesSocketExist(TEXT("head")))
        {
            OutLocation = ListenerCharacter->GetMesh()->GetSocketLocation(TEXT("head"))
                          + FVector(0, 0, EarHeightOffset);
        }
    }
}

bool AWandererPlayerController::AreEarsUnderwater() const
{
    return ListenerAudioManager && ListenerAudioManager->AreEarsUnderwater();
}

void AWandererPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    if (!IsFrameworkActive() || !IsLocalController()
        || GetWorld()->GetFirstPlayerController() != this)
    {
        return;
    }
    WaterSampleElapsedSec += DeltaSeconds;
    if (WaterSampleElapsedSec < WaterSampleIntervalSec)
    {
        return;
    }
    WaterSampleElapsedSec = 0;
    UpdateUnderwaterAudio();
    if (IsFrameworkActive() && ListenerAudioManager)
    {
        ListenerAudioManager->RetryPendingMix();
    }
}

void AWandererPlayerController::InitializeAudioDefaults()
{
    static ConstructorHelpers::FObjectFinder<UAkAudioEvent> PauseEvent(
        TEXT("/Game/WwiseAudio/Events/UI/Pause_Menu_SFX"));
    static ConstructorHelpers::FObjectFinder<UAkAudioEvent> ResumeEvent(
        TEXT("/Game/WwiseAudio/Events/UI/Resume_Menu_SFX"));
    MenuPauseEvent = PauseEvent.Object;
    MenuResumeEvent = ResumeEvent.Object;
}

void AWandererPlayerController::UpdateUnderwaterAudio()
{
    if (!ListenerAudioManager)
    {
        return;
    }
    const ACharacter* ListenerCharacter = Cast<ACharacter>(GetPawn());
    const ISwimmingStateProvider* Swimming =
        ListenerCharacter
            ? Cast<ISwimmingStateProvider>(ListenerCharacter->GetCharacterMovement())
            : nullptr;
    FListenerWaterSample Sample;
    float Surface = 0.0f;
    Sample.bHasSurface = Swimming && Swimming->GetSurfaceHeight(Surface);
    FVector Ear;
    FVector Front;
    FVector Right;
    GetAudioListenerPosition(Ear, Front, Right);
    Sample.DepthCm = Surface - Ear.Z;
    Sample.SampleTimeSec = GetWorld()->GetTimeSeconds();
    Sample.HysteresisCm = WaterlineHysteresis;
    Sample.HoldSec = WaterStateHoldTime;
    Sample.TransitionIntervalMinSec = MinWaterStateInterval;
    ListenerAudioManager->UpdateWater(Sample);
}

void AWandererPlayerController::ResetUnderwaterAudio(bool bReleaseOwnership)
{
    if (ListenerAudioManager)
    {
        ListenerAudioManager->ResetWater(bReleaseOwnership);
    }
}

void AWandererPlayerController::SetMenuAudioDucking(EWandererPlayState PlayState)
{
    if (!ListenerAudioManager || (!IsLocalController() && PlayState != EWandererPlayState::Normal))
    {
        return;
    }
    const EListenerMenuMode Mode = PlayState == EWandererPlayState::PauseMenu
        ? EListenerMenuMode::Paused : PlayState == EWandererPlayState::UI
        ? EListenerMenuMode::Focused : EListenerMenuMode::Closed;
    ListenerAudioManager->SetMenuState(Mode, MenuPauseEvent, MenuResumeEvent);
}

