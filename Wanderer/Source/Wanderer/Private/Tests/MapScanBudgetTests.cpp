#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#include "Framework/Mapping/WandererMapComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapScanBudgetTest, "Wanderer.Map.ScanBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMapScanBudgetTest::RunTest(const FString& InParameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient scan world"), World))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    AActor* Owner = World->SpawnActor<AActor>();
    auto* Scanner = NewObject<UWandererMapComponent>(Owner);
    AActor* MeshOwner = World->SpawnActor<AActor>();
    auto* DoorMesh = NewObject<UStaticMesh>(MeshOwner, TEXT("MapScanDoor"));
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Door = NewObject<UStaticMeshComponent>(MeshOwner);
        Door->SetStaticMesh(DoorMesh);
        MeshOwner->AddOwnedComponent(Door);
    }

    using EPhase = UWandererMapComponent::EScanPhase;
    Scanner->Phase = EPhase::Seed;
    Scanner->Snapshot.bScanning = true;
    Scanner->AdvanceScan(0.0);
    TestTrue(TEXT("Expired deadline leaves seed pending"), Scanner->Phase == EPhase::Seed);

    Scanner->Phase = EPhase::Bounds;
    Scanner->BeginSurveyBounds();
    Scanner->BoundsActorWeakPtr = MeshOwner;
    Scanner->AdvanceScan(0.0);
    TestTrue(TEXT("Bounds stage counts as scanning"), Scanner->IsScanning());
    TestEqual(TEXT("Expired budget leaves the first component pending"), Scanner->NextComponent, 0);
    Scanner->AdvanceScanStep();
    TestEqual(TEXT("One bounds step consumes one component slot"), Scanner->NextComponent, 1);
    TestEqual(TEXT("Door is collected in the bounds traversal"), Scanner->DoorCandidates.Num(), 1);
    Scanner->AdvanceScanStep();
    TestEqual(TEXT("Bounds cursor resumes at the next component"), Scanner->NextComponent, 2);
    TestEqual(TEXT("Next Door candidate is retained"), Scanner->DoorCandidates.Num(), 2);

    Scanner->Phase = EPhase::Entrances;
    Scanner->AdvanceScan(0.0);
    TestEqual(TEXT("Expired budget leaves the first entrance pending"), Scanner->NextDoor, 0);
    Scanner->AdvanceScanStep();
    TestEqual(TEXT("One entrance step consumes one candidate"), Scanner->NextDoor, 1);
    TestTrue(TEXT("Entrance stage keeps the scan active"), Scanner->IsScanning());
    Scanner->OnScanLevelChanged(World->PersistentLevel, World);
    Scanner->AdvanceScan(TNumericLimits<double>::Max());
    TestEqual(TEXT("Level change invalidates the in-flight entrance pass"), Scanner->NextDoor, 1);
    Scanner->Rescan();
    TestTrue(TEXT("Restart clears the old candidate list and cursors"),
        Scanner->DoorCandidates.IsEmpty() && Scanner->NextDoor == 0 && Scanner->NextComponent == 0);
    TestFalse(TEXT("Waiting for next scan has no active work"), Scanner->IsScanning());
    return true;
}
#endif
