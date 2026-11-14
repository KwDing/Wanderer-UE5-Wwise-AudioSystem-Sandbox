#pragma once

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Only exact per-run manifests qualify; production backups and mixed batches are preserved.
inline void RemoveWwiseTestBackups(const FString& RunID)
{
	for (const FString& Area : {TEXT("WwiseEventSync"), TEXT("WwiseBrowserSync")})
	{
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / Area / TEXT("Backups"));
		TArray<FString> Directories;
		IFileManager::Get().FindFiles(Directories, *(Root / TEXT("*")), false, true);
		for (const FString& Name : Directories)
		{
			FGuid Batch;
			if (!FGuid::ParseExact(Name, EGuidFormats::Digits, Batch)) continue;
			const FString Directory = Root / Name;
			TArray<FString> Manifest;
			if (!FFileHelper::LoadFileToStringArray(Manifest, *(Directory / TEXT("Manifest.txt"))) || Manifest.IsEmpty()) continue;
			const bool bOnlyThisRun = !Manifest.ContainsByPredicate([RunID](const FString& Line)
			{
				FString Source, Target;
				if (!Line.Split(TEXT(" => "), &Source, &Target)) return true;
				return !Source.Contains(TEXT("/") + RunID + TEXT("/")) && !Source.Contains(TEXT("/__SyncAutomation_") + RunID + TEXT("/"));
			});
			if (bOnlyThisRun) IFileManager::Get().DeleteDirectory(*Directory, false, true);
		}
	}
}
