#include "Features/GameLog/GameLogModel.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Features/GameLog/GameLogSnapshotOperation.h"

bool FGameLogModel::Refresh()
{
    const int32 Preset = FDiagnosticRecorder::GetPreset();
    const uint64 Revision = FDiagnosticRecorder::GetRevision();
    const uint64 Suppressed = FDiagnosticRecorder::GetSuppressedCount();
    const bool bHistoryChanged = SeenRevision != Revision;
    if (!bHistoryChanged && SeenPreset == Preset && SeenSuppressed == Suppressed)
    {
        return false;
    }
    if (bHistoryChanged)
    {
        SeenRevision = Revision;
        RefreshHistory();
    }

    SeenPreset = Preset;
    SeenSuppressed = Suppressed;
    static constexpr const TCHAR* PresetNames[] = {TEXT("Problems"),
        TEXT("Playback"),
        TEXT("Spatial + playback"),
        TEXT("Events"),
        TEXT("All")};
    Status = FString::Printf(
        TEXT("Preset=%s | retained=%d | rate-dropped=%llu | evicted=%llu | "
             "Detailed capture expires automatically. PlayingID is not proof of audibility."),
        PresetNames[Preset],
        RetainedCount,
        SeenSuppressed,
        EvictedCount);
    return true;
}

void FGameLogModel::Clear()
{
    FDiagnosticRecorder::Clear();
    SpatialLines.Reset();
    Refresh();
}

void FGameLogModel::CaptureSpace()
{
    SpatialLines = FGameLogSnapshotOperation::Capture();
    SeenRevision = MAX_uint64;
    Refresh();
}

void FGameLogModel::SetPreset(int32 InPreset)
{
    constexpr int32 SpatialPreset = 2;
    FDiagnosticRecorder::SetPreset(InPreset);
    if (InPreset == SpatialPreset)
    {
        CaptureSpace();
        return;
    }
    Refresh();
}

void FGameLogModel::SetFocus(const FString& InFocus)
{
    FDiagnosticRecorder::SetFocus(InFocus);
}

void FGameLogModel::SetFootstepsEnabled(bool bInEnabled)
{
    FDiagnosticRecorder::SetFootstepsEnabled(bInEnabled);
}

bool FGameLogModel::IsFootstepsEnabled() const
{
    return FDiagnosticRecorder::IsFootstepsEnabled();
}

FString FGameLogModel::GetFocus() const
{
    return FDiagnosticRecorder::GetFocus();
}

const FString& FGameLogModel::GetText() const
{
    return Text;
}

const FString& FGameLogModel::GetStatus() const
{
    return Status;
}

void FGameLogModel::RefreshHistory()
{
    const FDiagnosticSnapshot Snapshot = FDiagnosticRecorder::GetSnapshot();
    RetainedCount = Snapshot.Records.Num();
    EvictedCount = Snapshot.Evicted;
    TArray<FString> Lines;
    Lines.Reserve(RetainedCount + SpatialLines.Num());
    for (const FDiagnosticRecord& Record : Snapshot.Records)
    {
        Lines.Add(Record.Json);
    }
    Lines.Append(SpatialLines);
    Text = FString::Join(Lines, TEXT("\n"));
}
