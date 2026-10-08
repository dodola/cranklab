#pragma once
#include "CoreMinimal.h"

namespace CrankModelDownload {
    struct FSnapshot {int State=4; int64 Bytes=0; FString Error;};
#if PLATFORM_ANDROID
    bool HasWifi();
    int64 Begin(const FString& Url,const FString& Partial,int64 Size);
    FSnapshot Poll(int64 Job);
    void Cancel(int64 Job);
    void Release(int64 Job);
#endif
}
