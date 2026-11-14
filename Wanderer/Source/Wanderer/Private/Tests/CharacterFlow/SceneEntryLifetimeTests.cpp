#include "Framework/WandererLoadingComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/CharacterFlow/CharacterFlowTestActors.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneEntryLifetimeTest,
    "Wanderer.Character.SceneEntryLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSceneEntryLifetimeTest::RunTest(const FString& InParameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    ACharacterFlowTestHost* Host = World->SpawnActor<ACharacterFlowTestHost>();
    UWandererLoadingComponent* Loading = NewObject<UWandererLoadingComponent>(Host);
    Host->SetIgnoreMoveInput(true);
    Host->SetIgnoreLookInput(true);
    Loading->StartLoading();
    Loading->StartLoading();
    TestTrue(TEXT("Entry lock holds"), Host->IsMoveInputIgnored());
    TestFalse(TEXT("Map scan gated until streaming ready"), Host->bMapScanEnabled);
    Loading->Stop();
    Loading->Stop();
    TestTrue(TEXT("Other owner's move lock survives"), Host->IsMoveInputIgnored());
    TestTrue(TEXT("Other owner's look lock survives"), Host->IsLookInputIgnored());
    Host->SetIgnoreMoveInput(false);
    Host->SetIgnoreLookInput(false);
    TestFalse(TEXT("No leaked duplicate move lock"), Host->IsMoveInputIgnored());
    TestFalse(TEXT("No leaked duplicate look lock"), Host->IsLookInputIgnored());
    Loading->StartLoading();
    TestFalse(TEXT("Terminal Stop cannot reacquire input"), Host->IsMoveInputIgnored());
    return true;
}
#endif
