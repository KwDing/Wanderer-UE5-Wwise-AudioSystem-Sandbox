#include "Actors/Audio/WandererWorldAudioBudget.h"

#include "Systems/Audio/WandererAudioSubsystem.h"

FAudioBudgetLimits AWandererWorldAudioBudget::GetAudioBudgetLimits() const
{
    FAudioBudgetLimits Limits;
    Limits.TotalEvents = MaxTotalEvents;
    Limits.AmbienceEvents = MaxAmbienceEvents;
    Limits.DetailEvents = MaxDetailEvents;
    Limits.UpdateIntervalSec = UpdateInterval;
    return Limits;
}

void AWandererWorldAudioBudget::SubmitAudioResources(UObject& InReceiver) const
{
    UWandererAudioSubsystem* System = Cast<UWandererAudioSubsystem>(&InReceiver);
    if (!System || System->GetWorld() != GetWorld())
    {
        return;
    }

    System->ConfigureResourceGroups(ResourceGroups);
}
