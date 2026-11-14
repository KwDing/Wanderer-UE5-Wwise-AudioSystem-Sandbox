#include "Core/Diagnostics/DiagnosticTraceScope.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"

FDiagnosticTraceScope::FDiagnosticTraceScope(bool bInFootstep)
{
#if !UE_BUILD_SHIPPING
    check(IsInGameThread());
    bPreviousFootstep = FDiagnosticRecorder::IsFootstepTrace();
    Previous = FDiagnosticRecorder::BeginTrace(bInFootstep);
#endif
}

FDiagnosticTraceScope::~FDiagnosticTraceScope()
{
#if !UE_BUILD_SHIPPING
    FDiagnosticRecorder::RestoreTrace(Previous, bPreviousFootstep);
#endif
}
