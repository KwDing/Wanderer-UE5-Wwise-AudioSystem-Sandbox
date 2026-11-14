#include "Systems/Audio/AcousticQueryLibrary.h"

#include "AkAcousticPortal.h"
#include "AkRoomComponent.h"

FString UAcousticQueryLibrary::GetAcousticPortalRooms(AActor* PortalActor)
{
    const auto* Portal = Cast<AAkAcousticPortal>(PortalActor);
    if (!Portal || !IsValid(Portal->Portal))
    {
        return TEXT("InvalidPortal");
    }
    const auto Front = Portal->Portal->GetFrontRoomComponent();
    const auto Back = Portal->Portal->GetBackRoomComponent();
    const AActor* FrontOwner = Front.IsValid() ? Front->GetOwner() : nullptr;
    const AActor* BackOwner = Back.IsValid() ? Back->GetOwner() : nullptr;
    return FString::Printf(TEXT("%s | %s"),
        IsValid(FrontOwner) ? *FrontOwner->GetName() : TEXT("Outdoors"),
        IsValid(BackOwner) ? *BackOwner->GetName() : TEXT("Outdoors"));
}
