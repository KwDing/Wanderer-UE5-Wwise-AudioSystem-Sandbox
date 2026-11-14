#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ActorFolderLibrary.generated.h"

UCLASS()
class UActorFolderLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "World Audio|Editor")
    static TArray<FString> GetEditorFolderPaths();

    UFUNCTION(BlueprintCallable, Category = "World Audio|Editor")
    static int32 RemoveEmptyEditorFolders(const TArray<FString>& Paths);
};
