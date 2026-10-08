#pragma once
#include "CoreMinimal.h"

// Android touch feedback, independent of UE's gamepad force-feedback motors.
struct CRANKRUNTIME_API FCrankHaptics {
    static void Pulse(bool CrankTick,float Strength);
    static void Cancel();
};
