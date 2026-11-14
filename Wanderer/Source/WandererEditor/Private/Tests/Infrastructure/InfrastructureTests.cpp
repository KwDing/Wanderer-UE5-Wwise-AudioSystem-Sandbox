#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#include "Core/ActorPathUtils.h"
#include "Core/CharacterTeleportLibrary.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Tests/Infrastructure/EventBusTestSubsystem.h"
#include "Tests/Infrastructure/TeleportTestCharacter.h"

namespace
{
    struct FInfrastructureWorld
    {
        UWorld* World = nullptr;
        FInfrastructureWorld()
        {
            UWorld::InitializationValues Values;
            Values.AllowAudioPlayback(false)
                .CreatePhysicsScene(true)
                .CreateNavigation(false)
                .CreateAISystem(false)
                .ShouldSimulatePhysics(false)
                .SetTransactional(false);
            World = UWorld::CreateWorld(
                EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        }
        ~FInfrastructureWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        AActor* AddBox(const FVector& InLocation, const FVector& InExtent)
        {
            AActor* Actor = World->SpawnActor<AActor>();
            UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box);
            Box->SetBoxExtent(InExtent);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Actor->SetActorLocation(InLocation);
            return Actor;
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfrastructureLegacyTest,
    "Wanderer.Infrastructure.LegacyAudioCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInfrastructureLegacyTest::RunTest(const FString& InParameters)
{
#if !UE_BUILD_SHIPPING
    const int32 Previous = FDiagnosticRecorder::GetPreset();
    const bool bFootsteps = FDiagnosticRecorder::IsFootstepsEnabled();
    ON_SCOPE_EXIT
    {
        FDiagnosticRecorder::SetPreset(Previous);
        FDiagnosticRecorder::SetFootstepsEnabled(bFootsteps);
    };
    FOutputDeviceNull Output;
    IConsoleManager& Console = IConsoleManager::Get();
    TestTrue(TEXT("Legacy master accepted"),
        Console.ProcessUserConsoleInput(TEXT("Wanderer.AudioLog 1"), Output, nullptr));
    TestEqual(TEXT("Master maps to timed playback"), FDiagnosticRecorder::GetPreset(), 1);
    Console.ProcessUserConsoleInput(TEXT("Wanderer.AudioLog.Footsteps 1"), Output, nullptr);
    TestTrue(TEXT("Footsteps maps to opt-in"), FDiagnosticRecorder::IsFootstepsEnabled());
    Console.ProcessUserConsoleInput(TEXT("Wanderer.AudioLog.Amb 0"), Output, nullptr);
    TestEqual(TEXT("Removed channel off cannot disable other captures"),
        FDiagnosticRecorder::GetPreset(),
        1);
    Console.ProcessUserConsoleInput(TEXT("Wanderer.AudioLog 0"), Output, nullptr);
    TestEqual(TEXT("Master off retains problem mode"), FDiagnosticRecorder::GetPreset(), 0);
    Console.ProcessUserConsoleInput(TEXT("Wanderer.AudioLog.Amb 1"), Output, nullptr);
    TestEqual(TEXT("Legacy enable starts timed playback"), FDiagnosticRecorder::GetPreset(), 1);
    Console.ProcessUserConsoleInput(TEXT("Wanderer.AudioLog nonsense"), Output, nullptr);
    TestEqual(TEXT("Malformed input does not change state"), FDiagnosticRecorder::GetPreset(), 1);
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfrastructurePathTest,
    "Wanderer.Infrastructure.ActorPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInfrastructurePathTest::RunTest(const FString& InParameters)
{
    FInfrastructureWorld Scope;
    const FSoftObjectPath Path(TEXT("/Game/Maps/PathAudit.PathAudit:PersistentLevel.Actor_1"));
    TestEqual(TEXT("Non-PIE preserved"), ActorPathUtils::ForWorld(Path, Scope.World), Path);
    TestEqual(TEXT("Null world preserved"), ActorPathUtils::ForWorld(Path, nullptr), Path);
    Scope.World->WorldType = EWorldType::PIE;
    UPackage* Package = Scope.World->GetOutermost();
    const int32 PreviousInstance = Package->GetPIEInstanceID();
    ON_SCOPE_EXIT
    {
        Package->SetPIEInstanceID(PreviousInstance);
    };
    Package->SetPIEInstanceID(3);
    const FSoftObjectPath Third = ActorPathUtils::ForWorld(Path, Scope.World);
    TestTrue(TEXT("World-local PIE identity"), Third.ToString().Contains(TEXT("UEDPIE_3_")));
    TestEqual(
        TEXT("Repeated conversion is stable"), ActorPathUtils::ForWorld(Third, Scope.World), Third);
    TestTrue(TEXT("Null path stays null"),
        ActorPathUtils::ForWorld(FSoftObjectPath(), Scope.World).IsNull());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfrastructureEventTest,
    "Wanderer.Infrastructure.EventShutdownAndRecursion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInfrastructureEventTest::RunTest(const FString& InParameters)
{
    FInfrastructureWorld Scope;
    UGameInstance* Instance = NewObject<UGameInstance>();
    UEventBusTestSubsystem* Bus = NewObject<UEventBusTestSubsystem>(Instance);
    Bus->WorldWeakPtr = Scope.World;
    Bus->OnGameEvent.AddDynamic(Bus, &UEventBusTestSubsystem::OnMessage);
    FWandererGameEventMessage Message;
    Message.Text = TEXT("original");
    int32 Observed = 0;
    const FDelegateHandle Handle = UWandererEventBusSubsystem::OnEventObserved().AddLambda(
        [&](UWorld* InWorld, const FWandererGameEventMessage& InMessage)
        {
            if (InWorld != Scope.World)
            {
                return;
            }
            ++Observed;
            Bus->Publish(InMessage);
        });
    Bus->Publish(Message);
    UWandererEventBusSubsystem::OnEventObserved().Remove(Handle);
    TestEqual(TEXT("Recursive chain bounded"), Observed, 8);
    TestEqual(TEXT("Accepted levels delivered"), Bus->Received, 8);
    Bus->bStopOnReceive = true;
    Bus->Publish(Message);
    const int32 Before = Bus->Received;
    Bus->Publish(Message);
    TestEqual(TEXT("Stopped bus rejects subsequent publish"), Bus->Received, Before);
    UEventBusTestSubsystem* Second = NewObject<UEventBusTestSubsystem>(Instance);
    Second->WorldWeakPtr = Scope.World;
    Second->OnGameEvent.AddDynamic(Second, &UEventBusTestSubsystem::OnMessage);
    const FDelegateHandle Stop = UWandererEventBusSubsystem::OnEventObserved().AddLambda(
        [&](UWorld*, const FWandererGameEventMessage&)
        {
            Second->Deinitialize();
        });
    Second->Publish(Message);
    UWandererEventBusSubsystem::OnEventObserved().Remove(Stop);
    TestEqual(TEXT("Shutdown in observer cancels later gameplay phase"), Second->Received, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfrastructureTeleportTest,
    "Wanderer.Infrastructure.TeleportPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInfrastructureTeleportTest::RunTest(const FString& InParameters)
{
    FInfrastructureWorld Scope;
    ATeleportTestCharacter* Character = Scope.World->SpawnActor<ATeleportTestCharacter>();
    Character->SetActorLocation(FVector(-300, 0, 150));
    const FVector Before = Character->GetActorLocation();
    FText Failure;
    TestFalse(TEXT("No floor rejects"),
        UCharacterTeleportLibrary::TeleportCharacter(
            Scope.World, Character, FVector::ZeroVector, FRotator::ZeroRotator, Failure));
    TestEqual(TEXT("Preflight does not move"), Character->GetActorLocation(), Before);
    Scope.AddBox(FVector(0, 0, -10), FVector(1000, 1000, 10));
    AActor* Blocker = Scope.AddBox(FVector(0, 0, 100), FVector(80, 80, 100));
    TestFalse(TEXT("Blocked destination rejects"),
        UCharacterTeleportLibrary::TeleportCharacter(
            Scope.World, Character, FVector::ZeroVector, FRotator::ZeroRotator, Failure));
    Blocker->Destroy();
    Character->GetCharacterMovement()->Velocity = FVector(100, 0, 0);
    TestTrue(TEXT("Grounded placement accepted"),
        UCharacterTeleportLibrary::TeleportCharacter(
            Scope.World, Character, FVector::ZeroVector, FRotator(20, 90, 30), Failure));
    TestTrue(TEXT("Velocity stopped"), Character->GetCharacterMovement()->Velocity.IsNearlyZero());
    TestTrue(TEXT("Arrival is yaw-only"), Character->GetActorRotation().Equals(FRotator(0, 90, 0)));
    FInfrastructureWorld Other;
    TestFalse(TEXT("Other world rejected"),
        UCharacterTeleportLibrary::TeleportCharacter(
            Other.World, Character, FVector::ZeroVector, FRotator::ZeroRotator, Failure));
    Character->bDestroyMovementAfterTeleport = true;
    TestTrue(TEXT("Movement destruction during placement tolerated"),
        UCharacterTeleportLibrary::TeleportCharacter(
            Scope.World, Character, FVector(200, 0, 0), FRotator::ZeroRotator, Failure));
    ATeleportTestCharacter* Ended = Scope.World->SpawnActor<ATeleportTestCharacter>();
    Ended->DispatchBeginPlay();
    Ended->bEndPlayAfterTeleport = true;
    TestTrue(TEXT("Accepted placement tolerates non-destroy EndPlay"),
        UCharacterTeleportLibrary::TeleportCharacter(
            Scope.World, Ended, FVector(600, 0, 0), FRotator::ZeroRotator, Failure));
    TestFalse(TEXT("Non-destroy lifetime actually ended"), Ended->HasActorBegunPlay());
    TestEqual(TEXT("Ended lifetime skips movement cleanup"),
        Ended->GetCharacterMovement()->Velocity,
        FVector(123, 0, 0));
    ATeleportTestCharacter* Destroyed = Scope.World->SpawnActor<ATeleportTestCharacter>();
    Destroyed->bDestroyAfterTeleport = true;
    TestTrue(TEXT("Accepted movement remains success after synchronous destruction"),
        UCharacterTeleportLibrary::TeleportCharacter(
            Scope.World, Destroyed, FVector(400, 0, 0), FRotator::ZeroRotator, Failure));
    return true;
}
#endif
