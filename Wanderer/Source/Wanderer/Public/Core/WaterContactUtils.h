#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;

/** Posed body geometry at the water plane. Values only, no retained asset references. */
struct FWandererWaterBodySection
{
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ZeroVector;
    float Radius = 0.0f;
};

namespace UE::Wanderer::Water
{
    constexpr int32 MaxBodySections = 24;
    WANDERER_API void SampleBodySections(const USkeletalMeshComponent& Mesh,
        float WaterHeight,
        TArray<FWandererWaterBodySection>& OutSections);
    WANDERER_API FBox GetSectionBounds(const TArray<FWandererWaterBodySection>& Sections);
    WANDERER_API FVector ProjectToBodyEdge(
        const FVector& Point, const TArray<FWandererWaterBodySection>& Sections);
} // namespace UE::Wanderer::Water
