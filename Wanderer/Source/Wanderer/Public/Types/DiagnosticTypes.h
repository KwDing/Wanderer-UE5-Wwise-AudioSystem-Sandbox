#pragma once

#include "CoreMinimal.h"

enum class EDiagnosticDomain : uint8
{
    Audio,
    Events,
    Spatial
};

struct FDiagnosticRecord
{
    uint64 Sequence = 0;
    FString Json;
};

struct FDiagnosticSnapshot
{
    TArray<FDiagnosticRecord> Records;
    /** Revision of the retained history, independent of dropped records. */
    uint64 Revision = 0;
    uint64 Suppressed = 0;
    uint64 Evicted = 0;
};
