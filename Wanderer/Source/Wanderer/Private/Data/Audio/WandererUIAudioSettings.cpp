#include "Data/Audio/WandererUIAudioSettings.h"

#include "Data/Audio/WandererUIAudioProfile.h"

TSoftObjectPtr<UWandererUIAudioProfile> UWandererUIAudioSettings::GetProfileReference(
    const UClass* WidgetClass) const
{
    TSoftObjectPtr<UWandererUIAudioProfile> Reference = DefaultProfile;
    for (const UClass* Class = WidgetClass; Class; Class = Class->GetSuperClass())
    {
        if (const auto* Profile = ScreenProfiles.Find(Class->GetFName()))
        {
            Reference = *Profile;
            break;
        }
    }
    return Reference;
}
