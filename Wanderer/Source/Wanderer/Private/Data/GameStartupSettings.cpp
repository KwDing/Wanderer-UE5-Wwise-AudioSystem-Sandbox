#include "Data/GameStartupSettings.h"

UGameStartupSettings::UGameStartupSettings()
{
    DefaultPawnClassSoftPtr = FSoftClassPath(
        TEXT("/Game/Blueprints/Characters/Female/BP_FemaleCharacter.BP_FemaleCharacter_C"));
}
