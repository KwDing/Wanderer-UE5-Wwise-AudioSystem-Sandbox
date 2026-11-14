#include "Framework/CharacterSelectionModel.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "Misc/AutomationTest.h"
#include "Data/WandererCharacterCatalog.h"
#include "Tests/CharacterFlow/CharacterFlowTestActors.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FSelectionFixture
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        ACharacterFlowTestHost* Host = World->SpawnActor<ACharacterFlowTestHost>();
        ACharacterPreviewTestActor* Provider = nullptr;
        APawn* Original = World->SpawnActor<APawn>();
        FSelectionFixture()
        {
            Host->Possess(Original);
            Host->Model = NewObject<UCharacterSelectionModel>(Host);
            UWandererCharacterCatalog* Catalog = NewObject<UWandererCharacterCatalog>(Host);
            FWandererCharacterEntry First;
            First.ID = TEXT("First");
            First.CharacterClass = APawn::StaticClass();
            Catalog->Characters.Add(First);
            FWandererCharacterEntry Second;
            Second.ID = TEXT("Second");
            Second.CharacterClass = ASpectatorPawn::StaticClass();
            Catalog->Characters.Add(Second);
            Host->Model->Initialize(Host, Catalog);
            FActorSpawnParameters Params;
            Params.Owner = Host;
            Provider = World->SpawnActor<ACharacterPreviewTestActor>(Params);
            Provider->ModelWeakPtr = Host->Model;
            Host->Model->RegisterProvider(Provider);
        }
        ~FSelectionFixture()
        {
            Host->Model->Stop();
            World->DestroyWorld(false);
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterSelectionModelTest,
    "Wanderer.Character.SelectionModel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterSelectionModelTest::RunTest(const FString& InParameters)
{
    FSelectionFixture Fixture;
    UCharacterSelectionModel* Model = Fixture.Host->Model;
    Fixture.Host->bLoading = true;
    TestFalse(TEXT("Loading blocks opening"), Model->Open());
    Fixture.Host->bLoading = false;
    TestTrue(TEXT("Open stable model"), Model->Open());
    TestTrue(TEXT("Preview is available"), Model->CanChoose());
    TestEqual(TEXT("Current option"), Model->GetCurrentIndex(), 0);
    TestFalse(TEXT("Invalid index rejected"), Model->Choose(8));
    TestTrue(TEXT("Switch to configured candidate"), Model->Choose(1));
    TestTrue(TEXT("Candidate possessed"), Fixture.Host->GetPawn()->IsA<ASpectatorPawn>());
    TestFalse(TEXT("Old pawn destroyed only after commit"), IsValid(Fixture.Original));
    TestFalse(TEXT("Close releases portrait state"), Model->IsOpen());
    TestNull(TEXT("Hidden model does not expose preview"), Model->GetPortrait());
    TestFalse(TEXT("Hidden capture stopped"), Fixture.Provider->bActive);
    TestTrue(TEXT("Reopen after replacement"), Model->Open());
    TestTrue(TEXT("Second switch succeeds"), Model->Choose(0));
    TestEqual(TEXT("Returned to first option"), Model->GetCurrentIndex(), 0);
    Model->Stop();
    TestFalse(TEXT("Stop is terminal"), Model->Open());
    TestFalse(
        TEXT("Stopped model rejects late registration"), Model->RegisterProvider(Fixture.Provider));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterSelectionRollbackTest,
    "Wanderer.Character.SelectionRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterSelectionRollbackTest::RunTest(const FString& InParameters)
{
    FSelectionFixture Fixture;
    UCharacterSelectionModel* Model = Fixture.Host->Model;
    Model->Open();
    Fixture.Host->bRejectClose = true;
    TestFalse(TEXT("Rejected close rolls back"), Model->Choose(1));
    TestTrue(TEXT("Original possession restored"), Fixture.Host->GetPawn() == Fixture.Original);
    TestTrue(TEXT("Original still alive"), IsValid(Fixture.Original));
    Fixture.Host->bRejectClose = false;
    Fixture.Host->bRejectCandidate = true;
    TestFalse(TEXT("Possession failure rolls back"), Model->Choose(1));
    TestTrue(TEXT("Failed possess retains original"), Fixture.Host->GetPawn() == Fixture.Original);
    Fixture.Host->bRejectCandidate = false;
    Fixture.Host->bStopOnClose = true;
    TestFalse(TEXT("Stop during state callback rejects commit"), Model->Choose(1));
    TestTrue(
        TEXT("Callback teardown restores original"), Fixture.Host->GetPawn() == Fixture.Original);
    TestNull(TEXT("Stopped model clears portrait"), Model->GetPortrait());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterSelectionReentryTest,
    "Wanderer.Character.SelectionReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterSelectionReentryTest::RunTest(const FString& InParameters)
{
    {
        FSelectionFixture Fixture;
        Fixture.Host->Model->Open();
        Fixture.Provider->bStopDuringSpawn = true;
        TestFalse(TEXT("Stop during spawn rejects candidate"), Fixture.Host->Model->Choose(1));
        TestTrue(TEXT("Spawn reentry leaves original possessed"),
            Fixture.Host->GetPawn() == Fixture.Original);
    }
    {
        FSelectionFixture Fixture;
        Fixture.Provider->bStopDuringPreview = true;
        Fixture.Host->Model->Open();
        TestNull(TEXT("Late preview cannot repopulate stopped model"),
            Fixture.Host->Model->GetPortrait());
        TestFalse(TEXT("Late selection rejected"), Fixture.Host->Model->Choose(1));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterSelectionTransferTest,
    "Wanderer.Character.SelectionOwnershipTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterSelectionTransferTest::RunTest(const FString& InParameters)
{
    FSelectionFixture Fixture;
    Fixture.Host->TransferTo = Fixture.World->SpawnActor<APlayerController>();
    Fixture.Host->Model->Open();
    TestFalse(TEXT("Callback ownership transfer cancels original commit"),
        Fixture.Host->Model->Choose(1));
    TestTrue(TEXT("Original controller recovers its own pawn"),
        Fixture.Host->GetPawn() == Fixture.Original);
    TestTrue(TEXT("Other controller retains transferred candidate"),
        Fixture.Host->TransferTo->GetPawn()
            && Fixture.Host->TransferTo->GetPawn()->IsA<ASpectatorPawn>());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterPreviousPawnTransferTest,
    "Wanderer.Character.PreviousPawnTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterPreviousPawnTransferTest::RunTest(const FString& InParameters)
{
    FSelectionFixture Fixture;
    Fixture.Host->TransferTo = Fixture.World->SpawnActor<APlayerController>();
    Fixture.Host->TransferPawn = Fixture.Original;
    Fixture.Host->Model->Open();
    TestTrue(
        TEXT("Switch commits after previous pawn transferred"), Fixture.Host->Model->Choose(1));
    TestTrue(TEXT("Transferred previous pawn remains alive"), IsValid(Fixture.Original));
    TestTrue(TEXT("Other controller keeps previous pawn"),
        Fixture.Host->TransferTo->GetPawn() == Fixture.Original);
    return true;
}
#endif
