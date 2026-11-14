#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class AActor;

/** Repairs missing authored grass after editor map loading; never runs in gameplay worlds. */
class FGrassRegenerationService
{
  public:
    ~FGrassRegenerationService();

    void Start();
    void Stop();

  private:
    friend class FGrassRegenerationLifecycleTest;
    friend class FGrassSceneTest;

    FDelegateHandle MapOpenedHandle;
    FTSTicker::FDelegateHandle GenerationHandle;
    uint32 Generation = 0;
    int32 RemainingPasses = 0;
    bool bStarted = false;

    void OnMapOpened(const FString& InFilename, bool bInTemplate);
    bool OnGenerationTick(float InDeltaTime);
    void CancelGeneration();
    static bool RegenerateMissingGrass(AActor& InActor);
};
