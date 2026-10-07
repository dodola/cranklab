#pragma once
#include "CoreMinimal.h"

struct CRANKRUNTIME_API FCrankImportResult {
    int State=0; // idle, selecting, copying, complete, error, cancelled
    FString Directory, Message;
    bool CanRetry=false;
};

// PDX directories passed to the C runtime must exist on the physical disk.
struct CRANKRUNTIME_API FCrankGameFiles {
    static FString Root();
    static FString PrepareFixture(FString& Error);
    static FString PrepareLauncher(FString& Error);
    // Validates ZIP paths/CRC/budgets and publishes a complete PDX atomically.
    static FString InstallArchive(const FString& Archive,const FString& Library,FString& Error,const TAtomic<bool>* Cancelled=nullptr);
    static FString Title(const FString& Directory);
    static FString SaveDirectory(const FString& Directory);
    static bool CoverPixels(const FString& Directory,FIntPoint& Size,TArray<uint8>& BGRA);
    static FString Capability(const FString& Directory);
    static bool BeginImport(bool Folder);
    static bool BeginUrlImport(const FString& Url);
    static bool RetryImport();
    static FCrankImportResult PollImport();
};
