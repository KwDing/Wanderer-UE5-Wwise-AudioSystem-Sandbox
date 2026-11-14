#pragma once

#include "CoreMinimal.h"

/** Synchronous scope only. Async callbacks must carry an explicit value correlation separately. */
class WANDERER_API FDiagnosticTraceScope
{
  public:
    explicit FDiagnosticTraceScope(bool bInFootstep = false);
    ~FDiagnosticTraceScope();
    FDiagnosticTraceScope(const FDiagnosticTraceScope&) = delete;
    FDiagnosticTraceScope& operator=(const FDiagnosticTraceScope&) = delete;

  private:
    uint64 Previous = 0;
    bool bPreviousFootstep = false;
};
