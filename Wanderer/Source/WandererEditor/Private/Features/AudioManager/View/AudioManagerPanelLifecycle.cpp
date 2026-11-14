#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "IDetailsView.h"

#include "Features/AudioManager/Controller/AudioManagerController.h"

SAudioManagerPanel::~SAudioManagerPanel()
{
    Stop();
}

void SAudioManagerPanel::Stop()
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    if (Details && DetailsChangedHandle.IsValid())
    {
        Details->OnFinishedChangingProperties().Remove(DetailsChangedHandle);
        DetailsChangedHandle.Reset();
    }
    if (Controller)
    {
        Controller->Stop();
    }
    SetEnabled(false);
}
