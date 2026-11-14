#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Systems/Audio/AudioSchedulingStateTypes.h"

#include "Interfaces/Audio/AudioSourceInterface.h"

namespace
{
    /** Per-pass counters owned by Schedule; references update the subsystem's live readback. */
    struct FAudioAdmissionContext
    {
        FAudioBudgetLimits Limits;
        TMap<FName, int32>& Groups;
        int32& Admitted;
        int32& Overflow;
        TMap<FString, int32> Profiles;
        TSet<UObject*> ClaimedObjects;
        int32 Ambiences = 0;
        int32 Details = 0;

        FAudioAdmissionContext(const FAudioBudgetLimits& InLimits,
            TMap<FName, int32>& InGroups,
            int32& InAdmitted,
            int32& InOverflow)
            : Limits(InLimits), Groups(InGroups), Admitted(InAdmitted), Overflow(InOverflow)
        {
            Limits.TotalEvents = FMath::Max(1, Limits.TotalEvents);
            Limits.AmbienceEvents = FMath::Max(1, Limits.AmbienceEvents);
            Limits.DetailEvents = FMath::Max(1, Limits.DetailEvents);
        }

        bool IsFull(const FAudioSchedulingInfo& InInfo, bool bInGroupFull) const
        {
            return bInGroupFull || Admitted >= Limits.TotalEvents
                   || (InInfo.bAmbience && Ambiences >= Limits.AmbienceEvents)
                   || (InInfo.bDetail && Details >= Limits.DetailEvents)
                   || Profiles.FindRef(InInfo.ProfileKey) >= InInfo.MaxInstances;
        }

        void RecordAdmission(
            const FAudioSchedulingInfo& InInfo, bool bInFull, UObject* InPlaybackIdentity)
        {
            Overflow += bInFull;
            ++Admitted;
            ++Groups.FindOrAdd(InInfo.ResourceGroup);
            Ambiences += InInfo.bAmbience;
            Details += InInfo.bDetail;
            ++Profiles.FindOrAdd(InInfo.ProfileKey);
            if (InPlaybackIdentity)
            {
                ClaimedObjects.Add(InPlaybackIdentity);
            }
        }
    };
} // namespace

bool UWandererAudioSubsystem::IsSourceCurrent(const UObject* InSource) const
{
    return !bDeinitializing && IsValid(InSource) && InSource->GetWorld() == GetWorld()
           && SourcesWeakPtrs.Contains(InSource);
}

void UWandererAudioSubsystem::CollectCandidates(
    const FVector& InListener, bool bInNight, TArray<FAudioSourceCandidate>& OutCandidates) const
{
    const TArray<TWeakObjectPtr<UObject>> Snapshot = SourcesWeakPtrs;
    for (const TWeakObjectPtr<UObject>& SourceWeakPtr : Snapshot)
    {
        IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(SourceWeakPtr.Get());
        if (!Source || !IsSourceCurrent(SourceWeakPtr.Get()))
        {
            continue;
        }

        const bool bEligible = Source->Evaluate(InListener);
        if (bDeinitializing)
        {
            return;
        }
        Source = Cast<IAudioSourceInterface>(SourceWeakPtr.Get());
        if (!Source || !IsSourceCurrent(SourceWeakPtr.Get()))
        {
            continue;
        }
        if (!bEligible)
        {
            Source->UpdatePlayback(false, NAME_None, bInNight);
            continue;
        }

        const FAudioSchedulingInfo Info = Source->GetSchedulingInfo();
        if (IsSourceCurrent(SourceWeakPtr.Get()))
        {
            OutCandidates.Add({SourceWeakPtr, Info});
        }
    }

    OutCandidates.Sort(
        [](const FAudioSourceCandidate& A, const FAudioSourceCandidate& B)
        {
            if (A.Info.bProtected != B.Info.bProtected)
            {
                return A.Info.bProtected;
            }

            const float ScoreA =
                A.Info.Priority
                + (A.Info.bPlaying ? AudioSchedulingDefaults::PlayingPriorityBias : 0.0f);
            const float ScoreB =
                B.Info.Priority
                + (B.Info.bPlaying ? AudioSchedulingDefaults::PlayingPriorityBias : 0.0f);
            return ScoreA != ScoreB ? ScoreA > ScoreB : A.Info.OwnerPath < B.Info.OwnerPath;
        });
}

void UWandererAudioSubsystem::Schedule(const FVector& InListener)
{
    TArray<FAudioSourceCandidate> Candidates;
    const bool bNight = ReadNightState();
    if (bDeinitializing)
    {
        return;
    }
    CollectCandidates(InListener, bNight, Candidates);
    if (bDeinitializing)
    {
        return;
    }
    GroupAdmitted.Reset();
    AdmittedCount = 0;
    ProtectedOverflow = 0;
    FAudioAdmissionContext Context(
        ReadBudgetLimits(), GroupAdmitted, AdmittedCount, ProtectedOverflow);

    for (const FAudioSourceCandidate& Candidate : Candidates)
    {
        if (bDeinitializing)
        {
            return;
        }
        IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(Candidate.SourceWeakPtr.Get());
        if (!Source || !IsSourceCurrent(Candidate.SourceWeakPtr.Get()))
        {
            continue;
        }

        const FAudioSchedulingInfo& Info = Candidate.Info;
        const int32 GroupLimit = GetResourceGroupLimit(Info.ResourceGroup);
        const bool bGroupFull =
            GroupLimit > 0 && GroupAdmitted.FindRef(Info.ResourceGroup) >= GroupLimit;
        const bool bFull = Context.IsFull(Info, bGroupFull);
        UObject* Object = Source->GetPlaybackIdentity();
        if (bDeinitializing)
        {
            return;
        }
        Source = Cast<IAudioSourceInterface>(Candidate.SourceWeakPtr.Get());
        if (!Source || !IsSourceCurrent(Candidate.SourceWeakPtr.Get()))
        {
            continue;
        }
        const bool bDuplicate = Object && Context.ClaimedObjects.Contains(Object);
        const bool bAllowed = !bDuplicate && (!bFull || Info.bProtected);
        if (bAllowed)
        {
            Context.RecordAdmission(Info, bFull, Object);
        }

        const FName Reason =
            bDuplicate ? FName(TEXT("Duplicate Room owner"))
                       : FName(bGroupFull ? TEXT("Group budget culled") : TEXT("Budget culled"));
        Source->UpdatePlayback(bAllowed, Reason, bNight);
    }
}
