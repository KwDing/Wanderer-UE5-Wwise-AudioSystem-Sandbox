#pragma once

#include "CoreMinimal.h"

#include "Types/DiagnosticTypes.h"

class UObject;
class FDiagnosticTraceScope;

WANDERER_API DECLARE_LOG_CATEGORY_EXTERN(LogWandererAudio, Warning, All);
WANDERER_API DECLARE_LOG_CATEGORY_EXTERN(LogWandererEvents, Warning, All);

/** Game-thread value recorder. Never calls gameplay observers or retains source objects. */
class WANDERER_API FDiagnosticRecorder
{
  public:
    static bool IsEnabled(EDiagnosticDomain InDomain, bool bInProblem = false);
    static bool IsFootstepsEnabled();
    static void SetFootstepsEnabled(bool bInEnabled);
    /** Detailed captures expire; problems remain available after expiration. */
    static void SetPreset(int32 InPreset);
    static int32 GetPreset();
    static void SetFocus(const FString& InFocus);
    static FString GetFocus();
    static bool Admit(const UObject* InContext, EDiagnosticDomain InDomain, bool bInProblem);
    /** Low-level sink: caller must first Admit; only bounded tests may bypass admission. */
    static void Record(const UObject* InContext,
        EDiagnosticDomain InDomain,
        const TCHAR* InStage,
        const FString& InDetail,
        bool bInProblem = false);
    /** Stable state only: do not pass frame-varying positions as the transition key. */
    static bool HasTransition(const UObject* InContext, const FString& InState);
    static void CommitTransition(const UObject* InContext, const FString& InState);
    /** Changes only when retained records change or are cleared. */
    static uint64 GetRevision();
    static uint64 GetSuppressedCount();
    static FDiagnosticSnapshot GetSnapshot();
    static void Clear();
    static uint64 GetTrace();

  private:
    friend class FDiagnosticTraceScope;

    static bool IsFootstepTrace();
    static uint64 BeginTrace(bool bInFootstep);
    static void RestoreTrace(uint64 InTrace, bool bInFootstep);
};

#if !UE_BUILD_SHIPPING
// The payload is evaluated only after filtering and rate admission.
#define WANDERER_DIAGNOSTIC(Context, Domain, Stage, Problem, Detail)                               \
    do                                                                                             \
    {                                                                                              \
        if (FDiagnosticRecorder::Admit(Context, Domain, Problem))                                  \
        {                                                                                          \
            FDiagnosticRecorder::Record(Context, Domain, Stage, Detail, Problem);                  \
        }                                                                                          \
    } while (false)
#else
#define WANDERER_DIAGNOSTIC(Context, Domain, Stage, Problem, Detail)                               \
    do                                                                                             \
    {                                                                                              \
    } while (false)
#endif
