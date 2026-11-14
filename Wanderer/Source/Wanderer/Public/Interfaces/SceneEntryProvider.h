#pragma once
#include "UObject/Interface.h"
#include "SceneEntryProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USceneEntryProvider : public UInterface
{
    GENERATED_BODY()
};
/** Host coordination contract; loading and UI do not discover concrete sibling components. */
class WANDERER_API ISceneEntryProvider
{
    GENERATED_BODY()
  public:
    virtual bool IsEntryLoading() const = 0;
    virtual bool AreEntryCharactersReady() const = 0;
    virtual void SetEntryMapScanEnabled(bool bInEnabled) = 0;
    virtual void StartEntryMapScan() = 0;
    virtual bool IsEntryMapReady() const = 0;
    virtual bool SetEntryLoadingState(bool bInLoading) = 0;
    virtual bool SetCharacterSelectionState(bool bInOpen) = 0;
};
