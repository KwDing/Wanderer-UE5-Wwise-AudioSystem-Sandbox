#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Systems/Audio/AudioSchedulingStateTypes.h"
#include "Systems/Audio/WorldAudioAdmissionContext.h"
#include "Systems/Audio/RadioPlaybackManager.h"

#include "Interfaces/Audio/AudioSourceInterface.h"

int32 UWandererAudioSubsystem::GetWorldPlaybackCount() const
{
    int32 Active = GetObjectActiveCount() + GetRadioActiveCount()
        + GetEmitterPoolStats().WorldRetiring;
    for (const TWeakObjectPtr<UObject>& SourceWeakPtr : SourcesWeakPtrs)
    {
        const IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(SourceWeakPtr.Get());
        Active += Source && Source->GetPlayingID() != 0;
    }
    return Active;
}

bool UWandererAudioSubsystem::CanStartWorldPlayback() const
{
    const FAudioBudgetLimits Limits = ReadBudgetLimits();
    return GetWorldPlaybackCount()
        < FMath::Max(1, Limits.RegularSlots) + FMath::Max(0, Limits.TransitionSlots);
}

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

}

// 统一裁决播放资格，玩法只提交意图
void UWandererAudioSubsystem::Schedule(const FVector& InListener)
{
    TArray<FAudioSourceCandidate> Candidates;
    const bool bNight = ReadNightState();
    if (bDeinitializing)
    {
        return;
    }
    CollectCandidates(InListener, bNight, Candidates);
    CollectObjectCandidates(InListener, Candidates);
    CollectRadioCandidates(InListener, Candidates);
    if (bDeinitializing)
    {
        return;
    }
    Candidates.Sort(
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
    GroupAdmitted.Reset();
    AdmittedCount = 0;
    ProtectedOverflow = 0;
    FWorldAudioAdmissionContext Context(
        ReadBudgetLimits(), GroupAdmitted, AdmittedCount, ProtectedOverflow);
    Context.StartsAvailable = FMath::Max(0, Context.Limits.RegularSlots
        + Context.Limits.TransitionSlots - GetWorldPlaybackCount());
    SelectAdmissions(Candidates, bNight, Context);
    if (bDeinitializing)
    {
        return;
    }
    RetainPlayingCandidates(Candidates, Context);
    ApplyAdmissions(Candidates, bNight);
}

void UWandererAudioSubsystem::SelectAdmissions(
    TArray<FAudioSourceCandidate>& InOutCandidates, bool bInNight, FWorldAudioAdmissionContext& InOutContext)
{
    for (FAudioSourceCandidate& Candidate : InOutCandidates)
    {
        if (bDeinitializing)
        {
            return;
        }
        IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(Candidate.SourceWeakPtr.Get());
        const bool bObject = Candidate.ObjectInstanceID.IsValid() || Candidate.RadioWeakPtr.IsValid();
        if (!bObject && (!Source || !IsSourceCurrent(Candidate.SourceWeakPtr.Get())))
        {
            continue;
        }

        const FAudioSchedulingInfo& Info = Candidate.Info;
        const int32 GroupLimit = GetResourceGroupLimit(Info.ResourceGroup);
        const bool bGroupFull =
            GroupLimit > 0 && GroupAdmitted.FindRef(Info.ResourceGroup) >= GroupLimit;
        const bool bFull = InOutContext.IsFull(Info, bGroupFull);
        const UObject* Object = Source ? Source->GetPlaybackIdentity() : nullptr;
        if (bDeinitializing)
        {
            return;
        }
        Source = Cast<IAudioSourceInterface>(Candidate.SourceWeakPtr.Get());
        if (!bObject && (!Source || !IsSourceCurrent(Candidate.SourceWeakPtr.Get())))
        {
            continue;
        }
        const bool bDuplicate = Object && InOutContext.ClaimedObjects.Contains(Object);
        const bool bSlotFull = InOutContext.DesiredRegular
            >= FMath::Max(1, InOutContext.Limits.RegularSlots);
        const bool bEmergencyFull = InOutContext.DesiredEmergency >= InOutContext.Limits.EmergencySlots;
        Candidate.bAdmitted = !bDuplicate && (Candidate.bEmergency
            ? !bEmergencyFull : !bSlotFull && (!bFull || Info.bProtected));
        if (Candidate.bAdmitted)
        {
            InOutContext.DesiredEmergency += Candidate.bEmergency;
            InOutContext.DesiredRegular += !Candidate.bEmergency;
            Candidate.bAdmitted = PrepareCandidate(Candidate, bInNight, InOutContext);
            if (bDeinitializing)
            {
                return;
            }
        }
        if (Candidate.bAdmitted && Candidate.bEmergency)
        {
            ++InOutContext.EmergencyAdmitted;
        }
        else if (Candidate.bAdmitted)
        {
            InOutContext.RecordAdmission(Info, bFull, Object);
        }

        Candidate.RejectionReason =
            bDuplicate ? FName(TEXT("Duplicate Room owner"))
                       : FName(bGroupFull ? TEXT("Group budget culled") : TEXT("Budget culled"));
    }

}

bool UWandererAudioSubsystem::PrepareCandidate(FAudioSourceCandidate& InOutCandidate,
    bool bInNight, FWorldAudioAdmissionContext& InOutContext)
{
    if (InOutCandidate.Info.bPlaying)
    {
        return true;
    }
    IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(InOutCandidate.SourceWeakPtr.Get());
    bool bReady = false;
    if (URadioPlaybackManager* Radio = InOutCandidate.RadioWeakPtr.Get())
    {
        bReady = Radio->PrepareAdmission(InOutCandidate.ContentID, InOutCandidate.Info.Priority);
    }
    else
    {
        bReady = InOutCandidate.ObjectInstanceID.IsValid()
            ? PrepareObjectAdmission(InOutCandidate.ObjectInstanceID, InOutCandidate.bEmergency)
            : Source && Source->PrepareForAdmission(bInNight);
    }
    InOutCandidate.bPreparing = !bReady
        || (!InOutCandidate.bEmergency && InOutContext.StartsAvailable == 0);
    InOutContext.StartsAvailable -= !InOutCandidate.bPreparing && !InOutCandidate.bEmergency;
    return !InOutCandidate.bPreparing;
}

// 替换音未就绪时保留合格旧声，避免空档
void UWandererAudioSubsystem::RetainPlayingCandidates(
    TArray<FAudioSourceCandidate>& InOutCandidates, FWorldAudioAdmissionContext& InOutContext)
{
    // A pending replacement does not silence a still-eligible incumbent.
    for (FAudioSourceCandidate& Candidate : InOutCandidates)
    {
        if (Candidate.bAdmitted || Candidate.bPreparing || !Candidate.Info.bPlaying)
        {
            continue;
        }
        const FAudioSchedulingInfo& Info = Candidate.Info;
        const int32 GroupLimit = GetResourceGroupLimit(Info.ResourceGroup);
        const bool bFull = InOutContext.IsFull(Info,
            GroupLimit > 0 && GroupAdmitted.FindRef(Info.ResourceGroup) >= GroupLimit);
        const IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(Candidate.SourceWeakPtr.Get());
        const UObject* Identity = Source ? Source->GetPlaybackIdentity() : nullptr;
        const bool bDuplicate = Identity && InOutContext.ClaimedObjects.Contains(Identity);
        Candidate.bAdmitted = !bDuplicate && (Candidate.bEmergency
            ? InOutContext.EmergencyAdmitted < InOutContext.Limits.EmergencySlots
            : InOutContext.RegularAdmitted < InOutContext.Limits.RegularSlots
                && (!bFull || Info.bProtected));
        if (Candidate.bAdmitted && Candidate.bEmergency)
        {
            ++InOutContext.EmergencyAdmitted;
        }
        else if (Candidate.bAdmitted)
        {
            InOutContext.RecordAdmission(Info, bFull, Identity);
        }
    }

}

void UWandererAudioSubsystem::ApplyAdmissions(
    const TArray<FAudioSourceCandidate>& InCandidates, bool bInNight)
{
    // Retire losers before posting winners so spare capacity can carry the crossfade.
    for (bool bAdmitted : {false, true})
    {
        for (const FAudioSourceCandidate& Candidate : InCandidates)
        {
            if (bDeinitializing)
            {
                return;
            }
            if (Candidate.bPreparing || Candidate.bAdmitted != bAdmitted)
            {
                continue;
            }
            if (URadioPlaybackManager* Radio = Candidate.RadioWeakPtr.Get())
            {
                Radio->SetAdmission(Candidate.ContentID, bAdmitted, Candidate.Info.Priority);
            }
            else if (Candidate.ObjectInstanceID.IsValid())
            {
                SetObjectAdmission(Candidate.ObjectInstanceID, bAdmitted, Candidate.bEmergency);
            }
            else if (IsSourceCurrent(Candidate.SourceWeakPtr.Get()))
            {
                if (IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(Candidate.SourceWeakPtr.Get()))
                {
                    Source->UpdatePlayback(bAdmitted,
                        bAdmitted ? NAME_None : Candidate.RejectionReason, bInNight);
                }
            }
        }
    }
}
