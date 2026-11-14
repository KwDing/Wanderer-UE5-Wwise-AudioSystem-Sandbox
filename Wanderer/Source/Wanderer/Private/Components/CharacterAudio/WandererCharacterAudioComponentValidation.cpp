#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"

#include "AkAudioEvent.h"
#include "AkRtpc.h"
#include "AkStateValue.h"
#include "AkSwitchValue.h"
#include "AkTrigger.h"

#include "Components/CharacterAudio/CharacterAudioActionUtils.h"

namespace
{
    using namespace WandererCharacterActions;
    bool IsMappingKeyValid(FName Key)
    {
        return !Key.IsNone();
    }
    bool IsMappingKeyValid(TEnumAsByte<EPhysicalSurface> Key)
    {
        return Key < SurfaceType_Max;
    }

    template <typename KeyType, typename AssetType>
    bool ValidateGroupMap(const TMap<KeyType, TObjectPtr<AssetType>>& Values)
    {
        if (Values.IsEmpty())
        {
            return false;
        }
        uint32 GroupID = 0;
        for (const auto& Entry : Values)
        {
            const AssetType* Asset = Entry.Value.Get();
            if (!IsMappingKeyValid(Entry.Key) || !IsValid(Asset) || !Asset->GetShortID()
                || !Asset->GetGroupID())
            {
                return false;
            }
            if (GroupID && GroupID != Asset->GetGroupID())
            {
                return false;
            }
            GroupID = Asset->GetGroupID();
        }
        return true;
    }

    bool ValidateParameterValue(const FWandererAudioAction& AudioAction)
    {
        using ESource = EWandererAudioValueSource;
        if (AudioAction.ValueSource == ESource::Constant)
        {
            return FMath::IsFinite(AudioAction.ConstantValue);
        }
        if (AudioAction.ValueSource == ESource::NumericContext)
        {
            return !AudioAction.bUseFallback || FMath::IsFinite(AudioAction.FallbackNumber);
        }
        if (AudioAction.ValueSource != ESource::SemanticMapping
            || !IsValid(AudioAction.NumberMapping))
        {
            return false;
        }
        const auto& Values = AudioAction.NumberMapping->Values;
        if (Values.IsEmpty()
            || (AudioAction.bUseFallback && !Values.Contains(AudioAction.FallbackKey)))
        {
            return false;
        }
        for (const auto& Entry : Values)
        {
            if (Entry.Key.IsNone() || !FMath::IsFinite(Entry.Value))
            {
                return false;
            }
        }
        return true;
    }

    bool ValidateSwitchValue(
        const FWandererAudioAction& AudioAction, const FSurfaceValues& Surfaces)
    {
        using ESource = EWandererAudioValueSource;
        if (AudioAction.ValueSource == ESource::Constant)
        {
            return IsValid(AudioAction.SwitchValue) && AudioAction.SwitchValue->GetShortID()
                   && AudioAction.SwitchValue->GetGroupID();
        }
        if (AudioAction.ValueSource == ESource::ProfileSurface)
        {
            return ValidateGroupMap(Surfaces)
                   && (!AudioAction.bUseFallback || Surfaces.Contains(SurfaceType_Default));
        }
        if (AudioAction.ValueSource != ESource::SemanticMapping
            || !IsValid(AudioAction.SwitchMapping))
        {
            return false;
        }
        const auto& Values = AudioAction.SwitchMapping->Values;
        return ValidateGroupMap(Values)
               && (!AudioAction.bUseFallback || Values.Contains(AudioAction.FallbackKey));
    }

    bool ValidateStateValue(const FWandererAudioAction& AudioAction)
    {
        using ESource = EWandererAudioValueSource;
        if (AudioAction.ValueSource == ESource::Constant)
        {
            return IsValid(AudioAction.StateValue) && AudioAction.StateValue->GetShortID()
                   && AudioAction.StateValue->GetGroupID();
        }
        if (AudioAction.ValueSource != ESource::SemanticMapping
            || !IsValid(AudioAction.StateMapping))
        {
            return false;
        }
        const auto& Values = AudioAction.StateMapping->Values;
        return ValidateGroupMap(Values)
               && (!AudioAction.bUseFallback || Values.Contains(AudioAction.FallbackKey));
    }

    bool ValidateValue(const FWandererAudioAction& AudioAction, const FSurfaceValues& Surfaces)
    {
        using ESource = EWandererAudioValueSource;
        using EType = EWandererAudioActionType;
        if (AudioAction.ValueSource != ESource::Constant
            && AudioAction.ValueSource != ESource::ProfileSurface
            && AudioAction.ContextKey.IsNone())
        {
            return false;
        }
        if (AudioAction.AudioActionType == EType::SetParameter)
        {
            return ValidateParameterValue(AudioAction);
        }
        if (AudioAction.AudioActionType == EType::SetSwitch)
        {
            return ValidateSwitchValue(AudioAction, Surfaces);
        }
        if (AudioAction.AudioActionType == EType::SetState)
        {
            return ValidateStateValue(AudioAction);
        }
        return false;
    }

    bool ValidateDefinition(const FWandererAudioAction& AudioAction, const FSurfaceValues& Surfaces)
    {
        using EType = EWandererAudioActionType;
        using EScope = EWandererAudioParameterScope;
        if (AudioAction.AudioActionID.IsNone())
        {
            return false;
        }
        if (AudioAction.Target != EWandererAudioTarget::Owner
            && AudioAction.Target != EWandererAudioTarget::ContextTarget)
        {
            return false;
        }
        if (IsParameterAudioAction(AudioAction))
        {
            if (!IsValid(AudioAction.Parameter) || !AudioAction.Parameter->GetShortID())
            {
                return false;
            }
            if (AudioAction.Scope != EScope::Emitter && AudioAction.Scope != EScope::ThisExecution
                && AudioAction.Scope != EScope::Global
                && AudioAction.Scope != EScope::ExistingPlayback)
            {
                return false;
            }
        }
        switch (AudioAction.AudioActionType)
        {
        case EType::PostEvent:
            return IsValid(AudioAction.Event) && AudioAction.Event->GetShortID();
        case EType::PostTrigger:
            return IsValid(AudioAction.Trigger) && AudioAction.Trigger->GetShortID();
        case EType::ResetParameter:
            return AudioAction.Scope != EScope::ThisExecution;
        case EType::SetParameter:
        case EType::SetSwitch:
        case EType::SetState:
            return ValidateValue(AudioAction, Surfaces);
        default:
            return false;
        }
    }

    bool ValidateList(const TArray<FWandererAudioAction>& AudioActions,
        const FSurfaceValues& Surfaces,
        FString& OutError)
    {
        if (AudioActions.IsEmpty())
        {
            OutError = TEXT("Enabled action has no audio actions.");
            return false;
        }
        TSet<FName> AudioActionIDs;
        TSet<uint32> Parameters;
        const FWandererAudioAction& Last = AudioActions.Last();
        for (int32 Index = 0; Index < AudioActions.Num(); ++Index)
        {
            const FWandererAudioAction& AudioAction = AudioActions[Index];
            const bool bSnapshot =
                AudioAction.AudioActionType == EWandererAudioActionType::SetParameter
                && AudioAction.Scope == EWandererAudioParameterScope::ThisExecution;
            const bool bInvalidEventOrder =
                AudioAction.AudioActionType == EWandererAudioActionType::PostEvent
                && Index != AudioActions.Num() - 1;
            const bool bInvalidSnapshot =
                bSnapshot
                && (Last.AudioActionType != EWandererAudioActionType::PostEvent
                    || Last.Target != AudioAction.Target);
            const uint32 ParameterID =
                IsParameterAudioAction(AudioAction) && IsValid(AudioAction.Parameter)
                    ? AudioAction.Parameter->GetShortID()
                    : 0;
            if (!ValidateDefinition(AudioAction, Surfaces)
                || AudioActionIDs.Contains(AudioAction.AudioActionID) || bInvalidEventOrder
                || bInvalidSnapshot || (ParameterID && Parameters.Contains(ParameterID)))
            {
                OutError = FString::Printf(
                    TEXT(
                        "AudioAction[%d] %s: invalid asset/value/mapping/scope, duplicate "
                        "ID/parameter, or invalid Event order. PostEvent must be last; "
                        "ThisExecution needs that Event on the same target."),
                    Index,
                    *AudioAction.AudioActionID.ToString());
                return false;
            }
            AudioActionIDs.Add(AudioAction.AudioActionID);
            if (ParameterID)
            {
                Parameters.Add(ParameterID);
            }
        }
        return true;
    }

} // namespace

bool UWandererCharacterAudioComponent::ValidateAudioActions() const
{
    for (const auto& Entry : CachedActions)
    {
        const FWandererCharacterAudioActionRow& Row = Entry.Value;
        if (!Row.bEnabled)
        {
            continue;
        }
        const TArray<FWandererAudioAction>* AudioActions =
            WandererCharacterActions::GetAudioActions(Row);
        if (!AudioActions)
        {
            return FailConfiguration(
                FString::Printf(TEXT("%s: invalid AudioActionSource or missing AudioActionsAsset."),
                    *Entry.Key.ToString()));
        }
        FString Error;
        if (!ValidateList(*AudioActions, SurfaceSwitches, Error))
        {
            return FailConfiguration(
                FString::Printf(TEXT("%s: %s"), *Entry.Key.ToString(), *Error));
        }
    }
    return true;
}
