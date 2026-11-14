#pragma once

#include "CoreMinimal.h"

#include "FootContactTypes.generated.h"

/** Read-only snapshot for contact diagnostics; owns no objects. */
USTRUCT(BlueprintType)
struct FWandererFootContactDebug
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category = "Interaction|Footsteps")
    bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category = "Interaction|Footsteps")
    bool bArmed = false;
    UPROPERTY(BlueprintReadOnly, Category = "Interaction|Footsteps")
    bool bLandingPending = false;
    UPROPERTY(BlueprintReadOnly, Category = "Interaction|Footsteps")
    float SoleOffset = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Interaction|Footsteps")
    double LastStepTime = -1.0;
};

/** Value snapshot of existing serialized contact settings. */
struct FFootContactSettings
{
    FName LeftFootBone = TEXT("foot_l");
    FName RightFootBone = TEXT("foot_r");
    float ContactHeight = 1.0f;
    float ReleaseHeight = 4.0f;
    float GroundSupportTolerance = 12.0f;
    float ContactConfirmationSeconds = 0.015f;
    float GroundTraceHeight = 25.0f;
    float GroundTraceDepth = 60.0f;
    float WalkingGateSeconds = 0.18f;
    float RunningGateSeconds = 0.12f;
    float RunningSpeedThreshold = 245.0f;
    float SwimStrokeReachRatio = 0.8f;
    float TreadingReachRatio = 0.1f;
    float MinimumSwimHandSpeed = 5.0f;
};
