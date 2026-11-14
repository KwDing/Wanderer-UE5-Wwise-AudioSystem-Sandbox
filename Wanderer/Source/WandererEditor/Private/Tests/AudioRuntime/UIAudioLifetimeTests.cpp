#include "Misc/AutomationTest.h"
#include "Framework/WandererGameState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AkAudioEvent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/WandererPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Framework/RadioControlModel.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/WandererUIAudio.h"
#include "Data/Audio/WandererUIAudioProfile.h"
#include "Data/Audio/WandererUIAudioSettings.h"
#include "Systems/Audio/WandererUIAudioSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUIAudioCacheLifetimeTest, "Wanderer.UIAudio.CacheSurvivesGCAndReleases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUIAudioCacheLifetimeTest::RunTest(const FString& Parameters)
{
    // Test roots own only the instance/subsystem, never the profile graph under test.
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
    TStrongObjectPtr<UWandererUIAudioSubsystem> Audio(NewObject<UWandererUIAudioSubsystem>(Instance.Get()));
    auto* Child = NewObject<UWandererUIAudioProfile>();
    auto* Parent = NewObject<UWandererUIAudioProfile>();
    auto* Table = NewObject<UDataTable>();
    auto* Event = NewObject<UAkAudioEvent>();
    Child->ParentProfile = Parent;
    Parent->ActionTables.Add(Table);
    Table->RowStruct = FWandererUIAudioActionRow::StaticStruct();
    FWandererUIAudioActionRow Row;
    Row.InlineAudioActions.AddDefaulted_GetRef().Event = Event;
    Table->AddRow(TEXT("Choose"), Row);
    const TWeakObjectPtr<UWandererUIAudioProfile> WeakChild(Child);
    const TWeakObjectPtr<UWandererUIAudioProfile> WeakParent(Parent);
    const TWeakObjectPtr<UDataTable> WeakTable(Table);
    const TWeakObjectPtr<UAkAudioEvent> WeakEvent(Event);
    auto* Settings = GetMutableDefault<UWandererUIAudioSettings>();
    TGuardValue<TSoftObjectPtr<UWandererUIAudioProfile>> DefaultGuard(Settings->DefaultProfile, Child);
    TestTrue(TEXT("Runtime subsystem resolves transient profile"), Audio->ResolveProfile(nullptr) == Child);
    for (int32 Index = 0; Index < 8; ++Index)
    {
        CollectGarbage(RF_NoFlags, true);
        if (!TestTrue(TEXT("Cache keeps entire inherited action graph alive"), WeakChild.IsValid() && WeakParent.IsValid() && WeakTable.IsValid() && WeakEvent.IsValid())) return false;
        TestTrue(TEXT("GC keeps same cached identity"), Audio->ResolveProfile(nullptr) == WeakChild.Get());
        FString Error;
        TestTrue(TEXT("Inherited action remains readable"), WeakChild->FindAction(TEXT("Choose"), Row, Error));
        TestTrue(TEXT("Event dependency remains the same"), Row.InlineAudioActions[0].Event == WeakEvent.Get());
    }
    Audio->Deinitialize();
    Audio->Deinitialize();
    TestNull(TEXT("Stopped subsystem refuses profile reload"), Audio->ResolveProfile(nullptr));
    CollectGarbage(RF_NoFlags, true);
    TestFalse(TEXT("Released cache does not retain child"), WeakChild.IsValid());
    TestFalse(TEXT("Released cache does not retain parent"), WeakParent.IsValid());
    TestFalse(TEXT("Released cache does not retain table"), WeakTable.IsValid());
    TestFalse(TEXT("Released cache does not retain Event"), WeakEvent.IsValid());
    return true;
}

namespace
{
AWandererPlayerController* GetSpeakerTestPlayer()
{
    return GEditor && GEditor->PlayWorld ? Cast<AWandererPlayerController>(GEditor->PlayWorld->GetFirstPlayerController()) : nullptr;
}

void ExerciseSpeakerCycle(FAutomationTestBase& Test, int32 Index)
{
    auto* Player = GetSpeakerTestPlayer();
    TArray<UUserWidget*> Widgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Player, Widgets, UUserWidget::StaticClass(), true);
    UUserWidget* Widget = nullptr;
    for (UUserWidget* Candidate : Widgets)
    {
        if (Candidate->GetClass()->GetFName() == TEXT("WandererSpeakerWidget"))
        {
            Widget = Candidate;
            break;
        }
    }
    if (!Test.TestTrue(TEXT("Speaker UI and controller exist"), Player && Widget && Player->GetPawn())) return;
    URadioControlModel* Model = Player->GetRadioControlModel();
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::OpenPanel;
    Model->Execute(Request);
    Request.Action = ERadioControlAction::CursorLeft;
    Model->Execute(Request);
    const FVector Position = Player->GetPawn()->GetActorLocation() + FVector(150, 0, 0);
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SpeakerGCTest), true, Player->GetPawn());
    if (!Test.TestTrue(TEXT("Find nearby speaker support"), Player->GetWorld()->LineTraceSingleByChannel(Hit, Position + FVector(0, 0, 100), Position - FVector(0, 0, 300), ECC_Visibility, Query))) return;
    Request.Action = ERadioControlAction::BeginPlacement;
    Request.Item = EWandererSpeakerItem::Radio;
    if (!Test.TestTrue(TEXT("Begin speaker placement"), Model->Execute(Request))) return;
    Request.Action = ERadioControlAction::PreviewSurface;
    Request.SurfacePoint = Hit.ImpactPoint;
    Request.SurfaceNormal = Hit.ImpactNormal;
    if (!Test.TestTrue(TEXT("Preview speaker support"), Model->Execute(Request))) return;
    Request.Action = ERadioControlAction::ConfirmPlacement;
    if (!Test.TestTrue(TEXT("Deploy speaker"), Model->Execute(Request))) return;
    auto* Audio = Player->GetGameInstance()->GetSubsystem<UWandererUIAudioSubsystem>();
    const TWeakObjectPtr<UWandererUIAudioProfile> Profile(Audio->ResolveProfile(Widget->GetClass()));
    CollectGarbage(RF_NoFlags, true);
    Test.TestTrue(TEXT("Live UI cache survives GC between deployment and recall"), Profile.IsValid());
    Test.TestTrue(TEXT("Retrieve button sound posts after GC"), UWandererUIAudio::PlayUIAction(Widget, EWandererUIAudioAction::Choose) != 0);
    Request.Action = Index % 2 == 0 ? ERadioControlAction::RetrieveAll : ERadioControlAction::RetrieveSelected;
    Test.TestTrue(TEXT("Retrieve command succeeds"), Model->Execute(Request));
    Test.TestEqual(TEXT("Inventory is empty after recall"), Model->GetSnapshot().Devices.Num(), 0);
    CollectGarbage(RF_NoFlags, true);
    Test.TestTrue(TEXT("Next button sound posts after destroyed actors are collected"), UWandererUIAudio::PlayUIAction(Widget, EWandererUIAudioAction::Cancel) != 0);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeakerRecallGCTest, "Wanderer.UIAudio.SpeakerRecallAcrossGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeakerRecallGCTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Start = FPlatformTime::Seconds()]()
    {
        auto* Player = GetSpeakerTestPlayer();
        const auto* State = Player ? Player->GetWorld()->GetGameState<AWandererGameState>() : nullptr;
        if (State && State->GetPlayState() == EWandererPlayState::Normal) return true;
        if (FPlatformTime::Seconds() - Start < 180) return false;
        AddError(TEXT("PIE did not finish loading within 180 seconds."));
        return true;
    }));
    for (int32 Index = 0; Index < 30; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Index]() { ExerciseSpeakerCycle(*this, Index); return true; }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.15f));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
