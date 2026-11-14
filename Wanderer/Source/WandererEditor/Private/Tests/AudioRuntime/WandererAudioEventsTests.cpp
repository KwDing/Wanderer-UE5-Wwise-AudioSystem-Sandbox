#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Core/WandererEventBusSubsystem.h"
#include "Systems/Audio/WandererAudioDiagnostics.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Widgets/Docking/SDockTab.h"

namespace WandererAudioEventsTests
{
    struct FSceneCapture
    {
        FSceneCapture()
        {
            Handle =
                UWandererEventBusSubsystem::OnEventObserved().AddRaw(this, &FSceneCapture::Observe);
        }
        ~FSceneCapture()
        {
            UWandererEventBusSubsystem::OnEventObserved().Remove(Handle);
        }
        void Observe(UWorld* World, const FWandererGameEventMessage& Message)
        {
            if (World && World->WorldType == EWorldType::PIE
                && Message.Type == EWandererGameEvent::EnterScene)
            {
                ++Count;
                Scene = Message.Scene;
            }
        }
        FDelegateHandle Handle;
        int32 Count = 0;
        FName Scene;
    };

    class FVerifyScene : public IAutomationLatentCommand
    {
      public:
        FVerifyScene(
            FAutomationTestBase* InTest, TSharedRef<FSceneCapture> InCapture, int32 InExpected)
            : Test(InTest), Capture(InCapture), Expected(InExpected)
        {
        }
        virtual bool Update() override
        {
            Test->TestEqual(TEXT("One scene event per PIE session"), Capture->Count, Expected);
            Test->TestFalse(TEXT("Scene has no PIE prefix"),
                Capture->Scene.ToString().Contains(TEXT("UEDPIE_")));
            Test->TestFalse(TEXT("Scene populated"), Capture->Scene.IsNone());
            return true;
        }

      private:
        FAutomationTestBase* Test;
        TSharedRef<FSceneCapture> Capture;
        int32 Expected;
    };
} // namespace WandererAudioEventsTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWandererSceneLifecycleTest,
    "Wanderer.AudioEvents.SceneLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWandererSceneLifecycleTest::RunTest(const FString& Parameters)
{
    using namespace WandererAudioEventsTests;
    TSharedRef<FSceneCapture> Capture = MakeShared<FSceneCapture>();
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyScene(this, Capture, 1));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyScene(this, Capture, 2));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
