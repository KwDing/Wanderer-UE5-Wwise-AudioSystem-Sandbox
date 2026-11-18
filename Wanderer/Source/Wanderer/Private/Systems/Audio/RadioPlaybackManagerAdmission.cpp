#include "Systems/Audio/RadioPlaybackManager.h"

#include "GameFramework/Actor.h"

#include "Data/Audio/WandererRadioAudioSettings.h"
#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"
#include "Systems/Audio/WwiseAudioBackend.h"

void URadioPlaybackManager::CollectCandidates(
    const FVector& InListener, TArray<FAudioSourceCandidate>& OutCandidates)
{
    AActor* Owner = OwnerWeakPtr.Get();
    if (bShuttingDown || bOperationActive || !Owner)
    {
        return;
    }
    const auto* Settings = GetDefault<UWandererRadioAudioSettings>();
    LoadPriority = AudioSchedulingDefaults::PreloadPriority;
    bHasSpatialSample = !Sources.IsEmpty();
    TMap<FName, float> Distances;
    TSet<FName> PlaybackContents;
    for (const FRadioEmitterSnapshot& Source : Sources)
    {
        float& Distance = Distances.FindOrAdd(Source.ContentID, FLT_MAX);
        Distance = FMath::Min(Distance,
            static_cast<float>(FVector::Distance(InListener, Source.Transform.GetLocation()) / 100));
        if (Source.bPlaying || FindSyncGroup(Source.ContentID))
        {
            PlaybackContents.Add(Source.ContentID);
        }
    }
    bWithinLoadRange = false;
    for (const auto& Pair : Distances)
    {
        const bool bPlaying = FindSyncGroup(Pair.Key) != nullptr;
        const float Limit = bPlaying ? FMath::Max(Settings->ActivationDistanceM, Settings->ReleaseDistanceM)
            : FMath::Max(0.01f, Settings->ActivationDistanceM);
        if (Pair.Value > Limit)
        {
            SetAdmission(Pair.Key, false, AudioSchedulingDefaults::PreloadPriority);
            continue;
        }
        bWithinLoadRange = true;
        if (!PlaybackContents.Contains(Pair.Key))
        {
            continue;
        }
        FAudioSourceCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
        Candidate.RadioWeakPtr = this;
        Candidate.ContentID = Pair.Key;
        Candidate.Info.OwnerPath = Owner->GetPathName() + Pair.Key.ToString();
        Candidate.Info.ProfileKey = Candidate.Info.OwnerPath;
        Candidate.Info.Priority = FMath::Clamp(Settings->Priority
            - 40 * Pair.Value / FMath::Max(0.01f, Settings->ActivationDistanceM), 0.0f, 99.0f);
        Candidate.Info.MaxInstances = 1;
        Candidate.Info.bDetail = true;
        Candidate.Info.bPlaying = bPlaying;
    }
}

bool URadioPlaybackManager::PrepareAdmission(FName InContentID, float InPriority)
{
    if (bShuttingDown || bOperationActive || !bWithinLoadRange)
    {
        return false;
    }
    LoadPriority = InPriority;
    AdmittedContents.Remove(InContentID);
    if (!bResourcesRequested)
    {
        RequestMedia();
    }
    AdvanceMedia();
    const UAudioEmitterPool* Pool = EmitterPoolWeakPtr.Get();
    return MediaStatus == EAudioEventResourceStatus::Ready && Pool && !Pool->HasPendingSource(*this);
}

void URadioPlaybackManager::SetAdmission(FName InContentID, bool bInAdmitted, float InPriority)
{
    if (bShuttingDown)
    {
        return;
    }
    if (bInAdmitted)
    {
        LoadPriority = InPriority;
        AdmittedContents.Add(InContentID);
        return;
    }
    AdmittedContents.Remove(InContentID);
    if (bOperationActive)
    {
        return;
    }
    const int32 Index = SyncGroups.IndexOfByPredicate([InContentID](const FRadioSyncGroup& Group)
        { return Group.ContentID == InContentID; });
    if (Index == INDEX_NONE)
    {
        return;
    }
    FRadioSyncGroup& Group = SyncGroups[Index];
    const int32 PositionMs = WandererAudioBackend::GetPlaybackPositionMs(Group.PlayingID);
    if (PositionMs >= 0)
    {
        SavedPositionsMs.Add(InContentID, PositionMs);
    }
    if (Sources.ContainsByPredicate([InContentID](const FRadioEmitterSnapshot& Source)
        { return Source.ContentID == InContentID && Source.bPlaying; }))
    {
        PendingStarts.Add(InContentID);
    }
    const auto* Settings = GetDefault<UWandererRadioAudioSettings>();
    DestroySyncGroup(Group, FMath::Max(1, FMath::RoundToInt(Settings->FadeOutSec * 1000)));
    SyncGroups.RemoveAt(Index);
}

void URadioPlaybackManager::SuspendAdmissions()
{
    TArray<FName> Contents = AdmittedContents.Array();
    for (FName Content : Contents)
    {
        SetAdmission(Content, false, AudioSchedulingDefaults::PreloadPriority);
    }
}

int32 URadioPlaybackManager::GetActiveEmitterCount() const
{
    return SyncGroups.Num();
}

void URadioPlaybackManager::ApplyPendingAdmissions()
{
    TArray<FName> Rejected;
    for (const FRadioSyncGroup& Group : SyncGroups)
    {
        if (!AdmittedContents.Contains(Group.ContentID))
        {
            Rejected.Add(Group.ContentID);
        }
    }
    for (FName Content : Rejected)
    {
        SetAdmission(Content, false, AudioSchedulingDefaults::PreloadPriority);
    }
}
