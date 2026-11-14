#include "Editing/ActorFolderLibrary.h"

#include "Editor.h"
#include "EditorActorFolders.h"
#include "EngineUtils.h"


TArray<FString> UActorFolderLibrary::GetEditorFolderPaths()
{
    TArray<FString> Result;
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
    {
        return Result;
    }
    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        FActorFolders::Get().ForEachFolder(*World,
            [&Result](const FFolder& Folder)
            {
                Result.AddUnique(Folder.GetPath().ToString());
                return true;
            });
    }
    Result.Sort();
    return Result;
}

int32 UActorFolderLibrary::RemoveEmptyEditorFolders(const TArray<FString>& Paths)
{
    int32 Removed = 0;
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
    {
        return 0;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return 0;
    }
    TArray<FFolder> Folders;
    FActorFolders::Get().ForEachFolder(*World,
        [&Folders, &Paths](const FFolder& Folder)
        {
            if (Paths.Contains(Folder.GetPath().ToString()))
            {
                Folders.Add(Folder);
            }
            return true;
        });
    Folders.Sort(
        [](const FFolder& A, const FFolder& B)
        {
            return A.GetPath().ToString().Len() > B.GetPath().ToString().Len();
        });
    for (const FFolder& Folder : Folders)
    {
        const FString Path = Folder.GetPath().ToString();
        bool bOccupied = false;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetFolder() == Folder
                || It->GetFolderPath().ToString().StartsWith(Path + TEXT("/")))
            {
                bOccupied = true;
                break;
            }
        }
        if (bOccupied)
        {
            continue;
        }
        FActorFolders::Get().DeleteFolder(*World, Folder);
        if (!FActorFolders::Get().ContainsFolder(*World, Folder))
        {
            ++Removed;
        }
    }
    return Removed;
}
