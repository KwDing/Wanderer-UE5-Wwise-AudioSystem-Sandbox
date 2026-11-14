#include "Features/AudioManager/InteriorFitAudioSyncService.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectGlobals.h"

#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

namespace
{
    constexpr float PollIntervalSec = 1.0f;

    FString WorldSnapshot(UWorld& InWorld)
    {
        TArray<FString> Rows;
        for (TActorIterator<AActor> It(&InWorld); It; ++It)
        {
            if (FInteriorFitGeometryImportOperation::IsSource(*It))
            {
                FString Tags;
                for (FName Tag : It->Tags)
                {
                    if (Tag.ToString().StartsWith(TEXT("InteriorFitProof_")))
                    {
                        Tags = Tag.ToString();
                        break;
                    }
                }
                Rows.Add(It->GetPathName() + Tags
                         + FInteriorFitGeometryImportOperation::Fingerprint(*It));
            }
        }
        Rows.Sort();
        return FString::Join(Rows, TEXT("|"));
    }
} // namespace

FInteriorFitAudioSyncService::~FInteriorFitAudioSyncService()
{
    Stop();
}

void FInteriorFitAudioSyncService::Start()
{
    if (Handle.IsValid())
    {
        return;
    }
    bStopped = false;
    ++Generation;
    Handle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(AsShared(), &FInteriorFitAudioSyncService::Tick),
        PollIntervalSec);
}

void FInteriorFitAudioSyncService::Stop()
{
    bStopped = true;
    ++Generation;
    FTSTicker::GetCoreTicker().RemoveTicker(Handle);
    Handle.Reset();
    LastWorld.Reset();
    LastSnapshot.Reset();
}

bool FInteriorFitAudioSyncService::Tick(float InDeltaSeconds)
{
    if (bStopped)
    {
        return false;
    }
    if (!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || IsGarbageCollecting()
        || GIsTransacting || GEditor->IsTransactionActive() || GIsAutomationTesting)
    {
        return true;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World || World->WorldType != EWorldType::Editor)
    {
        return true;
    }
    const TWeakObjectPtr<UWorld> WorldWeakPtr = World;
    const uint64 ActiveGeneration = Generation;
    const FString Snapshot = WorldSnapshot(*World);
    const TWeakPtr<FInteriorFitAudioSyncService> WeakService = AsShared();
    if (LastWorld.Get() == World && LastSnapshot == Snapshot)
    {
        // Two stable polls avoid copying partially generated or actively dragged geometry.
        Reconcile(World,
                  [WeakService, ActiveGeneration]
                  {
                      const TSharedPtr<FInteriorFitAudioSyncService> Service = WeakService.Pin();
                      return Service && !Service->bStopped
                             && Service->Generation == ActiveGeneration;
                  });
    }
    if (bStopped || Generation != ActiveGeneration)
    {
        return false;
    }
    if (!WorldWeakPtr.IsValid() || !GEditor
        || WorldWeakPtr.Get() != GEditor->GetEditorWorldContext().World())
    {
        return true;
    }
    LastWorld = WorldWeakPtr;
    LastSnapshot = Snapshot;
    return true;
}
