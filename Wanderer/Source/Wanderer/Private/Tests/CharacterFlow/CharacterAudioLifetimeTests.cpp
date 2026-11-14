#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "AkComponent.h"

#include "Data/Audio/WandererCharacterAudioProfile.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterAudioLifetimeTest,
    "Wanderer.Character.AudioLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterAudioLifetimeTest::RunTest(const FString& InParameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    AActor* Owner = World->SpawnActor<AActor>();
    UWandererCharacterAudioComponent* Audio = NewObject<UWandererCharacterAudioComponent>(Owner);
    UAkComponent* Emitter = NewObject<UAkComponent>(Owner);
    Owner->SetRootComponent(Emitter);
    Emitter->RegisterComponentWithWorld(World);
    UWandererCharacterAudioProfile* Profile = NewObject<UWandererCharacterAudioProfile>(Audio);
    UDataTable* Table = NewObject<UDataTable>(Profile);
    Table->RowStruct = FWandererCharacterAudioActionRow::StaticStruct();
    FWandererCharacterAudioActionRow Row;
    Row.bEnabled = false;
    Table->AddRow(TEXT("Footstep"), Row);
    Profile->ActionTables.Add(Table);
    Audio->Profile = Profile;
    TestTrue(TEXT("Disabled action profile still initializes"), Audio->InitializeAudio(Emitter));
    TestTrue(TEXT("Emitter is only observed"), Audio->EmitterWeakPtr.Get() == Emitter);
    auto& Cooldowns = Audio->NextAllowedTimes.FindChecked(TEXT("Footstep"));
    Cooldowns.Get(EWandererAudioPlaybackScope::LeftFoot) = 10.0;
    TestEqual(TEXT("Right foot cooldown is independent"),
        Cooldowns.Get(EWandererAudioPlaybackScope::RightFoot),
        0.0);
    TestEqual(TEXT("Default cooldown is independent"),
        Cooldowns.Get(EWandererAudioPlaybackScope::Default),
        0.0);
    Audio->StopPlayback();
    TestEqual(TEXT("Stop resets left contact cooldown"),
        Audio->NextAllowedTimes.FindChecked(TEXT("Footstep"))
            .Get(EWandererAudioPlaybackScope::LeftFoot),
        0.0);
    TestTrue(TEXT("StopPlayback is not terminal disable"), Audio->IsAudioReady());
    AddExpectedError(TEXT("Audio emitter must be registered and owned by this actor."),
        EAutomationExpectedErrorFlags::Contains,
        1);
    TestFalse(TEXT("Invalid rebind fails"), Audio->InitializeAudio(nullptr));
    TestFalse(TEXT("Failed rebind clears readiness"), Audio->IsAudioReady());
    TestFalse(TEXT("Failed rebind releases old emitter"), Audio->EmitterWeakPtr.IsValid());
    TestTrue(TEXT("Failed rebind clears cached asset rows"), Audio->CachedActions.IsEmpty());
    TestTrue(TEXT("Valid rebind can recover"), Audio->InitializeAudio(Emitter));
    Emitter->DestroyComponent();
    Audio->StopPlayback();
    TestTrue(TEXT("Destroyed observed emitter is safe to stop"), IsValid(Audio));
    UAkComponent* Replacement = NewObject<UAkComponent>(Owner);
    Replacement->RegisterComponentWithWorld(World);
    Owner->AddInstanceComponent(Audio);
    Audio->RegisterComponentWithWorld(World);
    Owner->DispatchBeginPlay();
    if (!TestTrue(TEXT("Component entered play"), Audio->HasBegunPlay()))
    {
        return false;
    }
    TestTrue(TEXT("Replacement emitter can bind before EndPlay"),
        Audio->InitializeAudio(Replacement));
    Audio->EndPlay(EEndPlayReason::Destroyed);
    TestFalse(TEXT("Still-registered emitter cannot revive stopped audio"),
        Audio->InitializeAudio(Replacement));
    TestFalse(TEXT("EndPlay clears readiness"), Audio->IsAudioReady());
    TestTrue(TEXT("EndPlay leaves no action cache"), Audio->CachedActions.IsEmpty());
    TestEqual(TEXT("Ended audio rejects a late contact"),
        Audio->PlayAction(TEXT("Footstep"), FWandererCharacterAudioContext()),
        0);
    return true;
}
#endif
