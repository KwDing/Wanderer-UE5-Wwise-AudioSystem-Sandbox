#include "Systems/Audio/WandererUIAudioSubsystem.h"

#include "Engine/World.h"
#include "Templates/UnrealTemplate.h"

#include "Data/Audio/WandererUIAudioProfile.h"
#include "Data/Audio/WandererUIAudioSettings.h"

void UWandererUIAudioSubsystem::Deinitialize()
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    LoadedProfiles.Reset();
    Super::Deinitialize();
}

UWandererUIAudioProfile* UWandererUIAudioSubsystem::ResolveProfile(const UClass* InScreenClass)
{
    if (!IsInGameThread() || bStopped || bResolving)
    {
        return nullptr;
    }
    TGuardValue<bool> ResolvingGuard(bResolving, true);
    const TSoftObjectPtr<UWandererUIAudioProfile> Reference =
        GetDefault<UWandererUIAudioSettings>()->GetProfileReference(InScreenClass);
    const FSoftObjectPath Path = Reference.ToSoftObjectPath();
    if (Path.IsNull())
    {
        return nullptr;
    }
    if (const TObjectPtr<UWandererUIAudioProfile>* Loaded = LoadedProfiles.Find(Path))
    {
        if (IsValid(Loaded->Get()))
        {
            return Loaded->Get();
        }
        LoadedProfiles.Remove(Path);
    }
    UWandererUIAudioProfile* Profile = Reference.LoadSynchronous();
    if (bStopped)
    {
        return nullptr;
    }
    if (IsValid(Profile))
    {
        LoadedProfiles.Add(Path, Profile);
    }
    return Profile;
}

bool UWandererUIAudioSubsystem::CanPlay(const UWorld* InWorld) const
{
    return !bStopped && IsValid(InWorld) && InWorld->IsGameWorld() && !InWorld->bIsTearingDown
           && InWorld->GetGameInstance() == GetGameInstance();
}
