#include "Data/WandererMapMarkerSettings.h"

#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

const FWandererMapDoorMarker* UWandererMapMarkerSettings::FindDoorMarker(
    const UWorld& InWorld, FName InID) const
{
    const FString ScenePackage = GetScenePackage(InWorld);
    return DoorMarkers.FindByPredicate(
        [InID, &ScenePackage](const FWandererMapDoorMarker& Marker)
        {
            return !InID.IsNone() && Marker.ID == InID && Marker.ScenePackage == ScenePackage;
        });
}

FString UWandererMapMarkerSettings::GetScenePackage(const UWorld& InWorld)
{
    return UWorld::RemovePIEPrefix(InWorld.GetOutermost()->GetName());
}

FText UWandererMapMarkerSettings::GetSceneTitle(const FString& InScenePackage) const
{
    const FString Package =
        UWorld::RemovePIEPrefix(FPackageName::ObjectPathToPackageName(InScenePackage));
    if (const FString* Title = SceneTitles.Find(Package))
    {
        return FText::FromString(*Title);
    }
    FString Title = FPackageName::GetShortName(Package);
    Title.ReplaceInline(TEXT("_"), TEXT(" "));
    return FText::FromString(Title.IsEmpty() ? TEXT("WANDERER") : Title.ToUpper());
}
