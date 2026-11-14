#include "Tests/CharacterContact/SurfaceNotificationProbe.h"

#include "Framework/Surface/WandererSurfaceComponent.h"
void USurfaceNotificationProbe::OnSurface(FName InPrevious, FName InCurrent)
{
    ++SurfaceCalls;
    ObservedWetness = Surface->GetCurrentWetness();
    if (bDestroyOnSurface)
    {
        Surface->DestroyComponent();
        return;
    }
    Surface->PublishContact(TEXT("snow"), TEXT("dry"));
}
void USurfaceNotificationProbe::OnWetness(FName InPrevious, FName InCurrent)
{
    ++WetnessCalls;
}
