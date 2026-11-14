#include "Foundation/EditorModuleHost.h"

#include "Foundation/EditorFeatureRegistration.h"

void FEditorModuleHost::StartupModule()
{
    FEditorFeatureRegistration::StartAll();
}

void FEditorModuleHost::ShutdownModule()
{
    FEditorFeatureRegistration::StopAll();
}
