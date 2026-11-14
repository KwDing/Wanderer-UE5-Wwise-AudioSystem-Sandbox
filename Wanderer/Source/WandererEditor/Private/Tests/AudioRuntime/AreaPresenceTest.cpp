#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAreaPresenceTest, "Wanderer.Audio.Area.Presence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAreaPresenceTest::RunTest(const FString& Parameters)
{
    auto* Area = GEditor->GetEditorWorldContext().World()->SpawnActor<AWandererAudioAreaEmitter>();
    if (!TestNotNull(TEXT("Area"), Area)) return false;
    ON_SCOPE_EXIT { Area->Destroy(); };
    Area->CellSize = 100;
    Area->HeightAboveSurface = 200;
    Area->DepthBelowSurface = 50;
    Area->SetSurfaceCells({FVector::ZeroVector, FVector(300, 0, 0)});
    Area->SetActorLocation(FVector(1000, 2000, 0));
    auto* Table = NewObject<UDataTable>(Area);
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FWandererWorldAudioProfileRow Row;
    Row.Mode = EWandererWorldAudioMode::Area;
    Row.PlayEvent = FSoftObjectPath(TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_snow.Play_Amb_snow"));
    Table->AddRow(TEXT("Presence"), Row);
    Area->AudioSource->Profile.DataTable = Table;
    Area->AudioSource->Profile.RowName = TEXT("Presence");
    const auto Check = [&](FVector Local, float Expected, const TCHAR* Label)
    {
        TestTrue(TEXT("Valid nearby candidate"), Area->AudioSource->Evaluate(Area->GetActorTransform().TransformPosition(Local)));
        TestEqual(Label, Area->AudioSource->GetInsideBlend(), Expected);
    };
    Check(FVector(0, 0, 100), 1, TEXT("Inside cell"));
    Check(FVector(51, 0, 100), 0, TEXT("One cm outside is silent despite spread radius"));
    Check(FVector(150, 0, 100), 0, TEXT("Hole inside overall bounds is not inside area"));
    Check(FVector(300, 0, 100), 1, TEXT("Reentry into second cell"));
    Check(FVector(300, 0, 201), 0, TEXT("Above volume is outside"));
    return true;
}
#endif
