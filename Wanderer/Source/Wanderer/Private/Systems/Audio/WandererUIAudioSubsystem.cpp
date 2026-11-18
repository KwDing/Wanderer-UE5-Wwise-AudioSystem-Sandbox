#include "Systems/Audio/WandererUIAudioSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Templates/UnrealTemplate.h"

#include "AkAudioEvent.h"

#include "Data/Audio/WandererUIAudioProfile.h"
#include "Data/Audio/WandererUIAudioSettings.h"
#include "Systems/Audio/AudioEventLeaseManager.h"

void UWandererUIAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    EventLeases = NewObject<UAudioEventLeaseManager>(this);
    PreloadTicker = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UWandererUIAudioSubsystem::AdvancePreloads), .05f);
}

void UWandererUIAudioSubsystem::Deinitialize()
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    FTSTicker::GetCoreTicker().RemoveTicker(PreloadTicker);
    for (const auto& Pair : ProfileRequests)
    {
        Pair.Value->CancelHandle();
    }
    ProfileRequests.Reset();
    EventRequests.Reset();
    if (EventLeases)
    {
        EventLeases->Stop();
        EventLeases = nullptr;
    }
    LoadedProfiles.Reset();
    Super::Deinitialize();
}

void UWandererUIAudioSubsystem::PreloadConfiguredProfiles()
{
    const auto* Settings = GetDefault<UWandererUIAudioSettings>();
    RequestProfile(Settings->DefaultProfile.ToSoftObjectPath());
    for (const auto& Pair : Settings->ScreenProfiles)
    {
        RequestProfile(Pair.Value.ToSoftObjectPath());
    }
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
    UWandererUIAudioProfile* Profile = Reference.Get();
    if (IsValid(Profile))
    {
        LoadedProfiles.Add(Path, Profile);
    }
    else
    {
        RequestProfile(Path);
    }
    return Profile;
}

void UWandererUIAudioSubsystem::RequestProfile(const FSoftObjectPath& InPath)
{
    if (bStopped || InPath.IsNull() || LoadedProfiles.Contains(InPath) || ProfileRequests.Contains(InPath))
    {
        return;
    }
    if (auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(InPath))
    {
        ProfileRequests.Add(InPath, MoveTemp(Handle));
    }
}

void UWandererUIAudioSubsystem::RequestEvent(UAkAudioEvent* InEvent)
{
    if (EventLeases && InEvent && !EventRequests.Contains(InEvent) && EventLeases->Acquire(InEvent))
    {
        EventRequests.Add(InEvent, EAudioEventResourceStatus::Pending);
    }
}

bool UWandererUIAudioSubsystem::AdvancePreloads(float InDeltaSec)
{
    if (bStopped)
    {
        return false;
    }
    for (auto It = ProfileRequests.CreateIterator(); It; ++It)
    {
        if (!It.Value()->HasLoadCompleted())
        {
            continue;
        }
        if (auto* Profile = Cast<UWandererUIAudioProfile>(It.Value()->GetLoadedAsset()))
        {
            LoadedProfiles.Add(It.Key(), Profile);
            PreloadProfileEvents(*Profile);
        }
        It.RemoveCurrent();
    }
    for (auto& Pair : EventRequests)
    {
        if (Pair.Value == EAudioEventResourceStatus::Pending)
        {
            Pair.Value = EventLeases->Advance(Pair.Key.Get());
        }
    }
    return true;
}

bool UWandererUIAudioSubsystem::CanPlay(const UWorld* InWorld) const
{
    return !bStopped && IsValid(InWorld) && InWorld->IsGameWorld() && !InWorld->bIsTearingDown
           && InWorld->GetGameInstance() == GetGameInstance();
}
