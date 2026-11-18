#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/InteriorFitAudioSyncService.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "AkAudioEvent.h"
#include "Components/LineBatchComponent.h"
#include "Components/SplineComponent.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "InteriorFitArea.h"
#include "InteriorFitLibrary.h"
#include "InteriorFitSplineGuide.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"

namespace
{
AInteriorFitArea* MakeArea(UWorld* World, FName Group, float Offset)
{
    auto* Area = World->SpawnActor<AInteriorFitArea>();
    Area->Tags.Add(Group);
    Area->SetSurfaceCells({FVector(Offset, 0, 0)});
    return Area;
}
TArray<AWandererAudioAreaEmitter*> AudioAreas(UWorld* World)
{
    TArray<AWandererAudioAreaEmitter*> Actors;
    for (TActorIterator<AWandererAudioAreaEmitter> It(World); It; ++It) Actors.Add(*It);
    return Actors;
}
UWandererAudioSourceComponent* GetAudioSource(AActor* Actor)
{
    return Actor->FindComponentByClass<UWandererAudioSourceComponent>();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFAudioSyncRevisionTest, "Wanderer.Audio.IFSync.StableRevision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIFAudioSyncRevisionTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AInteriorFitArea* Source = MakeArea(World, TEXT("InteriorFitProof_RevisionTest"), 0);
    MakeArea(World, TEXT("InteriorFitProof_RevisionOther"), 1000);
    const auto Service = MakeShared<FInteriorFitAudioSyncService>();
    Service->Start();
    Service->PollWorld(*World, 0);
    TestEqual(TEXT("First sample cannot write"), AudioAreas(World).Num(), 0);
    Service->PollWorld(*World, 1);
    TestEqual(TEXT("Both startup samples collected"), Service->SampleCount, 2);
    TestEqual(TEXT("Stable startup enters Reconcile once"), Service->ReconcileCount, 1);
    if (!TestEqual(TEXT("Second sample creates both targets"), AudioAreas(World).Num(), 2))
    {
        return false;
    }
    TestEqual(TEXT("Both groups processed initially"), Service->ReconciledGroupCount, 2);
    for (int32 Time = 2; Time <= 10; ++Time)
    {
        Service->PollWorld(*World, Time);
    }
    TestEqual(TEXT("Stable one-second ticks reuse the sample"), Service->SampleCount, 2);
    Service->PollWorld(*World, 11);
    TestEqual(TEXT("An identical fallback sample skips Reconcile"), Service->ReconcileCount, 1);

    Source->SetSurfaceCells({FVector(500, 0, 0)});
    Service->PollWorld(*World, 12);
    TestEqual(TEXT("Silent plugin cell replacement is still waiting for fallback"), Service->SampleCount, 3);
    Service->PollWorld(*World, 21);
    TestEqual(TEXT("A changed fallback sample still needs confirmation"), Service->ReconcileCount, 1);
    Service->PollWorld(*World, 22);
    TestEqual(TEXT("Only the changed group reconciles"), Service->ReconciledGroupCount, 3);

    auto Targets = AudioAreas(World);
    AWandererAudioAreaEmitter** EditedTarget = Targets.FindByPredicate([Source](AWandererAudioAreaEmitter* Target)
    {
        return GetAudioSource(Target)->GeometryImportSource == Source->GetPathName();
    });
    if (!TestTrue(TEXT("The edited source still has its target"), EditedTarget != nullptr))
    {
        return false;
    }
    TestTrue(TEXT("Confirmed silent edit copies the new cells"),
        (*EditedTarget)->GetSurfaceCells()[0].Equals(FVector(500, 0, 0)));
    (*EditedTarget)->Destroy();
    Service->PollWorld(*World, 23);
    TestEqual(TEXT("Target deletion waits for two samples"), AudioAreas(World).Num(), 1);
    Service->PollWorld(*World, 24);
    TestEqual(TEXT("Target deletion re-pairs despite unchanged source geometry"), AudioAreas(World).Num(), 2);
    TestEqual(TEXT("Only the deleted target's group is rebuilt"), Service->ReconciledGroupCount, 4);
    Service->Stop();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFAudioSyncSingleTest, "Wanderer.Audio.IFSync.Single",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIFAudioSyncSingleTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    const FName Group(TEXT("InteriorFitProof_AutoSyncTest"));
    auto* Source = MakeArea(World, Group, 0);
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FAudioSourceRequest Request;
    Request.Action = EAudioSourceAction::ImportGeometry;
    Request.World = World;
    Request.Targets = {Source};
    Request.TablePath = FSoftObjectPath(Table);
    Request.ProfileRowName = TEXT("KEEP_SOUND");
    Request.Profile.PlayEvent = NewObject<UAkAudioEvent>();
    const FString PreviewShape =
        TEXT("{\"origin\":[0,0,0],\"cells\":[[0,0,0]],\"cell_size\":40}");
    TestTrue(TEXT("Plugin draws Area preview"),
        UInteriorFitLibrary::DrawAreaPreview(World, PreviewShape));
    TestTrue(TEXT("Plugin draws separate spline preview"),
        UInteriorFitLibrary::DrawSplinePreviewWithStyle(World,
            {FVector::ZeroVector, FVector(100.0, 0.0, 0.0)}, EInteriorFitSplineClosure::Open,
            EInteriorFitSplineBudgetStatus::WithinBudget));
    ULineBatchComponent* PreviewLines = World->PersistentLineBatcher;
    if (!TestNotNull(TEXT("Editor preview line batcher"), PreviewLines))
    {
        return false;
    }
    const int32 LinesBefore = PreviewLines->BatchedLines.Num();
    FAudioSourceRequest Invalid = Request;
    Invalid.Profile.PlayEvent.Reset();
    TestEqual(TEXT("Invalid event prevents handoff"),
        FAudioSourceAuthoringOperation::Apply(Invalid).Applied, 0);
    TestEqual(TEXT("Failed handoff retains previews"),
        PreviewLines->BatchedLines.Num(), LinesBefore);
    FInteriorFitAudioSyncService::Reconcile(World);
    const TArray<AWandererAudioAreaEmitter*> Unconfigured = AudioAreas(World);
    if (!TestEqual(TEXT("Reconcile creates exactly one Area"), Unconfigured.Num(), 1))
    {
        return false;
    }
    TestNull(TEXT("Automatic creation does not choose an Event"),
        GetAudioSource(Unconfigured[0])->GetProfile());
    TestEqual(TEXT("Automatic creation does not write the table"), Table->GetRowMap().Num(), 0);
    const int32 LinesAfter = PreviewLines->BatchedLines.Num();
    TestTrue(TEXT("Automatic creation clears only the Area batch"),
        LinesAfter > 0 && LinesAfter < LinesBefore);
    TestEqual(TEXT("Explicit import creates configured audio"),
        FAudioSourceAuthoringOperation::Apply(Request).Applied, 1);
    TestTrue(TEXT("Area cleanup is already complete"),
        UInteriorFitLibrary::DrawAreaPreview(World, FString()));
    TestEqual(TEXT("Clearing Area again does not affect the remaining spline"),
        PreviewLines->BatchedLines.Num(), LinesAfter);
    TestEqual(TEXT("Repeated import rejects a duplicate"),
        FAudioSourceAuthoringOperation::Apply(Request).Applied, 0);
    auto Targets = AudioAreas(World);
    if (!TestEqual(TEXT("One configured output"), Targets.Num(), 1))
    {
        return false;
    }
    auto* Target = Targets[0];
    TestEqual(TEXT("Configuration reuses the automatically created Actor"),
        Target, Unconfigured[0]);
    auto* Component = GetAudioSource(Target);
    TestEqual(TEXT("Explicit row retained"), Component->Profile.RowName, Request.ProfileRowName);
    Component->ResourceGroup = TEXT("KeepGroup");
    for (int32 Index = 0; Index < 3; ++Index) FInteriorFitAudioSyncService::Reconcile(World);
    TestEqual(TEXT("Repeated scans do not duplicate"), AudioAreas(World).Num(), 1);
    Source->Destroy();
    FInteriorFitAudioSyncService::Reconcile(World);
    TestFalse(TEXT("Missing geometry suspends playback"), Component->bEnabled);
    Source = MakeArea(World, Group, 750);
    FInteriorFitAudioSyncService::Reconcile(World);
    TestEqual(TEXT("Regeneration reuses target"), AudioAreas(World)[0], Target);
    TestTrue(TEXT("Regeneration copies geometry"), Target->GetSurfaceCells()[0].Equals(FVector(750, 0, 0)));
    TestTrue(TEXT("Enabled state restored"), Component->bEnabled);
    TestEqual(TEXT("Event preserved"), Component->GetProfile()->PlayEvent, Request.Profile.PlayEvent);
    TestEqual(TEXT("Resource group preserved"), Component->ResourceGroup, FName(TEXT("KeepGroup")));
    Component->bGeometryAutoSync = false;
    Source->SetSurfaceCells({FVector(1500, 0, 0)});
    FInteriorFitAudioSyncService::Reconcile(World);
    TestTrue(TEXT("Opt out keeps geometry"), Target->GetSurfaceCells()[0].Equals(FVector(750, 0, 0)));
    TestEqual(TEXT("Opt out does not create extra target"), AudioAreas(World).Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFAudioAutoSplineTest, "Wanderer.Audio.IFSync.AutoSpline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIFAudioAutoSplineTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AInteriorFitSplineGuide* Source = World->SpawnActor<AInteriorFitSplineGuide>();
    const TArray<FVector> Points = {FVector::ZeroVector, FVector(600.0, 0.0, 0.0)};
    Source->Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local);
    TestTrue(TEXT("Plugin draws Spline preview"),
        UInteriorFitLibrary::DrawSplinePreviewWithStyle(World, Points,
            EInteriorFitSplineClosure::Open, EInteriorFitSplineBudgetStatus::WithinBudget));
    ULineBatchComponent* Lines = World->PersistentLineBatcher;
    if (!TestNotNull(TEXT("Spline preview batcher"), Lines))
    {
        return false;
    }
    const int32 Before = Lines->BatchedLines.Num();
    FInteriorFitAudioSyncService::Reconcile(World);
    FInteriorFitAudioSyncService::Reconcile(World);
    int32 Count = 0;
    for (TActorIterator<AWandererAudioSplineEmitter> It(World); It; ++It)
    {
        ++Count;
        TestEqual(TEXT("Spline points copied"), It->Spline->GetNumberOfSplinePoints(),
            Points.Num());
        TestNull(TEXT("Spline awaits Event configuration"), It->AudioSource->GetProfile());
    }
    TestEqual(TEXT("Exactly one audio Spline after repeated scans"), Count, 1);
    TestTrue(TEXT("Spline preview cleared"), Lines->BatchedLines.Num() < Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFAudioSyncBranchesTest, "Wanderer.Audio.IFSync.AmbiguousBranches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIFAudioSyncBranchesTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    const FName Group(TEXT("InteriorFitProof_Branches"));
    auto* First = MakeArea(World, Group, 0);
    auto* Second = MakeArea(World, Group, 1000);
    FInteriorFitGeometryImportOperation::CreatePlaceholder(First);
    FInteriorFitGeometryImportOperation::CreatePlaceholder(Second);
    auto Targets = AudioAreas(World);
    if (!TestEqual(TEXT("Initial branches each get a placeholder"), Targets.Num(), 2)) return false;
    First->Destroy(); Second->Destroy();
    First = MakeArea(World, Group, 200);
    Second = MakeArea(World, Group, 1300);
    FInteriorFitAudioSyncService::Reconcile(World);
    TestEqual(TEXT("Uncertain replacement creates no extra copies"), AudioAreas(World).Num(), 2);
    TestTrue(TEXT("Ambiguous targets suspended"), !GetAudioSource(Targets[0])->bEnabled && !GetAudioSource(Targets[1])->bEnabled);
    FInteriorFitGeometryImportOperation::Update(First, Targets[0], EGeometryImportTransform::AdoptSource);
    FInteriorFitGeometryImportOperation::Update(Second, Targets[1], EGeometryImportTransform::AdoptSource);
    FInteriorFitAudioSyncService::Reconcile(World);
    TestTrue(TEXT("Explicit repair resumes both"), GetAudioSource(Targets[0])->bEnabled && GetAudioSource(Targets[1])->bEnabled);
    TestEqual(TEXT("Repair does not create duplicates"), AudioAreas(World).Num(), 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFAudioSyncDuplicateTest, "Wanderer.Audio.IFSync.DuplicateAndLegacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIFAudioSyncDuplicateTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Source = MakeArea(World, TEXT("InteriorFitProof_Duplicate"), 0);
    FInteriorFitGeometryImportOperation::CreatePlaceholder(Source);
    auto* Extra = FInteriorFitGeometryImportOperation::CreatePlaceholder(Source);
    FInteriorFitAudioSyncService::Reconcile(World);
    TestEqual(TEXT("Duplicate binding does not multiply"), AudioAreas(World).Num(), 2);
    for (auto* Target : AudioAreas(World)) TestFalse(TEXT("Conflicting copies are muted"), GetAudioSource(Target)->bEnabled);
    GetAudioSource(Extra)->GeometryImportGroup.Reset();
    GetAudioSource(Extra)->GeometryImportSource = TEXT("/Missing/OldSource");
    FInteriorFitAudioSyncService::Reconcile(World);
    TestTrue(TEXT("Unresolvable legacy record remains suspended"), GetAudioSource(Extra)->bGeometrySyncSuspended);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFAudioPlaceholderEditTest, "Wanderer.Audio.IFSync.ConfigurePlaceholder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIFAudioPlaceholderEditTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Source = MakeArea(World, TEXT("InteriorFitProof_Configure"), 0);
    auto* Target = FInteriorFitGeometryImportOperation::CreatePlaceholder(Source);
    if (!TestNotNull(TEXT("Placeholder created"), Target)) return false;
    auto* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FAudioSourceRequest Request;
    Request.Action = EAudioSourceAction::ImportGeometry;
    Request.World = World;
    Request.TablePath = FSoftObjectPath(Table);
    Request.Targets = {Source};
    Request.Profile.PlayEvent = NewObject<UAkAudioEvent>();
    Request.ChangedFields.Add(TEXT("PlayEvent"));
    const auto Result = FAudioSourceAuthoringOperation::Apply(Request);
    if (!TestEqual(TEXT("First Event assigned without Apply All"), Result.Applied, 1)) return false;
    TestEqual(TEXT("Existing placeholder converted in place"), AudioAreas(World).Num(), 1);
    TestEqual(TEXT("Mode inferred from geometry"), GetAudioSource(Target)->GetProfile()->Mode, EWandererWorldAudioMode::Area);
    return true;
}

#endif
