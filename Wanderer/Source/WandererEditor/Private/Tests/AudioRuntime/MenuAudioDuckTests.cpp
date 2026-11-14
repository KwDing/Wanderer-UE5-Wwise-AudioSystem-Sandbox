#include "Misc/AutomationTest.h"
#include "Framework/WandererGameState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/WandererPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "EngineUtils.h"
#include "AkGameplayStatics.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "UI/WandererUIAudio.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Pawn.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

namespace
{
AWandererGameState* GetMenuTestState()
{
	return GEditor && GEditor->PlayWorld ? GEditor->PlayWorld->GetGameState<AWandererGameState>() : nullptr;
}

void PressEscape()
{
	const FKeyEvent Escape(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);
	FSlateApplication::Get().ProcessKeyDownEvent(Escape);
	FSlateApplication::Get().ProcessKeyUpEvent(Escape);
}

void CheckAudioDuck(FAutomationTestBase& Test, const char* Expected)
{
	auto* Engine = IWwiseSoundEngineAPI::Get();
	AkStateID Actual = AK_INVALID_UNIQUE_ID;
	if (Test.TestTrue(TEXT("Wwise Audio_Duck group is loaded"), Engine && Engine->Query->GetState("Audio_Duck", Actual) == AK_Success))
		Test.TestTrue(FString::Printf(TEXT("Audio_Duck is %s (actual %u)"), ANSI_TO_TCHAR(Expected), Actual), Actual == Engine->GetIDFromString(Expected));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuAudioDuckTest, "Wanderer.Menu.EscapeAndAudioDuck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMenuAudioDuckTest::RunTest(const FString& Parameters)
{
	auto Stop = FInputBindingManager::Get().FindCommandInContext(TEXT("PlayWorld"), TEXT("StopPlaySession"));
	TestTrue(FString::Printf(TEXT("PIE Stop chord should be Shift+Escape (actual %s)"), Stop ? *Stop->GetActiveChord(EMultipleKeyBindingIndex::Primary)->GetInputText().ToString() : TEXT("unregistered")), Stop && *Stop->GetActiveChord(EMultipleKeyBindingIndex::Primary) == FInputChord(EModifierKey::Shift, EKeys::Escape));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Start = FPlatformTime::Seconds()]()
	{
		auto* State = GetMenuTestState();
		if (State && State->GetPlayState() == EWandererPlayState::Normal)
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			return true;
		}
		if (FPlatformTime::Seconds() - Start < 180) return false;
		AddError(TEXT("Demo PIE did not reach Normal within 180 s."));
		return true;
	}));
	struct FCursorProbe { AkPlayingID PlayingID = 0; AkTimeMs PausedPosition = 0; };
	const auto Probe = MakeShared<FCursorProbe>();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		if (auto* State = GetMenuTestState())
			if (auto* Player = State->GetWorld()->GetFirstPlayerController())
				if (auto* Pawn = Player->GetPawn()) Pawn->SetActorLocation(FVector(-492, -5151, 110));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Probe]()
	{
		if (auto* State = GetMenuTestState())
		{
			for (TActorIterator<AWandererAudioAreaEmitter> It(State->GetWorld()); It; ++It)
			{
				auto* Source = It->AudioSource.Get();
				if (!Source || Source->Profile.RowName != TEXT("AMB_GRASS_DAY") || !Source->IsPlaying()) continue;
				Probe->PlayingID = Source->GetPlayingID();
			}
		}
		TestTrue(TEXT("Cursor probe observes existing grass voice"), Probe->PlayingID != 0);
		return true;
	}));
	for (int32 Repeat = 0; Repeat < 2; ++Repeat)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { PressEscape(); return true; }));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Probe]()
		{
			auto* State = GetMenuTestState();
			TestTrue(TEXT("Escape opened pause menu"), State && State->GetPlayState() == EWandererPlayState::PauseMenu && UGameplayStatics::IsGamePaused(State));
			CheckAudioDuck(*this, "UI_Only");
			TestEqual(TEXT("Paused cursor readable"), IWwiseSoundEngineAPI::Get()->GetSourcePlayPosition(Probe->PlayingID, &Probe->PausedPosition, false), AK_Success);
			UAkGameplayStatics::StartOutputCapture(TEXT("menu_ui_runtime.wav"));
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Probe]()
		{
			AkTimeMs Position = -1;
			TestEqual(TEXT("Paused cursor query succeeds"), IWwiseSoundEngineAPI::Get()->GetSourcePlayPosition(Probe->PlayingID, &Position, false), AK_Success);
			TestEqual(TEXT("Pause holds source cursor"), Position, Probe->PausedPosition);
			auto* State = GetMenuTestState();
			auto* Player = State ? State->GetWorld()->GetFirstPlayerController() : nullptr;
            TArray<UUserWidget*> Widgets;
            UWidgetBlueprintLibrary::GetAllWidgetsOfClass(State, Widgets, UUserWidget::StaticClass(), true);
            UUserWidget* Widget = nullptr;
            for (UUserWidget* Candidate : Widgets)
            {
                if (Candidate->GetOwningPlayer() == Player
                    && Candidate->GetClass()->GetFName() == TEXT("WandererMapWidget"))
                {
                    Widget = Candidate;
                    break;
                }
            }
			TestTrue(TEXT("Actual menu UI action posts while SFX is paused"), UWandererUIAudio::PlayUIAction(Widget, EWandererUIAudioAction::Choose) != 0);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
		{
			UAkGameplayStatics::StopOutputCapture();
			PressEscape();
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Probe]()
		{
			auto* State = GetMenuTestState();
			TestTrue(TEXT("Second Escape resumed same PIE world"), State && State->GetPlayState() == EWandererPlayState::Normal && !UGameplayStatics::IsGamePaused(State));
			CheckAudioDuck(*this, "None");
			AkTimeMs Position = -1;
			TestEqual(TEXT("Resumed cursor readable"), IWwiseSoundEngineAPI::Get()->GetSourcePlayPosition(Probe->PlayingID, &Position, false), AK_Success);
			TestTrue(TEXT("Resume advances same playing ID"), Position > Probe->PausedPosition + 300);
			return true;
		}));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Probe]()
	{
		auto* State = GetMenuTestState();
		auto* Player = State ? Cast<AWandererPlayerController>(State->GetWorld()->GetFirstPlayerController()) : nullptr;
		TestTrue(TEXT("Character selection opens"), Player && Player->OpenCharacterSelection());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		CheckAudioDuck(*this, "UI_Focus");
		PressEscape();
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		CheckAudioDuck(*this, "None");
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		if (auto* State = GetMenuTestState()) State->SetPlayState(EWandererPlayState::PauseMenu);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		auto* Engine = IWwiseSoundEngineAPI::Get();
		AkStateID Actual = AK_INVALID_UNIQUE_ID;
		TestTrue(TEXT("Audio_Duck remains queryable after PIE"), Engine && Engine->Query->GetState("Audio_Duck", Actual) == AK_Success);
		// Unloading PIE resources may reset the group to native state 0.
		TestTrue(TEXT("PIE teardown leaves a neutral mix"), Engine && (Actual == 0 || Actual == Engine->GetIDFromString("None")));
		return true;
	}));
	return true;
}
#endif

