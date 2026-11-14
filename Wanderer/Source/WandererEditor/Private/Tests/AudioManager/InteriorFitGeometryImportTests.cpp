#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"
#include "AkAudioEvent.h"
#include "Components/SplineComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "InteriorFitArea.h"
#include "Misc/AutomationTest.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace
{
FAudioSourceRequest MakeRequest(UWorld* World)
{
    FAudioSourceRequest Request;
    Request.Action = EAudioSourceAction::ImportGeometry;
    Request.World = World;
    auto* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    Request.TablePath = FSoftObjectPath(Table);
    Request.Profile.PlayEvent = NewObject<UAkAudioEvent>();
    Request.Profile.NightPlayEvent = NewObject<UAkAudioEvent>();
    Request.ResourceGroup = TEXT("Forest");
    return Request;
}

AActor* MakeGuide(UWorld* World, bool bClosed)
{
    UClass* Class = LoadClass<AActor>(nullptr, TEXT("/Script/InteriorFitEditor.InteriorFitSplineGuide"));
    if (!Class) return nullptr;
    auto* Actor = World->SpawnActor<AActor>(Class);
    auto* Spline = Actor->FindComponentByClass<USplineComponent>();
    Spline->ClearSplinePoints(false);
    Spline->AddPoint(FSplinePoint(0, FVector(0, 0, 0), ESplinePointType::Linear), false);
    Spline->AddPoint(FSplinePoint(1, FVector(400, 0, 0), ESplinePointType::Linear), false);
    Spline->AddPoint(FSplinePoint(2, FVector(400, 600, 0), ESplinePointType::Linear), false);
    Spline->SetClosedLoop(bClosed);
    Actor->SetActorTransform(FTransform(FRotator(0, 37, 0), FVector(200, -300, 80), FVector(2, 3, 1.5)));
    return Actor;
}

int32 CountEmitters(UWorld* World)
{
    int32 Count = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (It->IsA<AWandererAudioAreaEmitter>() || It->IsA<AWandererAudioSplineEmitter>()) ++Count;
    return Count;
}

bool CopyWithoutEditorProperties(UObject* Source, UObject* Target)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Bytes);
    Writer.SetFilterEditorOnly(true);
    Source->SerializeScriptProperties(Writer);
    FObjectReader Reader(Bytes);
    Reader.SetFilterEditorOnly(true);
    Target->SerializeScriptProperties(Reader);
    return !Writer.IsError() && !Reader.IsError();
}

bool CheckImportedGeometry(FAutomationTestBase& Test, const FAudioSourceRequest& Request, USplineComponent* Spline, AInteriorFitArea* Area)
{
    AWandererAudioSplineEmitter* Imported = nullptr;
    AWandererAudioAreaEmitter* ImportedArea = nullptr;
    // Iterators unregister from their world on destruction; end their scope before map teardown.
    for (TActorIterator<AWandererAudioSplineEmitter> It(Request.World.Get()); It; ++It) { Imported = *It; break; }
    for (TActorIterator<AWandererAudioAreaEmitter> It(Request.World.Get()); It; ++It) { ImportedArea = *It; break; }
    if (!Test.TestNotNull(TEXT("Spline copied"), Imported) || !Test.TestNotNull(TEXT("Area copied"), ImportedArea)) return false;
    for (int32 Index = 0; Index <= 32; ++Index)
        Test.TestTrue(TEXT("Curve world positions unchanged"), Spline->GetLocationAtSplineInputKey(Index / 16.f, ESplineCoordinateSpace::World)
            .Equals(Imported->Spline->GetLocationAtSplineInputKey(Index / 16.f, ESplineCoordinateSpace::World), .001));
    Test.TestTrue(TEXT("Area full transform preserved"), ImportedArea->GetActorTransform().Equals(Area->GetActorTransform(), .001));
    Test.TestTrue(TEXT("Area cells copied"), ImportedArea->GetSurfaceCells() == Area->GetSurfaceCells());
    Test.TestEqual(TEXT("Area extents copied"), ImportedArea->DepthBelowSurface, Area->DepthBelowSurface);
    Test.TestEqual(TEXT("Night configuration retained"), Imported->AudioSource->GetProfile()->NightPlayEvent, Request.Profile.NightPlayEvent);
    Test.TestEqual(TEXT("Batch shared across branches"), Imported->AudioSource->GeometryImportBatch, ImportedArea->AudioSource->GeometryImportBatch);
    Test.TestEqual(TEXT("Group configured"), Imported->AudioSource->ResourceGroup, Request.ResourceGroup);
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryImportCreateTest, "Wanderer.Audio.GeometryImport.Create", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeometryImportCreateTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Request = MakeRequest(World);
    auto* Guide = MakeGuide(World, false);
    if (!TestNotNull(TEXT("IF Guide available"), Guide)) return false;
    auto* Spline = Guide->FindComponentByClass<USplineComponent>();
    Spline->SetSplinePointType(0, ESplinePointType::CurveCustomTangent, false);
    Spline->SetTangentsAtSplinePoint(0, FVector(50, 200, 0), FVector(200, 400, 50), ESplineCoordinateSpace::Local);
    auto* Area = World->SpawnActor<AInteriorFitArea>();
    Area->SetSurfaceCells({FVector(0, 0, 0), FVector(100, 100, 50)});
    Area->CellSize = 75;
    Area->HeightAboveSurface = 150;
    Area->DepthBelowSurface = 350;
    Area->SetActorTransform(FTransform(FRotator(20, 60, 15), FVector(-500, 400, 200), FVector(2, 1, 3)));
    Request.Targets = {Guide, Area};
    const FAudioSourceResult Created = FAudioSourceAuthoringOperation::Apply(Request);
    if (!Created.Errors.IsEmpty())
    {
        AddInfo(Created.Summary());
    }
    TestEqual(TEXT("Mixed batch creates two independent sources"), Created.Applied, 2);
    if (!CheckImportedGeometry(*this, Request, Spline, Area)) return false;
    GEditor->UndoTransaction();
    TestEqual(TEXT("Batch undo removes both sources"), CountEmitters(World), 0);
    auto* Table = Cast<UDataTable>(Request.TablePath.ResolveObject());
    TestEqual(TEXT("Undo removes created rows"), Table->GetRowNames().Num(), 0);
    GEditor->RedoTransaction();
    TestEqual(TEXT("Batch redo restores both sources"), CountEmitters(World), 2);
    for (TActorIterator<AWandererAudioSplineEmitter> It(World); It; ++It)
    {
        It->RecenterSpline();
        for (int32 Index = 0; Index <= 32; ++Index)
            TestTrue(TEXT("Recenter retains curve in world"), Spline->GetLocationAtSplineInputKey(Index / 16.f, ESplineCoordinateSpace::World)
                .Equals(It->Spline->GetLocationAtSplineInputKey(Index / 16.f, ESplineCoordinateSpace::World), .001));
    }
    World->EditorDestroyActor(Guide, true);
    World->EditorDestroyActor(Area, true);
    for (TActorIterator<AWandererAudioSplineEmitter> It(World); It; ++It)
    {
        FVector Position;
        float Distance = 0;
        float Blend = 0;
        TestTrue(TEXT("Query independent of deleted Guide"), It->QueryListener(FVector(500, 500, 100), 8, Position, Distance, Blend));
        TestFalse(TEXT("Runtime emitter not editor only"), It->IsEditorOnly());
    }
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryImportUpdateTest, "Wanderer.Audio.GeometryImport.Update", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeometryImportUpdateTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Guide = MakeGuide(World, true);
    if (!TestNotNull(TEXT("IF Guide available"), Guide)) return false;
    auto* Target = World->SpawnActor<AWandererAudioSplineEmitter>();
    auto Request = MakeRequest(World);
    auto* Table = Cast<UDataTable>(Request.TablePath.ResolveObject());
    Table->AddRow(TEXT("Original"), Request.Profile);
    Target->AudioSource->Profile.DataTable = Table;
    Target->AudioSource->Profile.RowName = TEXT("Original");
    Target->AudioSource->ResourceGroup = TEXT("OriginalGroup");
    Target->AudioSource->bEnabled = false;
    Target->AudioSource->PriorityBias = 17;
    const FTransform OriginalTransform = Target->GetActorTransform();
    const FVector OriginalPoint = Target->Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);
    TestEqual(TEXT("Adopt transform update succeeds"), FInteriorFitGeometryImportOperation::Update(Guide, Target, EGeometryImportTransform::AdoptSource).Applied, 1);
    TestTrue(TEXT("Full transform adopted"), Target->GetActorTransform().Equals(Guide->GetActorTransform()));
    TestEqual(TEXT("Profile reference unchanged"), Target->AudioSource->Profile.RowName, FName(TEXT("Original")));
    TestEqual(TEXT("Night event unchanged"), Target->AudioSource->GetProfile()->NightPlayEvent, Request.Profile.NightPlayEvent);
    TestEqual(TEXT("Group unchanged"), Target->AudioSource->ResourceGroup, FName(TEXT("OriginalGroup")));
    TestEqual(TEXT("Budget priority unchanged"), Target->AudioSource->PriorityBias, 17.f);
    TestFalse(TEXT("Enabled unchanged"), Target->AudioSource->bEnabled);
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores transform"), Target->GetActorTransform().Equals(OriginalTransform));
    TestTrue(TEXT("Undo restores geometry"), Target->Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local).Equals(OriginalPoint));
    TestTrue(TEXT("Undo restores metadata"), Target->AudioSource->GeometryImportSource.IsEmpty());
    TestEqual(TEXT("Keep target update succeeds"), FInteriorFitGeometryImportOperation::Update(Guide, Target, EGeometryImportTransform::KeepTarget).Applied, 1);
    TestTrue(TEXT("Target transform retained"), Target->GetActorTransform().Equals(OriginalTransform));
    const FString Digest = Target->AudioSource->GeometryImportDigest;
    Guide->SetActorRotation(FRotator(40, 0, 0));
    TestEqual(TEXT("Tilted Lake rejected"), FInteriorFitGeometryImportOperation::Update(Guide, Target, EGeometryImportTransform::AdoptSource).Applied, 0);
    TestTrue(TEXT("Failed update restores transform"), Target->GetActorTransform().Equals(OriginalTransform));
    TestEqual(TEXT("Failed update preserves metadata"), Target->AudioSource->GeometryImportDigest, Digest);
    TestEqual(TEXT("Update never adds profile rows"), Table->GetRowNames().Num(), 1);
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryImportFailureTest, "Wanderer.Audio.GeometryImport.FailureAndCookContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeometryImportFailureTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Request = MakeRequest(World);
    auto* First = MakeGuide(World, false);
    auto* InvalidLake = MakeGuide(World, true);
    if (!First || !InvalidLake) return false;
    InvalidLake->SetActorRotation(FRotator(45, 0, 0));
    Request.Targets = {First, InvalidLake};
    auto* Table = Cast<UDataTable>(Request.TablePath.ResolveObject());
    Table->AddRow(TEXT("Preexisting"), FWandererWorldAudioProfileRow());
    TestEqual(TEXT("Later branch failure aborts batch"), FInteriorFitGeometryImportOperation::Create(Request).Applied, 0);
    TestEqual(TEXT("No orphan emitters"), CountEmitters(World), 0);
    TestEqual(TEXT("No orphan template rows"), Table->GetRowNames().Num(), 1);
    TestTrue(TEXT("Existing table row retained"), Table->GetRowNames().Contains(TEXT("Preexisting")));
    First->SetActorScale3D(FVector(0, 1, 1));
    Request.Targets = {First};
    TestEqual(TEXT("Zero scale rejected"), FInteriorFitGeometryImportOperation::Create(Request).Applied, 0);
    auto* Arbitrary = World->SpawnActor<AWandererAudioSplineEmitter>();
    TestFalse(TEXT("Wanderer spline not accepted as IF source"), FInteriorFitGeometryImportOperation::IsSource(Arbitrary));
    Request.Targets = {Arbitrary};
    TestEqual(TEXT("Unrelated source rejected"), FInteriorFitGeometryImportOperation::Create(Request).Applied, 0);
    for (const FName Name : {FName(TEXT("GeometryImportSource")), FName(TEXT("GeometryImportBatch")), FName(TEXT("GeometryImportDigest"))})
    {
        const FProperty* Property = FindFProperty<FProperty>(UWandererAudioSourceComponent::StaticClass(), Name);
        TestTrue(TEXT("Provenance stripped by cook"), Property && Property->HasAnyPropertyFlags(CPF_EditorOnly));
    }
    const FProperty* Cells = FindFProperty<FProperty>(AInteriorFitArea::StaticClass(), TEXT("SurfaceCells"));
    const FProperty* Spline = FindFProperty<FProperty>(AWandererAudioSplineEmitter::StaticClass(), TEXT("Spline"));
    TestTrue(TEXT("Area cells retained in cook"), Cells && !Cells->HasAnyPropertyFlags(CPF_EditorOnly | CPF_Transient));
    TestTrue(TEXT("Owned spline retained in cook"), Spline && !Spline->HasAnyPropertyFlags(CPF_EditorOnly | CPF_Transient));
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryImportSerializationTest, "Wanderer.Audio.GeometryImport.RuntimeSerialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeometryImportSerializationTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Request = MakeRequest(World);
    auto* Guide = MakeGuide(World, false);
    if (!Guide) return false;
    Request.Targets = {Guide};
    if (FAudioSourceAuthoringOperation::Apply(Request).Applied != 1) return false;
    auto* Imported = *TActorIterator<AWandererAudioSplineEmitter>(World);
    auto* RuntimeCopy = World->SpawnActor<AWandererAudioSplineEmitter>();
    TestTrue(TEXT("Audio properties serialize without editor fields"), CopyWithoutEditorProperties(Imported->AudioSource, RuntimeCopy->AudioSource));
    TestTrue(TEXT("Spline properties serialize without editor fields"), CopyWithoutEditorProperties(Imported->Spline, RuntimeCopy->Spline));
    RuntimeCopy->Shape = Imported->Shape;
    // This in-memory property reader bypasses package load/component registration.
    RuntimeCopy->Spline->UpdateComponentToWorld();
    RuntimeCopy->SetActorTransform(Imported->GetActorTransform());
    RuntimeCopy->Spline->UpdateSpline();
    TestTrue(TEXT("Filtered local spline data unchanged"), RuntimeCopy->Spline->SplineCurves == Imported->Spline->SplineCurves);
    TestTrue(TEXT("Filtered transform restored"), RuntimeCopy->GetActorTransform().Equals(Imported->GetActorTransform()));
    TestTrue(TEXT("Source path stripped"), RuntimeCopy->AudioSource->GeometryImportSource.IsEmpty());
    TestFalse(TEXT("Import batch stripped"), RuntimeCopy->AudioSource->GeometryImportBatch.IsValid());
    TestTrue(TEXT("Digest stripped"), RuntimeCopy->AudioSource->GeometryImportDigest.IsEmpty());
    TestEqual(TEXT("Night event survives filtering"), RuntimeCopy->AudioSource->GetProfile()->NightPlayEvent, Request.Profile.NightPlayEvent);
    World->EditorDestroyActor(Guide, true);
    for (const FVector Listener : {FVector(0), FVector(300, 400, 500), FVector(-1000, 2000, -100)})
    {
        FVector Expected;
        FVector Actual;
        float ExpectedDistance = 0;
        float ActualDistance = 0;
        float Blend = 0;
        TestTrue(TEXT("Original query valid"), Imported->QueryListener(Listener, 8, Expected, ExpectedDistance, Blend));
        TestTrue(TEXT("Filtered query valid"), RuntimeCopy->QueryListener(Listener, 8, Actual, ActualDistance, Blend));
        TestTrue(TEXT("Filtered position unchanged"), Actual.Equals(Expected, .001));
        TestEqual(TEXT("Filtered distance unchanged"), ActualDistance, ExpectedDistance);
    }
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryImportLabelTest, "Wanderer.Audio.GeometryImport.Label", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeometryImportLabelTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Guide = MakeGuide(World, true);
    if (!TestNotNull(TEXT("Guide"), Guide)) return false;
    Guide->SetActorLabel(TEXT("Fit_lake_InteractiveLakeSurface_1"));
    auto* Target = FInteriorFitGeometryImportOperation::CreatePlaceholder(Guide);
    if (!TestNotNull(TEXT("Placeholder"), Target)) return false;
    TestEqual(TEXT("Audio name describes original surface"), Target->GetActorLabel(), FString(TEXT("AUDIO_SPLINE_InteractiveLakeSurface")));
    Target->SetActorLabel(TEXT("MyLakeAudio"));
    TestEqual(TEXT("Update succeeds"), FInteriorFitGeometryImportOperation::Update(Guide, Target, EGeometryImportTransform::AdoptSource).Applied, 1);
    TestEqual(TEXT("Update preserves user label"), Target->GetActorLabel(), FString(TEXT("MyLakeAudio")));
    auto* Area = World->SpawnActor<AInteriorFitArea>();
    Area->SetSurfaceCells({FVector::ZeroVector});
    Area->SetActorLabel(TEXT("Fit_area_Snow_12_1"));
    auto* AreaTarget = FInteriorFitGeometryImportOperation::CreatePlaceholder(Area);
    if (TestNotNull(TEXT("Area placeholder"), AreaTarget))
        TestEqual(TEXT("Original numeric suffix preserved"), AreaTarget->GetActorLabel(), FString(TEXT("AUDIO_AREA_Snow_12")));
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
#endif
