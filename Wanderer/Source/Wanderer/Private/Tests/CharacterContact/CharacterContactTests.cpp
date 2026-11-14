#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "Components/Footsteps/FootContactComponent.h"
#include "Components/Footsteps/WandererFootstepVFXComponent.h"
#include "Framework/Surface/WandererSurfaceComponent.h"
#include "Tests/CharacterContact/SurfaceNotificationProbe.h"
#include "Framework/WandererPlayerController.h"
#include "Interfaces/CharacterSurfaceProvider.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FContactWorld
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        AActor* Owner = nullptr;
        FContactWorld()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Owner = World->SpawnActor<AActor>();
        }
        ~FContactWorld()
        {
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
    };
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootContactGateTest,
    "Wanderer.CharacterContact.Gates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFootContactGateTest::RunTest(const FString& InParameters)
{
    FContactWorld Fixture;
    UFootContactComponent* Contact = NewObject<UFootContactComponent>(Fixture.Owner);
    Contact->LeftFootContact.bArmed = true;
    Contact->RightFootContact.bArmed = true;
    TestFalse(TEXT("First sample waits for confirmation"),
        Contact->ConfirmFootContact(Contact->LeftFootContact, true, 1.0));
    TestFalse(TEXT("Insufficient confirmation duration"),
        Contact->ConfirmFootContact(Contact->LeftFootContact, true, 1.01));
    TestTrue(TEXT("Confirmed edge consumed"),
        Contact->ConfirmFootContact(Contact->LeftFootContact, true, 1.02));
    TestFalse(TEXT("Left foot disarmed after consumption"), Contact->LeftFootContact.bArmed);
    TestTrue(TEXT("Other foot remains independently armed"), Contact->RightFootContact.bArmed);
    Contact->LeftFootContact.ContactSince = 2.0;
    TestFalse(TEXT("Lost support clears confirmation"),
        Contact->ConfirmFootContact(Contact->LeftFootContact, false, 2.1));
    TestEqual(TEXT("No delayed contact retry"), Contact->LeftFootContact.ContactSince, -1.0);
    Contact->StartAirborneContacts();
    TestTrue(TEXT("Both feet await landing"),
        Contact->LeftFootContact.bLandingPending && Contact->RightFootContact.bLandingPending);
    Contact->LeftFootContact.bHasLanded = true;
    Contact->LeftFootContact.bLandingPending = false;
    Contact->BeginLandingContacts();
    TestFalse(TEXT("Already landed foot is not rearmed"), Contact->LeftFootContact.bLandingPending);
    TestTrue(TEXT("Other landing foot armed"), Contact->RightFootContact.bArmed);
    Contact->LeftFootContact.SoleOffset = 3.0f;
    Contact->Stop();
    Contact->Stop();
    Contact->RequestJumpStartAudio();
    Contact->StartAirborneContacts();
    Contact->BeginLandingContacts();
    TestFalse(TEXT("Stopped detector rejects jump requests"), Contact->bJumpAudioPending);
    TestFalse(TEXT("Stop disarms pending landing"), Contact->RightFootContact.bLandingPending);
    TestEqual(TEXT("Reset preserves sole calibration"), Contact->LeftFootContact.SoleOffset, 3.0f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSurfacePublicationTest,
    "Wanderer.CharacterContact.SurfacePublication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSurfacePublicationTest::RunTest(const FString& InParameters)
{
    FContactWorld Fixture;
    UWandererSurfaceComponent* Surface = NewObject<UWandererSurfaceComponent>(Fixture.Owner);
    Surface->RegisterComponent();
    USurfaceNotificationProbe* Probe = NewObject<USurfaceNotificationProbe>(Fixture.Owner);
    Probe->Surface = Surface;
    Surface->OnSurfaceChanged.AddDynamic(Probe, &USurfaceNotificationProbe::OnSurface);
    Surface->OnWetnessChanged.AddDynamic(Probe, &USurfaceNotificationProbe::OnWetness);
    Surface->PublishContact(TEXT("soil"), TEXT("mild"));
    TestEqual(TEXT("Reentry does not publish recursively"), Probe->SurfaceCalls, 1);
    TestEqual(
        TEXT("Paired snapshot committed before callback"), Probe->ObservedWetness, FName("mild"));
    TestEqual(TEXT("Reentry did not mutate current surface"),
        Surface->GetCurrentSurface(),
        FName("soil"));
    TestEqual(TEXT("Wetness delivered once"), Probe->WetnessCalls, 1);
    Surface->PublishContact(TEXT("sand"), TEXT("wet"));
    TestEqual(TEXT("Changed sample waits for debounce"), Probe->SurfaceCalls, 1);
    Surface->PublishContact(TEXT("sand"), TEXT("wet"));
    TestEqual(TEXT("Second consistent sample commits"), Probe->SurfaceCalls, 2);
    Probe->bDestroyOnSurface = true;
    Surface->PublishContact(TEXT("stone"), TEXT("dry"));
    Surface->PublishContact(TEXT("stone"), TEXT("dry"));
    TestFalse(TEXT("Callback destroyed component"), IsValid(Surface));
    TestEqual(TEXT("No wetness callback after teardown"), Probe->WetnessCalls, 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootEffectLifetimeTest,
    "Wanderer.CharacterContact.EffectLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFootEffectLifetimeTest::RunTest(const FString& InParameters)
{
    FContactWorld Fixture;
    UWandererFootstepVFXComponent* Effects =
        NewObject<UWandererFootstepVFXComponent>(Fixture.Owner);
    USceneComponent* First = nullptr;
    for (int32 Index = 0; Index < 129; ++Index)
    {
        USceneComponent* Effect = NewObject<USceneComponent>(Fixture.Owner);
        Effect->RegisterComponent();
        if (Index == 0)
        {
            First = Effect;
        }
        Effects->AddEffect(Effect, 45.0f);
    }
    TestEqual(TEXT("Effect capacity bounded"), Effects->GetActiveEffectCount(), 128);
    TestFalse(TEXT("Oldest effect destroyed at capacity"), IsValid(First));
    const TWeakObjectPtr<USceneComponent> RemainingWeakPtr = Effects->Effects.Last().Component;
    Effects->bClearingEffects = true;
    USceneComponent* Reentrant = NewObject<USceneComponent>(Fixture.Owner);
    Reentrant->RegisterComponent();
    Effects->AddEffect(Reentrant, 1.0f);
    TestFalse(TEXT("Cleanup rejects reentrant effects"), IsValid(Reentrant));
    Effects->bClearingEffects = false;
    Effects->Stop();
    Effects->Stop();
    TestEqual(TEXT("Idempotent stop clears tracking"), Effects->GetActiveEffectCount(), 0);
    TestFalse(TEXT("Stop destroys tracked effects"), RemainingWeakPtr.IsValid());
    USceneComponent* Late = NewObject<USceneComponent>(Fixture.Owner);
    Late->RegisterComponent();
    Effects->AddEffect(Late, 1.0f);
    TestFalse(TEXT("Late effect destroyed after stop"), IsValid(Late));
    TestEqual(TEXT("No late effect retained"), Effects->GetActiveEffectCount(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSurfaceOwnerContractTest,
    "Wanderer.CharacterContact.SurfaceOwnerContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSurfaceOwnerContractTest::RunTest(const FString& InParameters)
{
    FContactWorld Fixture;
    AWandererPlayerController* Controller = Fixture.World->SpawnActor<AWandererPlayerController>();
    ICharacterSurfaceProvider* Provider = Cast<ICharacterSurfaceProvider>(Controller);
    if (!TestNotNull(TEXT("Controller exposes stable surface contract"), Provider))
    {
        return false;
    }
    UWandererSurfaceComponent* Child =
        Controller->FindComponentByClass<UWandererSurfaceComponent>();
    if (!TestNotNull(TEXT("Controller owns surface child without a Pawn"), Child))
    {
        return false;
    }
    Provider->RefreshContactRegions();
    const FCharacterSurfaceSnapshot Snapshot =
        Provider->QueryContactSurface(nullptr, FVector::ZeroVector);
    TestEqual(TEXT("Contract reads child snapshot"), Snapshot.Surface, Child->GetCurrentSurface());
    Child->DestroyComponent();
    Provider->RefreshContactRegions();
    TestEqual(TEXT("Destroyed child returns safe fallback"),
        Provider->QueryContactSurface(nullptr, FVector::ZeroVector).Surface,
        FName("ground"));
    TestEqual(TEXT("Destroyed child classification is safe"),
        Provider->ResolveSurfaceName(FHitResult()),
        FName("ground"));
    return true;
}
#endif
