#include "CrankAudioWave.h"
#include "Misc/ScopeLock.h"

void UCrankAudioWave::PushPCM(const uint8* Data,int32 Bytes) {
    constexpr int32 MaxQueuedBytes=44100; // 250 ms of 44.1 kHz stereo int16 PCM.
    if(!Data || Bytes<=0 || Bytes>MaxQueuedBytes || Bytes%sizeof(int16))return;
    FScopeLock Lock(&BufferMutex);
    if(PCM.Num()+Bytes>MaxQueuedBytes)PCM.Reset();
    PCM.Append(Data,Bytes);
}
void UCrankAudioWave::FlushPCM() {
    FScopeLock Lock(&BufferMutex);PCM.Reset();
}
int32 UCrankAudioWave::GeneratePCMData(uint8* Data,int32 SamplesNeeded) {
    if(!Data || SamplesNeeded<=0 || SamplesNeeded>MAX_int32/int32(sizeof(int16)))return 0;
    const int32 Bytes=SamplesNeeded*sizeof(int16);
    FMemory::Memzero(Data,Bytes);
    FScopeLock Lock(&BufferMutex);
    const int32 Copied=FMath::Min(Bytes,PCM.Num());
    if(Copied){FMemory::Memcpy(Data,PCM.GetData(),Copied);PCM.RemoveAt(0,Copied,EAllowShrinking::No);}
    return Bytes;
}
