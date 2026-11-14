#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** Static module-local registration. Callbacks and feature objects outlive this registration. */
class FEditorFeatureRegistration
{
public:
    FEditorFeatureRegistration(FName InName,
        TFunction<void()> InStart,
        TFunction<void()> InStop,
        bool bInCommandlet = false);
    ~FEditorFeatureRegistration();

    FEditorFeatureRegistration(const FEditorFeatureRegistration&) = delete;
    FEditorFeatureRegistration& operator=(const FEditorFeatureRegistration&) = delete;

    static void StartAll();
    static void StopAll();

private:
    FName Name;
    TFunction<void()> StartCallback;
    TFunction<void()> StopCallback;
    bool bCommandlet = false;
    bool bStarted = false;

    void Start();
    void Stop();
};
