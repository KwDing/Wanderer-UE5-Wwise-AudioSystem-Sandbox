#include "Components/CharacterAudio/CharacterAudioActionUtils.h"

namespace WandererCharacterActions
{
    const TArray<FWandererAudioAction>* GetAudioActions(const FWandererCharacterAudioActionRow& Row)
    {
        if (Row.AudioActionSource == EWandererAudioActionSource::Inline)
        {
            return &Row.InlineAudioActions;
        }
        if (Row.AudioActionSource == EWandererAudioActionSource::Asset
            && IsValid(Row.AudioActionsAsset))
        {
            return &Row.AudioActionsAsset->AudioActions;
        }
        return nullptr;
    }

    bool IsParameterAudioAction(const FWandererAudioAction& AudioAction)
    {
        return AudioAction.AudioActionType == EWandererAudioActionType::SetParameter
               || AudioAction.AudioActionType == EWandererAudioActionType::ResetParameter;
    }

} // namespace WandererCharacterActions
