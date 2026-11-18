#include "Actors/Audio/WandererWorldAudioBudget.h"

#include "Systems/Audio/WandererAudioSubsystem.h"

FAudioBudgetLimits AWandererWorldAudioBudget::GetAudioBudgetLimits() const
{
    FAudioBudgetLimits Limits;
    Limits.RegularSlots = FMath::Clamp(RegularSlots, 1, 64);
    Limits.TransitionSlots = FMath::Clamp(TransitionSlots, 0, 16);
    Limits.EmergencySlots = FMath::Clamp(EmergencySlots, 0, 8);
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
