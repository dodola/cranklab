#pragma once
#include "Sound/SoundWaveProcedural.h"
#include "CrankAudioWave.generated.h"

// The game thread produces/flushes PCM while the audio renderer consumes it.
// USoundWaveProcedural::ResetAudio drains a queue with a single consumer;
// calling it alongside the audio renderer can free a node still in use.
UCLASS()
class UCrankAudioWave : public USoundWaveProcedural {
    GENERATED_BODY()
public:
    void PushPCM(const uint8* Data,int32 Bytes);
    void FlushPCM();
    virtual int32 GeneratePCMData(uint8* Data,int32 SamplesNeeded) override;
private:
    FCriticalSection BufferMutex;
    TArray<uint8> PCM;
};
