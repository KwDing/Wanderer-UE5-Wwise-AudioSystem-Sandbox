#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"

#if !UE_BUILD_SHIPPING
namespace
{
    constexpr int32 ProblemsPreset = 0;
    constexpr int32 PlaybackPreset = 1;

    bool ReadToggle(const TArray<FString>& InArguments, bool& bOutEnabled, FOutputDevice& Out)
    {
        if (InArguments.Num() != 1 || (InArguments[0] != TEXT("0") && InArguments[0] != TEXT("1")))
        {
            Out.Log(TEXT("Deprecated command: provide 0 or 1. Prefer Wanderer.Diagnostic.Preset."));
            return false;
        }
        bOutEnabled = InArguments[0] == TEXT("1");
        return true;
    }

    void OnMaster(const TArray<FString>& InArguments, FOutputDevice& Out)
    {
        bool bEnabled = false;
        if (!ReadToggle(InArguments, bEnabled, Out))
        {
            return;
        }
        FDiagnosticRecorder::SetPreset(bEnabled ? PlaybackPreset : ProblemsPreset);
        Out.Log(
            TEXT("Migrated: Wanderer.AudioLog -> Wanderer.Diagnostic.Preset 0/1. "
                 "0 retains problems; 1 starts timed playback capture, not unlimited logging."));
    }

    void OnFootsteps(const TArray<FString>& InArguments, FOutputDevice& Out)
    {
        bool bEnabled = false;
        if (!ReadToggle(InArguments, bEnabled, Out))
        {
            return;
        }
        FDiagnosticRecorder::SetFootstepsEnabled(bEnabled);
        Out.Log(TEXT("Migrated: Wanderer.AudioLog.Footsteps -> Wanderer.Diagnostic.Footsteps. "
                     "Choose Playback/All separately to capture successful footsteps."));
    }

    void OnRemovedChannel(const TArray<FString>& InArguments, FOutputDevice& Out)
    {
        bool bEnabled = false;
        if (!ReadToggle(InArguments, bEnabled, Out))
        {
            return;
        }
        // A removed channel cannot safely turn off the shared preset on behalf of other channels.
        if (bEnabled)
        {
            FDiagnosticRecorder::SetPreset(PlaybackPreset);
        }
        Out.Log(bEnabled
                    ? TEXT("Deprecated channel: started shared timed Playback preset. "
                           "Use Wanderer.Diagnostic.Preset 1 or GameLog source filtering.")
                    : TEXT("No equivalent per-channel off switch. Settings unchanged. "
                           "Use Wanderer.Diagnostic.Preset 0 to return to Problems explicitly."));
    }

    FAutoConsoleCommand Master(TEXT("Wanderer.AudioLog"),
        TEXT("Deprecated: Diagnostic.Preset"),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&OnMaster));
    FAutoConsoleCommand Footsteps(TEXT("Wanderer.AudioLog.Footsteps"),
        TEXT("Deprecated: Diagnostic.Footsteps"),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&OnFootsteps));
    FAutoConsoleCommand Configuration(TEXT("Wanderer.AudioLog.Configuration"),
        TEXT("Deprecated channel; shared timed Playback preset on enable"),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&OnRemovedChannel));
    FAutoConsoleCommand Actions(TEXT("Wanderer.AudioLog.Actions"),
        TEXT("Deprecated channel; shared timed Playback preset on enable"),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&OnRemovedChannel));
    FAutoConsoleCommand Switches(TEXT("Wanderer.AudioLog.Switches"),
        TEXT("Deprecated channel; shared timed Playback preset on enable"),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&OnRemovedChannel));
    FAutoConsoleCommand Playback(TEXT("Wanderer.AudioLog.Playback"),
        TEXT("Deprecated channel; shared timed Playback preset on enable"),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&OnRemovedChannel));
    FAutoConsoleCommand Amb(TEXT("Wanderer.AudioLog.Amb"),
        TEXT("Deprecated channel; shared timed Playback preset on enable"),
        FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&OnRemovedChannel));
} // namespace
#endif
