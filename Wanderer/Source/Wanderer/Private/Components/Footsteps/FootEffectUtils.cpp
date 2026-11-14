#include "Components/Footsteps/FootEffectUtils.h"

#include "Particles/ParticleSystem.h"

#include "Data/FootEffectData.h"
namespace FootEffectSettings
{
    bool HasUnmaskedSecondaryEmitter(const UParticleSystem& Template)
    {
        for (const TCHAR* Name : {TEXT("GenericDust"), TEXT("Sand"), TEXT("Snow")})
        {
            const FString Package = FString(DustRoot) + TEXT("P_Footstep_") + Name;
            if (Template.GetPathName() == Package + TEXT(".P_Footstep_") + Name)
            {
                return true;
            }
        }
        return false;
    }
} // namespace FootEffectSettings
