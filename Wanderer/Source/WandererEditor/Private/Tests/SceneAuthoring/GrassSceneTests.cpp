#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"

#include "PCGComponent.h"

#include "Features/GrassRegenerationService.h"

namespace
{
    int32 CountGrassInstances(const AActor& InActor)
    {
        TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Components(&InActor);
        int32 Count = 0;
        for (const UHierarchicalInstancedStaticMeshComponent* Component : Components)
        {
            if (IsValid(Component))
            {
                Count += Component->GetInstanceCount();
            }
        }
        return Count;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGrassSceneTest,
    "Wanderer.SceneAuthoring.GrassSceneRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGrassSceneTest::RunTest(const FString& InParameters)
{
    if (!FApp::IsUnattended()
        || !FParse::Param(FCommandLine::Get(), TEXT("WandererDisposableSceneTest")))
    {
        AddWarning(
            TEXT("Skipped: requires an unattended disposable process and "
                 "-WandererDisposableSceneTest. The loaded world is modified without saving."));
        return true;
    }
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->GetOutermost()->GetName() != TEXT("/Game/Maps/L_AudioSystemDemo"))
    {
        AddError(TEXT("Run in a disposable process with L_AudioSystemDemo loaded; never save."));
        return false;
    }
    AActor* Target = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->ActorHasTag(TEXT("TrialAutoGrassPCG")) && It->FindComponentByClass<UPCGComponent>()
            && CountGrassInstances(**It) > 0)
        {
            Target = *It;
            break;
        }
    }
    if (!TestNotNull(TEXT("Real authored grass with saved instances exists"), Target))
    {
        return false;
    }
    const int32 Before = CountGrassInstances(*Target);
    const bool bWasDirty = Target->GetOutermost()->IsDirty();
    TestFalse(
        TEXT("Saved grass is reused"), FGrassRegenerationService::RegenerateMissingGrass(*Target));
    TestEqual(TEXT("Saved instance count unchanged"), CountGrassInstances(*Target), Before);
    TestEqual(TEXT("Reuse does not dirty package"), Target->GetOutermost()->IsDirty(), bWasDirty);
    UPCGComponent* PCG = Target->FindComponentByClass<UPCGComponent>();
    const TWeakObjectPtr<AActor> ActorWeakPtr = Target;
    const TWeakObjectPtr<UPCGComponent> PCGWeakPtr = PCG;
    const TWeakObjectPtr<UWorld> WorldWeakPtr = World;
    // This test changes only this disposable process's world. No save operation is performed.
    PCG->CleanupLocalImmediate(true);
    if (!ActorWeakPtr.IsValid() || !PCGWeakPtr.IsValid() || !WorldWeakPtr.IsValid()
        || ActorWeakPtr->GetWorld() != WorldWeakPtr.Get() || WorldWeakPtr->bIsTearingDown)
    {
        AddError(TEXT("Grass owner changed during cleanup."));
        return false;
    }
    TestEqual(TEXT("Missing-instance condition exercised"), CountGrassInstances(*Target), 0);
    TestTrue(TEXT("Missing grass regeneration requested"),
        FGrassRegenerationService::RegenerateMissingGrass(*Target));
    if (!ActorWeakPtr.IsValid() || !PCGWeakPtr.IsValid() || !WorldWeakPtr.IsValid()
        || ActorWeakPtr->GetWorld() != WorldWeakPtr.Get() || WorldWeakPtr->bIsTearingDown)
    {
        AddError(TEXT("Grass owner changed during generation submission."));
        return false;
    }
    constexpr double TimeoutSeconds = 120.0;
    const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, ActorWeakPtr, PCGWeakPtr, WorldWeakPtr, Deadline, Before]()
        {
            if (!ActorWeakPtr.IsValid() || !PCGWeakPtr.IsValid() || !WorldWeakPtr.IsValid()
                || WorldWeakPtr->bIsTearingDown || ActorWeakPtr->GetWorld() != WorldWeakPtr.Get())
            {
                AddError(TEXT("Grass owner disappeared during recovery."));
                return true;
            }
            if (!PCGWeakPtr->IsGenerating() && CountGrassInstances(*ActorWeakPtr) > 0)
            {
                AddInfo(FString::Printf(TEXT("Grass instances: before=%d recovered=%d"),
                    Before,
                    CountGrassInstances(*ActorWeakPtr)));
                return true;
            }
            if (FPlatformTime::Seconds() < Deadline)
            {
                return false;
            }
            AddError(TEXT("Grass recovery timed out without restored instances."));
            return true;
        }));
    return true;
}
#endif
