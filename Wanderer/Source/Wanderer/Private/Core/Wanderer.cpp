#include "Core/Wanderer.h"

#include "Modules/ModuleManager.h"

#include "Core/ModuleLifecycleBroadcaster.h"

class FWandererModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FModuleLifecycleBroadcaster::BroadcastStarted();
    }

    virtual void ShutdownModule() override
    {
        FModuleLifecycleBroadcaster::BroadcastStopped();
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FWandererModule, Wanderer, "Wanderer");
