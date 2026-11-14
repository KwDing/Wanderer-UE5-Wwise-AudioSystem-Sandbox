#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "PropertyEditorDelegates.h"

#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"

void FAudioManagerController::SetPage(int32 InPage)
{
    if (bStopped || (InPage != 0 && InPage != 1 && InPage != 3))
    {
        return;
    }
    const TSharedRef<FAudioManagerController> KeepAlive = AsShared();
    State.bShowRoomActions = false;
    State.ActivePage = InPage;
    if (InPage == 3)
    {
        State.TypeFilter = TEXT("全部类型");
        State.GroupFilter = TEXT("全部资源组");
    }
    if (InPage != 0)
    {
        State.bProjectAssets = false;
    }
    SetDetailsObjects(TArray<UObject*>());
    if (bStopped)
    {
        return;
    }
    State.SortColumn = "Name";
    RebuildColumns();
    Refresh();
    if (!bStopped && InPage == 1 && State.ActivePage == InPage)
    {
        EditBudget();
    }
    return;
}

UWorld* FAudioManagerController::GetObservedWorld() const
{
    return !bStopped && GEditor ? (GEditor->PlayWorld ? GEditor->PlayWorld.Get()
                                                      : GEditor->GetEditorWorldContext().World())
                                : nullptr;
}

AWandererWorldAudioBudget* FAudioManagerController::FindBudget() const
{
    if (UWorld* World = GetObservedWorld())
    {
        for (TActorIterator<AWandererWorldAudioBudget> It(World); It; ++It)
        {
            return *It;
        }
    }
    return nullptr;
}
