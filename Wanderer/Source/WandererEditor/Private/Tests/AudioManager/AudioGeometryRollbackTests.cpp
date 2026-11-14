#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Tests/AutomationEditorCommon.h"

#include "AkAudioEvent.h"
#include "InteriorFitArea.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryCommitOperation.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryPlanBuilder.h"
#include "Features/AudioManager/InteriorFitAudioSyncService.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

namespace
{
    struct FGeometryFixture
    {
        UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
        TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
        TStrongObjectPtr<UAkAudioEvent> Event{NewObject<UAkAudioEvent>()};
        FAudioSourceRequest Request;
        FDelegateHandle TableHandle;

        FGeometryFixture()
        {
            Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
            Request.World = World;
            Request.TablePath = FSoftObjectPath(Table.Get());
            Request.Profile.PlayEvent = Event.Get();
            Request.Action = EAudioSourceAction::ImportGeometry;
        }

        ~FGeometryFixture()
        {
            Table->OnDataTableChanged().Remove(TableHandle);
        }

        AInteriorFitArea* AddArea(float InX = 0.0f)
        {
            AInteriorFitArea* Area = World->SpawnActor<AInteriorFitArea>();
            Area->SetSurfaceCells({FVector(InX, 0, 0)});
            Area->Tags.Add(TEXT("InteriorFitProof_Rollback"));
            Request.Targets.Add(Area);
            return Area;
        }

        int32 Count() const
        {
            int32 Result = 0;
            for (TActorIterator<AWandererAudioAreaEmitter> It(World); It; ++It)
            {
                ++Result;
            }
            return Result;
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryStalePlanTest,
                                 "Wanderer.Audio.GeometryRollback.StalePlan",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FGeometryStalePlanTest::RunTest(const FString& InParameters)
{
    FGeometryFixture Fixture;
    AInteriorFitArea* Source = Fixture.AddArea();
    FAudioGeometryPlan Plan;
    TestTrue(TEXT("Preflight succeeds"),
             FAudioGeometryPlanBuilder::Import(Fixture.Request, Plan).IsEmpty());
    TestEqual(TEXT("Preflight creates no actors"), Fixture.Count(), 0);
    TestEqual(TEXT("Preflight writes no rows"), Fixture.Table->GetRowNames().Num(), 0);
    Source->SetSurfaceCells({FVector(900, 0, 0)});
    FAudioSourceResult Result;
    FAudioGeometryCommitOperation Commit(Plan);
    Commit.Execute(Result);
    TestEqual(TEXT("Stale source rejected"), Result.Applied, 0);
    TestTrue(TEXT("Failure explained"), !Result.Errors.IsEmpty());
    TestEqual(TEXT("No actor created"), Fixture.Count(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryRowRollbackTest,
                                 "Wanderer.Audio.GeometryRollback.ForeignRowAndUndo",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FGeometryRowRollbackTest::RunTest(const FString& InParameters)
{
    FGeometryFixture Fixture;
    AInteriorFitArea* Source = Fixture.AddArea();
    const FVector Original = Source->GetActorLocation();
    {
        const FScopedTransaction Previous(FText::FromString(TEXT("Previous user edit")));
        Source->Modify();
        Source->GetRootComponent()->Modify();
        Source->SetActorLocation(FVector(200, 0, 0));
    }
    const TSharedRef<bool> Active = MakeShared<bool>(true);
    Fixture.Request.CanContinue = [Active] { return *Active; };
    const TWeakObjectPtr<UDataTable> Table = Fixture.Table.Get();
    Fixture.TableHandle = Fixture.Table->OnDataTableChanged().AddLambda(
        [Active, Table]
        {
            if (*Active)
            {
                *Active = false;
                Table->AddRow(TEXT("ExternalRow"), FWandererWorldAudioProfileRow());
            }
        });
    const FAudioSourceResult Result = FInteriorFitGeometryImportOperation::Create(Fixture.Request);
    TestEqual(TEXT("Cancelled batch not committed"), Result.Applied, 0);
    TestEqual(TEXT("Created actor compensated"), Fixture.Count(), 0);
    TestEqual(TEXT("Only unrelated row remains"), Fixture.Table->GetRowNames().Num(), 1);
    TestTrue(TEXT("External row preserved"),
             Fixture.Table->GetRowMap().Contains(TEXT("ExternalRow")));
    GEditor->UndoTransaction();
    TestTrue(TEXT("Prior user undo survives cancellation"),
             Source->GetActorLocation().Equals(Original));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometrySourceDestroyedTest,
                                 "Wanderer.Audio.GeometryRollback.DestroyedSource",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FGeometrySourceDestroyedTest::RunTest(const FString& InParameters)
{
    FGeometryFixture Fixture;
    const TWeakObjectPtr<AActor> Source = Fixture.AddArea();
    Fixture.TableHandle = Fixture.Table->OnDataTableChanged().AddLambda(
        [Source]
        {
            if (Source.IsValid() && !Source->IsActorBeingDestroyed())
            {
                Source->Destroy();
            }
        });
    const FAudioSourceResult Result = FInteriorFitGeometryImportOperation::Create(Fixture.Request);
    TestEqual(TEXT("Destroyed source cancels creation"), Result.Applied, 0);
    TestEqual(TEXT("No orphan emitter"), Fixture.Count(), 0);
    TestEqual(TEXT("No orphan profile"), Fixture.Table->GetRowNames().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryUpdateCancelledTest,
                                 "Wanderer.Audio.GeometryRollback.UpdateCancelled",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FGeometryUpdateCancelledTest::RunTest(const FString& InParameters)
{
    FGeometryFixture Fixture;
    AInteriorFitArea* Source = Fixture.AddArea();
    AWandererAudioAreaEmitter* Target = Cast<AWandererAudioAreaEmitter>(
        FInteriorFitGeometryImportOperation::CreatePlaceholder(Source));
    if (!TestNotNull(TEXT("Placeholder created"), Target))
    {
        return false;
    }
    const FString OriginalDigest = Target->AudioSource->GeometryImportDigest;
    Source->SetSurfaceCells({FVector(900, 0, 0)});
    const TWeakObjectPtr<AWandererAudioAreaEmitter> TargetWeakPtr = Target;
    const FAudioSourceResult Result = FInteriorFitGeometryImportOperation::Update(
        Source, Target, EGeometryImportTransform::AdoptSource, [TargetWeakPtr]
        { return TargetWeakPtr.IsValid() && TargetWeakPtr->GetSurfaceCells()[0].X == 0.0; });
    TestEqual(TEXT("Cancellation after geometry write"), Result.Applied, 0);
    TestTrue(TEXT("Old geometry restored"),
             Target->GetSurfaceCells()[0].Equals(FVector::ZeroVector));
    TestEqual(TEXT("Old import identity preserved"), Target->AudioSource->GeometryImportDigest,
              OriginalDigest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryGroupCancelledTest,
                                 "Wanderer.Audio.GeometryRollback.GroupCancelled",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FGeometryGroupCancelledTest::RunTest(const FString& InParameters)
{
    FGeometryFixture Fixture;
    AInteriorFitArea* First = Fixture.AddArea();
    AInteriorFitArea* Second = Fixture.AddArea(1000.0f);
    if (!TestEqual(TEXT("Both audio outputs are configured explicitly"),
            FAudioSourceAuthoringOperation::Apply(Fixture.Request).Applied, 2))
    {
        return false;
    }
    TArray<TWeakObjectPtr<AWandererAudioAreaEmitter>> Targets;
    for (TActorIterator<AWandererAudioAreaEmitter> It(Fixture.World); It; ++It)
    {
        Targets.Add(*It);
    }
    First->SetSurfaceCells({FVector(100, 0, 0)});
    Second->SetSurfaceCells({FVector(1100, 0, 0)});
    FInteriorFitAudioSyncService::Reconcile(
        Fixture.World,
        [Targets]
        {
            for (const TWeakObjectPtr<AWandererAudioAreaEmitter>& Target : Targets)
            {
                if (!Target.IsValid() || FMath::Fmod(Target->GetSurfaceCells()[0].X, 1000.0) != 0.0)
                {
                    return false;
                }
            }
            return true;
        });
    TestEqual(TEXT("No duplicate audio outputs"), Fixture.Count(), 2);
    for (const TWeakObjectPtr<AWandererAudioAreaEmitter>& Target : Targets)
    {
        TestEqual(TEXT("Group geometry compensated"),
                  FMath::Fmod(Target->GetSurfaceCells()[0].X, 1000.0), 0.0);
        TestFalse(TEXT("Cancellation does not invent a missing-source suspension"),
                  Target->AudioSource->bGeometrySyncSuspended);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryShapeConflictTest,
                                 "Wanderer.Audio.GeometryRollback.ExternalShape",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FGeometryShapeConflictTest::RunTest(const FString& InParameters)
{
    FGeometryFixture Fixture;
    UClass* GuideClass
        = LoadClass<AActor>(nullptr, TEXT("/Script/InteriorFitEditor.InteriorFitSplineGuide"));
    if (!TestNotNull(TEXT("IF Guide class"), GuideClass))
    {
        return false;
    }
    AActor* Guide = Fixture.World->SpawnActor<AActor>(GuideClass);
    USplineComponent* GuideSpline = Guide->FindComponentByClass<USplineComponent>();
    GuideSpline->SetClosedLoop(false);
    AWandererAudioSplineEmitter* Target = Cast<AWandererAudioSplineEmitter>(
        FInteriorFitGeometryImportOperation::CreatePlaceholder(Guide));
    if (!TestNotNull(TEXT("Spline placeholder"), Target))
    {
        return false;
    }
    Guide->SetActorLocation(FVector(400, 100, 0));
    const TWeakObjectPtr<AWandererAudioSplineEmitter> TargetWeakPtr = Target;
    const FDelegateHandle Handle = Target->Spline->TransformUpdated.AddLambda(
        [TargetWeakPtr](USceneComponent*, EUpdateTransformFlags, ETeleportType)
        {
            if (TargetWeakPtr.IsValid())
            {
                TargetWeakPtr->Shape = EWandererAmbientSplineShape::Lake;
            }
        });
    const FAudioSourceResult Result = FInteriorFitGeometryImportOperation::Update(
        Guide, Target, EGeometryImportTransform::AdoptSource);
    Target->Spline->TransformUpdated.Remove(Handle);
    TestEqual(TEXT("External shape invalidates commit"), Result.Applied, 0);
    TestTrue(TEXT("External shape retained"), Target->Shape == EWandererAmbientSplineShape::Lake);
    TestTrue(TEXT("Incomplete compensation is reported"), Result.Summary().Contains(TEXT("外部")));
    TestEqual(TEXT("Incomplete compensation leaves its own Undo entry"),
              GEditor->Trans->GetUndoContext().Title.ToString(),
              FString(TEXT("Edit IF audio geometry")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometrySpawnLockTest,
                                 "Wanderer.Audio.GeometryRollback.SpawnLocksLevel",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FGeometrySpawnLockTest::RunTest(const FString& InParameters)
{
    FGeometryFixture Fixture;
    AInteriorFitArea* Source = Fixture.AddArea();
    const TWeakObjectPtr<ULevel> Level = Source->GetLevel();
    const bool bWasLocked = Level->bLocked;
    const FDelegateHandle Handle
        = Fixture.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
            [Level](AActor* InActor)
            {
                if (InActor->IsA<AWandererAudioAreaEmitter>() && Level.IsValid())
                {
                    Level->bLocked = true;
                }
            }));
    const FAudioSourceResult Result = FInteriorFitGeometryImportOperation::Create(Fixture.Request);
    Fixture.World->RemoveOnActorSpawnedHandler(Handle);
    Level->bLocked = bWasLocked;
    TestEqual(TEXT("Creation in newly locked level rejected"), Result.Applied, 0);
    TestEqual(TEXT("New actor removed"), Fixture.Count(), 0);
    TestEqual(TEXT("No profile published"), Fixture.Table->GetRowNames().Num(), 0);
    return true;
}
#endif
