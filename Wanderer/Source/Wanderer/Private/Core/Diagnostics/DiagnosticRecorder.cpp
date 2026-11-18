#include "Core/Diagnostics/DiagnosticRecorder.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "UObject/ObjectKey.h"

DEFINE_LOG_CATEGORY(LogWandererAudio);
DEFINE_LOG_CATEGORY(LogWandererEvents);

namespace
{
    constexpr int32 MaximumRecords = 512;
    constexpr int32 MaximumTransitions = 512;
    constexpr int32 MaximumDetailCharacters = 2048;
    constexpr double RateWindowSeconds = 1.0;
    FDiagnosticSnapshot DiagnosticSnapshotState;
    TMap<FObjectKey, FString> Transitions;
    FString Focus;
    uint64 CurrentTrace = 0;
    uint64 NextTrace = 0;
    uint64 Sequence = 0;
    bool bFootstepTrace = false;
    double CaptureStarted = 0;
    double WindowStarted = 0;
    int32 WindowCount = 0;
    int32 ObservedPreset = 0;
#if !UE_BUILD_SHIPPING
    TAutoConsoleVariable<int32> Preset(TEXT("Wanderer.Diagnostic.Preset"),
        0,
        TEXT("0 problems; 1 playback; 2 spatial; 3 events; 4 all. Detailed capture expires."));
    TAutoConsoleVariable<int32> DiagnosticFootstepSamplingVariable(
        TEXT("Wanderer.Diagnostic.Footsteps"), 0, TEXT("Opt into sampled footsteps. Default off."));
    TAutoConsoleVariable<int32> Rate(TEXT("Wanderer.Diagnostic.MaxPerSecond"),
        40,
        TEXT("Maximum diagnostic records per second; excess counted, never queued."));
    TAutoConsoleVariable<float> Duration(TEXT("Wanderer.Diagnostic.Seconds"),
        30.0f,
        TEXT("Detailed capture lifetime, clamped to 1..120 seconds."));
    FAutoConsoleCommand Capture(TEXT("Wanderer.Diagnostic.Capture"),
        TEXT("Capture all stages for the configured duration; footsteps remain opt-in."),
        FConsoleCommandDelegate::CreateLambda(
            []
            {
                FDiagnosticRecorder::SetPreset(4);
            }));
    FAutoConsoleCommand FocusCommand(TEXT("Wanderer.Diagnostic.Focus"),
        TEXT("Set source-path substring filter; no arguments clears it."),
        FConsoleCommandWithArgsDelegate::CreateLambda(
            [](const TArray<FString>& InArguments)
            {
                FDiagnosticRecorder::SetFocus(FString::Join(InArguments, TEXT(" ")));
            }));
    FAutoConsoleCommand Clear(TEXT("Wanderer.Diagnostic.Clear"),
        TEXT("Clear diagnostic history."),
        FConsoleCommandDelegate::CreateStatic(&FDiagnosticRecorder::Clear));
#endif

    FString Escape(const FString& InText)
    {
        FString Result;
        for (TCHAR Character : InText.Left(MaximumDetailCharacters))
        {
            if (Character == TEXT('"') || Character == TEXT('\\'))
            {
                Result += TEXT('\\');
            }
            if (Character < 32)
            {
                Result += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Character));
                continue;
            }
            Result += Character;
        }
        return Result;
    }
} // namespace

bool FDiagnosticRecorder::IsEnabled(EDiagnosticDomain InDomain, bool bInProblem)
{
#if !UE_BUILD_SHIPPING
    if (!IsInGameThread())
    {
        return false;
    }
    const int32 Value = GetPreset();
    if (bFootstepTrace && !IsFootstepsEnabled() && !bInProblem)
    {
        return false;
    }
    return bInProblem || Value == 4 || (Value == 1 && InDomain == EDiagnosticDomain::Audio)
           || (Value == 2 && InDomain != EDiagnosticDomain::Events)
           || (Value == 3 && InDomain == EDiagnosticDomain::Events);
#else
    return false;
#endif
}

bool FDiagnosticRecorder::IsFootstepsEnabled()
{
#if !UE_BUILD_SHIPPING
    return IsInGameThread() && DiagnosticFootstepSamplingVariable.GetValueOnGameThread() != 0;
#else
    return false;
#endif
}

void FDiagnosticRecorder::SetFootstepsEnabled(bool bInEnabled)
{
#if !UE_BUILD_SHIPPING
    check(IsInGameThread());
    DiagnosticFootstepSamplingVariable.AsVariable()->Set(bInEnabled ? 1 : 0, ECVF_SetByConsole);
#endif
}

void FDiagnosticRecorder::SetPreset(int32 InPreset)
{
#if !UE_BUILD_SHIPPING
    check(IsInGameThread());
    ObservedPreset = FMath::Clamp(InPreset, 0, 4);
    Preset.AsVariable()->Set(ObservedPreset, ECVF_SetByConsole);
    CaptureStarted = FPlatformTime::Seconds();
    Transitions.Reset();
#endif
}

int32 FDiagnosticRecorder::GetPreset()
{
#if !UE_BUILD_SHIPPING
    check(IsInGameThread());
    const int32 Value = FMath::Clamp(Preset.GetValueOnGameThread(), 0, 4);
    const double Now = FPlatformTime::Seconds();
    if (ObservedPreset != Value)
    {
        ObservedPreset = Value;
        CaptureStarted = Now;
        Transitions.Reset();
    }
    if (Value && Now - CaptureStarted >= FMath::Clamp(Duration.GetValueOnGameThread(), 1.f, 120.f))
    {
        SetPreset(0);
        return 0;
    }
    return Value;
#else
    return 0;
#endif
}

void FDiagnosticRecorder::SetFocus(const FString& InFocus)
{
    check(IsInGameThread());
    Focus = InFocus.Left(256);
    Transitions.Reset();
}

FString FDiagnosticRecorder::GetFocus()
{
    check(IsInGameThread());
    return Focus;
}

bool FDiagnosticRecorder::Admit(
    const UObject* InContext, EDiagnosticDomain InDomain, bool bInProblem)
{
    if (!IsEnabled(InDomain, bInProblem))
    {
        return false;
    }
    if (!Focus.IsEmpty() && !GetPathNameSafe(InContext).Contains(Focus))
    {
        return false;
    }
#if !UE_BUILD_SHIPPING
    const double Now = FPlatformTime::Seconds();
    if (Now - WindowStarted >= RateWindowSeconds)
    {
        WindowStarted = Now;
        WindowCount = 0;
    }
    if (WindowCount >= FMath::Clamp(Rate.GetValueOnGameThread(), 1, 200))
    {
        ++DiagnosticSnapshotState.Suppressed;
        return false;
    }
    ++WindowCount;
#endif
    return true;
}

void FDiagnosticRecorder::Record(const UObject* InContext,
    EDiagnosticDomain InDomain,
    const TCHAR* InStage,
    const FString& InDetail,
    bool bInProblem)
{
#if !UE_BUILD_SHIPPING
    check(IsInGameThread());
    const UWorld* World = IsValid(InContext) ? InContext->GetWorld() : nullptr;
    const TCHAR* Domain =
        InDomain == EDiagnosticDomain::Events
            ? TEXT("event")
            : (InDomain == EDiagnosticDomain::Spatial ? TEXT("spatial") : TEXT("audio"));
    FDiagnosticRecord Entry;
    Entry.Sequence = ++Sequence;
    Entry.Json = FString::Printf(
        TEXT("{\"seq\":%llu,\"trace\":%llu,\"time\":%.3f,\"frame\":%llu,")
            TEXT("\"domain\":\"%s\",\"stage\":\"%s\",\"problem\":%s,")
                TEXT("\"world\":\"%s\",\"net\":%d,\"source\":\"%s\",\"detail\":\"%s\"}"),
        Entry.Sequence,
        CurrentTrace,
        FPlatformTime::Seconds(),
        GFrameCounter,
        Domain,
        *Escape(InStage),
        bInProblem ? TEXT("true") : TEXT("false"),
        *Escape(GetPathNameSafe(World)),
        World ? static_cast<int32>(World->GetNetMode()) : -1,
        *Escape(GetPathNameSafe(InContext)),
        *Escape(InDetail));
    if (DiagnosticSnapshotState.Records.Num() >= MaximumRecords)
    {
        DiagnosticSnapshotState.Records.RemoveAt(0, 1, EAllowShrinking::No);
        ++DiagnosticSnapshotState.Evicted;
    }
    DiagnosticSnapshotState.Records.Add(MoveTemp(Entry));
    ++DiagnosticSnapshotState.Revision;
    if (InDomain == EDiagnosticDomain::Events)
    {
        UE_LOG(LogWandererEvents, Verbose, TEXT("%s"), *DiagnosticSnapshotState.Records.Last().Json);
    }
    else
    {
        UE_LOG(LogWandererAudio, Verbose, TEXT("%s"), *DiagnosticSnapshotState.Records.Last().Json);
    }
#endif
}

bool FDiagnosticRecorder::HasTransition(const UObject* InContext, const FString& InState)
{
    check(IsInGameThread());
    const FObjectKey Key(InContext);
    const FString* Previous = Transitions.Find(Key);
    if (Previous && *Previous == InState)
    {
        return false;
    }
    return true;
}

void FDiagnosticRecorder::CommitTransition(const UObject* InContext, const FString& InState)
{
    check(IsInGameThread());
    if (Transitions.Num() >= MaximumTransitions)
    {
        Transitions.Reset();
    }
    Transitions.Add(FObjectKey(InContext), InState.Left(MaximumDetailCharacters));
}

uint64 FDiagnosticRecorder::GetRevision()
{
    check(IsInGameThread());
    return DiagnosticSnapshotState.Revision;
}

uint64 FDiagnosticRecorder::GetSuppressedCount()
{
    check(IsInGameThread());
    return DiagnosticSnapshotState.Suppressed;
}

FDiagnosticSnapshot FDiagnosticRecorder::GetSnapshot()
{
    check(IsInGameThread());
    return DiagnosticSnapshotState;
}

void FDiagnosticRecorder::Clear()
{
    check(IsInGameThread());
    DiagnosticSnapshotState.Records.Reset();
    DiagnosticSnapshotState.Suppressed = 0;
    DiagnosticSnapshotState.Evicted = 0;
    ++DiagnosticSnapshotState.Revision;
    Transitions.Reset();
    WindowCount = 0;
}

uint64 FDiagnosticRecorder::GetTrace()
{
    return CurrentTrace;
}

bool FDiagnosticRecorder::IsFootstepTrace()
{
    return bFootstepTrace;
}

uint64 FDiagnosticRecorder::BeginTrace(bool bInFootstep)
{
    const uint64 Previous = CurrentTrace;
    if (!CurrentTrace)
    {
        CurrentTrace = ++NextTrace;
    }
    bFootstepTrace |= bInFootstep;
    return Previous;
}

void FDiagnosticRecorder::RestoreTrace(uint64 InTrace, bool bInFootstep)
{
    CurrentTrace = InTrace;
    bFootstepTrace = bInFootstep;
}
