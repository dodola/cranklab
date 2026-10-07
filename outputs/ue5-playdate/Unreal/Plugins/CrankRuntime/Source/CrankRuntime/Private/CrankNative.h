#pragma once
#include "CoreMinimal.h"
struct FCrankNative {
    void* Handle = nullptr;
    int (*Start)(const char*, const char*) = nullptr;
    int (*StartLauncher)(const char*, const char*) = nullptr;
    uint32 (*LauncherRequest)(char*,uint32) = nullptr;
    int (*LauncherReply)(uint32,const char*) = nullptr;
    int (*LauncherActivate)() = nullptr;
    int (*LauncherDetailAction)(uint32) = nullptr;
    void (*Stop)() = nullptr;
    int (*Status)() = nullptr;
    void (*Suspend)(uint32) = nullptr;
    void (*Buttons)(uint32) = nullptr;
    void (*Crank)(float, int) = nullptr;
    uint32 (*Frame)(uint8*, uint32) = nullptr;
    uint32 (*Audio)(int16*, uint32) = nullptr;
    void (*Error)(char*, uint32) = nullptr;
    uint32 (*NativeInfo)(char*,uint32) = nullptr;
    uint32 (*Menu)(char*,uint32) = nullptr;
    uint32 (*MenuImage)(uint8*,uint32) = nullptr;
    void (*MenuOpen)(int) = nullptr;
    int (*MenuSelect)(uint32,int) = nullptr;
    bool Load();
    void Unload();
    static FCrankNative& Get();
};
