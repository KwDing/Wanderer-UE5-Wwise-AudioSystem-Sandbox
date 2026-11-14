#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Tests/AutomationCommon.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Features/GameLog/GameLogModel.h"

namespace
{
    struct FDiagnosticTestScope
    {
        int32 Preset = FDiagnosticRecorder::GetPreset();
        bool bFootsteps = FDiagnosticRecorder::IsFootstepsEnabled();
        FString Focus = FDiagnosticRecorder::GetFocus();
        FDiagnosticTestScope()
        {
            FDiagnosticRecorder::Clear();
            FDiagnosticRecorder::SetFocus(FString());
        }
        ~FDiagnosticTestScope()
        {
            FDiagnosticRecorder::Clear();
            FDiagnosticRecorder::SetPreset(Preset);
            FDiagnosticRecorder::SetFootstepsEnabled(bFootsteps);
            FDiagnosticRecorder::SetFocus(Focus);
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiagnosticPolicyTest,
    "Wanderer.Diagnostics.Policy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDiagnosticPolicyTest::RunTest(const FString& InParameters)
{
    FDiagnosticTestScope Scope;
    FDiagnosticRecorder::SetPreset(0);
    int32 PayloadEvaluations = 0;
    WANDERER_DIAGNOSTIC(nullptr,
        EDiagnosticDomain::Audio,
        TEXT("disabled"),
        false,
        FString::FromInt(++PayloadEvaluations));
    TestEqual(TEXT("Disabled payload never evaluated"), PayloadEvaluations, 0);
    WANDERER_DIAGNOSTIC(nullptr, EDiagnosticDomain::Audio, TEXT("failure"), true, TEXT("reason"));
    TestEqual(
        TEXT("Problems captured without tab"), FDiagnosticRecorder::GetSnapshot().Records.Num(), 1);
    FDiagnosticRecorder::SetPreset(4);
    FDiagnosticRecorder::SetFootstepsEnabled(false);
    {
        FDiagnosticTraceScope Footstep(true);
        TestFalse(TEXT("Footsteps default silent"),
            FDiagnosticRecorder::IsEnabled(EDiagnosticDomain::Audio));
    }
    FDiagnosticRecorder::SetFocus(TEXT("OnlyThisActor"));
    TestFalse(TEXT("Unmatched sources excluded"),
        FDiagnosticRecorder::Admit(nullptr, EDiagnosticDomain::Audio, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiagnosticTraceTest,
    "Wanderer.Diagnostics.TraceAndJson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDiagnosticTraceTest::RunTest(const FString& InParameters)
{
    FDiagnosticTestScope Scope;
    FDiagnosticRecorder::SetPreset(4);
    const uint64 Before = FDiagnosticRecorder::GetTrace();
    {
        FDiagnosticTraceScope Outer;
        const uint64 Trace = FDiagnosticRecorder::GetTrace();
        TestTrue(TEXT("Request has identity"), Trace != 0);
        {
            FDiagnosticTraceScope Inner;
            TestEqual(
                TEXT("Synchronous handler shares origin"), FDiagnosticRecorder::GetTrace(), Trace);
            WANDERER_DIAGNOSTIC(nullptr,
                EDiagnosticDomain::Audio,
                TEXT("result"),
                false,
                TEXT("quote=\" newline=\n slash=\\"));
        }
    }
    TestEqual(TEXT("Scope restores predecessor"), FDiagnosticRecorder::GetTrace(), Before);
    const FDiagnosticSnapshot Snapshot = FDiagnosticRecorder::GetSnapshot();
    if (!TestEqual(TEXT("One record"), Snapshot.Records.Num(), 1))
    {
        return false;
    }
    TSharedPtr<FJsonObject> Parsed;
    TestTrue(TEXT("Machine readable JSON"),
        FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Snapshot.Records[0].Json), Parsed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiagnosticBoundsTest,
    "Wanderer.Diagnostics.Bounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDiagnosticBoundsTest::RunTest(const FString& InParameters)
{
    FDiagnosticTestScope Scope;
    FDiagnosticRecorder::SetPreset(4);
    IConsoleVariable* Rate =
        IConsoleManager::Get().FindConsoleVariable(TEXT("Wanderer.Diagnostic.MaxPerSecond"));
    const int32 PreviousRate = Rate->GetInt();
    ON_SCOPE_EXIT
    {
        Rate->Set(PreviousRate, ECVF_SetByConsole);
    };
    Rate->Set(2, ECVF_SetByConsole);
    WANDERER_DIAGNOSTIC(
        nullptr, EDiagnosticDomain::Audio, TEXT("pressure"), false, TEXT("first"));
    WANDERER_DIAGNOSTIC(
        nullptr, EDiagnosticDomain::Audio, TEXT("pressure"), false, TEXT("second"));
    FGameLogModel Model;
    Model.Refresh();
    const FString History = Model.GetText();
    const uint64 Revision = FDiagnosticRecorder::GetRevision();

    for (int32 Index = 0; Index < 18; ++Index)
    {
        WANDERER_DIAGNOSTIC(
            nullptr, EDiagnosticDomain::Audio, TEXT("pressure"), false, TEXT("payload"));
    }
    TestEqual(
        TEXT("Bounded per-second admission"), FDiagnosticRecorder::GetSnapshot().Records.Num(), 2);
    TestEqual(TEXT("Dropped records observable"),
        FDiagnosticRecorder::GetSnapshot().Suppressed,
        uint64(18));
    TestEqual(TEXT("Dropped records do not dirty the history"),
        FDiagnosticRecorder::GetRevision(), Revision);
    TestTrue(TEXT("GameLog refreshes the dropped counter"), Model.Refresh());
    TestTrue(TEXT("Dropped counter appears in GameLog status"),
        Model.GetStatus().Contains(TEXT("rate-dropped=18")));
    TestEqual(TEXT("Drop-only refresh leaves displayed history intact"), Model.GetText(), History);
    TestFalse(TEXT("Unchanged capture skips model refresh"), Model.Refresh());

    TestTrue(TEXT("Uncommitted transition retryable"),
        FDiagnosticRecorder::HasTransition(nullptr, TEXT("Rejected")));
    FDiagnosticRecorder::CommitTransition(nullptr, TEXT("Rejected"));
    TestFalse(TEXT("Committed transition suppressed"),
        FDiagnosticRecorder::HasTransition(nullptr, TEXT("Rejected")));
    FDiagnosticRecorder::Clear();
    for (int32 Index = 0; Index < 600; ++Index)
    {
        FDiagnosticRecorder::Record(
            nullptr, EDiagnosticDomain::Audio, TEXT("ring"), FString::FromInt(Index));
    }
    const FDiagnosticSnapshot Snapshot = FDiagnosticRecorder::GetSnapshot();
    TestEqual(TEXT("Ring bounded"), Snapshot.Records.Num(), 512);
    TestEqual(TEXT("Evictions visible"), Snapshot.Evicted, uint64(88));
    TestTrue(TEXT("Newest retained"), Snapshot.Records.Last().Json.Contains(TEXT("599")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiagnosticGameLogTest,
    "Wanderer.Diagnostics.GameLog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDiagnosticGameLogTest::RunTest(const FString& InParameters)
{
    FDiagnosticTestScope Scope;
    FDiagnosticRecorder::SetPreset(0);
    WANDERER_DIAGNOSTIC(
        nullptr, EDiagnosticDomain::Audio, TEXT("failure"), true, TEXT("BeforeTab"));
    FGameLogModel Model;
    Model.Refresh();
    TestTrue(TEXT("Late reader sees earlier problem"), Model.GetText().Contains(TEXT("BeforeTab")));
    TSharedPtr<SDockTab> Tab =
        FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("WandererGameLog")));
    TestTrue(TEXT("GameLog tab opens"), Tab.IsValid());
    if (Tab)
    {
        Tab->RequestCloseTab();
    }
    Model.SetPreset(2);
    TestTrue(TEXT("Spatial preset immediately captures state"),
        Model.GetText().Contains(TEXT("Snapshot")));
    Model.SetFootstepsEnabled(true);
    TestTrue(TEXT("Footsteps control round-trips through the model"), Model.IsFootstepsEnabled());
    Model.SetFocus(TEXT("OnlyThisActor"));
    TestEqual(TEXT("Capture focus round-trips through the model"),
        Model.GetFocus(), FString(TEXT("OnlyThisActor")));
    Model.Clear();
    TestTrue(TEXT("Clear removes both histories"), Model.GetText().IsEmpty());
    return true;
}
namespace
{
    class FVerifyDiagnosticExpiry : public IAutomationLatentCommand
    {
      public:
        FVerifyDiagnosticExpiry(FAutomationTestBase* InTest, float InSeconds, int32 InPreset)
            : Test(InTest), PreviousSeconds(InSeconds), PreviousPreset(InPreset)
        {
        }
        virtual bool Update() override
        {
            Test->TestEqual(TEXT("Detailed preset expires back to problems"),
                FDiagnosticRecorder::GetPreset(),
                0);
            IConsoleManager::Get()
                .FindConsoleVariable(TEXT("Wanderer.Diagnostic.Seconds"))
                ->Set(PreviousSeconds, ECVF_SetByConsole);
            FDiagnosticRecorder::SetPreset(PreviousPreset);
            return true;
        }

      private:
        FAutomationTestBase* Test;
        float PreviousSeconds;
        int32 PreviousPreset;
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiagnosticExpiryTest,
    "Wanderer.Diagnostics.Expiry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDiagnosticExpiryTest::RunTest(const FString& InParameters)
{
    IConsoleVariable* Seconds =
        IConsoleManager::Get().FindConsoleVariable(TEXT("Wanderer.Diagnostic.Seconds"));
    const float PreviousSeconds = Seconds->GetFloat();
    const int32 PreviousPreset = FDiagnosticRecorder::GetPreset();
    Seconds->Set(1.f, ECVF_SetByConsole);
    FDiagnosticRecorder::SetPreset(4);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyDiagnosticExpiry(this, PreviousSeconds, PreviousPreset));
    return true;
}
#endif
