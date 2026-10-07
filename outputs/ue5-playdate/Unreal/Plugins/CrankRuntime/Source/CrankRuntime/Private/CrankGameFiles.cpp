#include "CrankGameFiles.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/Compression.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"

static FString JavaString(JNIEnv* Env,jstring String) {
    if (!String) return FString();
    const char* Text=Env->GetStringUTFChars(String,nullptr);
    FString Result=Text ? UTF8_TO_TCHAR(Text) : TEXT("");
    if (Text) Env->ReleaseStringUTFChars(String,Text);
    Env->DeleteLocalRef(String); return Result;
}
static jclass ImportClass(JNIEnv* Env) {
    auto Class=AndroidJavaEnv::FindJavaClass("com.cranklab.CrankPdxImport");
    if (Env->ExceptionCheck()) { Env->ExceptionClear(); return nullptr; }
    return Class;
}
#endif

FString FCrankGameFiles::Root() {
#if !UE_BUILD_SHIPPING
    FString ProbeRoot;
    if(FParse::Value(FCommandLine::Get(),TEXT("CrankLibraryRoot="),ProbeRoot)) return FPaths::ConvertRelativePathToFull(ProbeRoot);
#endif
#if PLATFORM_ANDROID
    auto* Env=AndroidJavaEnv::GetJavaEnv(); if (!Env) return FString();
    jclass Class=ImportClass(Env); if (!Class) return FString();
    jmethodID Method=Env->GetStaticMethodID(Class,"root","(Landroid/app/Activity;)Ljava/lang/String;");
    FString Result;
    if (Method) Result=JavaString(Env,static_cast<jstring>(Env->CallStaticObjectMethod(Class,Method,AndroidJavaEnv::GetGameActivityThis())));
    if (Env->ExceptionCheck()) { Env->ExceptionClear(); Result.Empty(); }
    Env->DeleteLocalRef(Class); return Result;
#else
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("CrankLab"));
#endif
}

static FString Metadata(const FString& Directory,const TCHAR* Key) {
    FString Info; FFileHelper::LoadFileToString(Info,*(Directory/TEXT("pdxinfo")));
    TArray<FString> Lines; Info.ParseIntoArrayLines(Lines);
    const FString Prefix=FString(Key)+TEXT("=");
    for (const FString& Line:Lines) if (Line.StartsWith(Prefix)) return Line.Mid(Prefix.Len()).TrimStartAndEnd();
    return FString();
}
FString FCrankGameFiles::Title(const FString& Directory) {
    FString Name=Metadata(Directory,TEXT("name"));
    return Name.IsEmpty() ? FPaths::GetBaseFilename(Directory) : Name;
}
FString FCrankGameFiles::SaveDirectory(const FString& Directory) {
    FString Identity=Metadata(Directory,TEXT("bundleID"));
    if (Identity.IsEmpty()) Identity=Metadata(Directory,TEXT("name"))+TEXT("|")+Metadata(Directory,TEXT("author"));
    if (Identity==TEXT("|")) Identity=Directory;
    const FString Storage=Root();
    return Storage.IsEmpty() ? FString() : Storage/TEXT("GameSaves")/FMD5::HashAnsiString(*Identity);
}
FString FCrankGameFiles::Capability(const FString& Directory) {
    const bool Lua=FPaths::FileExists(Directory/TEXT("main.pdz")) || FPaths::FileExists(Directory/TEXT("main.lua")) || FPaths::FileExists(Directory/TEXT("main.luac"));
    if(FPaths::FileExists(Directory/TEXT("pdex.bin")))return Lua ? TEXT("Lua + C 游戏 · 兼容性待验证") : TEXT("C 游戏 · 兼容性待验证");
    if(!Lua) return TEXT("缺少可运行的游戏入口");
    if(FPaths::FileExists(Directory/TEXT("pdex.dylib")) || FPaths::FileExists(Directory/TEXT("pdex.dll")))return TEXT("Lua 入口 · 含桌面 C 扩展");
    return TEXT("Lua 游戏");
}
bool FCrankGameFiles::CoverPixels(const FString& Directory,FIntPoint& Size,TArray<uint8>& BGRA) {
    FString Images=Metadata(Directory,TEXT("imagePath")); if(Images.IsEmpty()) Images=TEXT(".");
    if(!FPaths::IsRelative(Images)) return false;
    FString Path=FPaths::ConvertRelativePathToFull(Directory/Images/TEXT("card.pdi")); FPaths::CollapseRelativeDirectories(Path);
    if(!FPaths::IsUnderDirectory(Path,FPaths::ConvertRelativePathToFull(Directory))) return false;
    const int64 FileSize=IFileManager::Get().FileSize(*Path); if(FileSize<32 || FileSize>1024*1024) return false;
    TArray<uint8> Raw,Cell; if(!FFileHelper::LoadFileToArray(Raw,*Path)) return false;
    if(FMemory::Memcmp(Raw.GetData(),"Playdate IMG",12)!=0 && FMemory::Memcmp(Raw.GetData(),"Playdate PI",11)!=0) return false;
    auto U32=[](const uint8* P){return uint32(P[0]) | (uint32(P[1])<<8) | (uint32(P[2])<<16) | (uint32(P[3])<<24);};
    if(U32(Raw.GetData()+12)&0x80000000u) {
        const uint32 Length=U32(Raw.GetData()+16); if(Length<16 || Length>1024*1024) return false;
        Cell.SetNumUninitialized(Length);
        if(!FCompression::UncompressMemory(NAME_Zlib,Cell.GetData(),Cell.Num(),Raw.GetData()+32,Raw.Num()-32)) return false;
    } else Cell.Append(Raw.GetData()+16,Raw.Num()-16);
    if(Cell.Num()<16) return false;
    auto U16=[&](int I){return int(Cell[I]) | (int(Cell[I+1])<<8);};
    const int W=U16(0),H=U16(2),Stride=U16(4),Left=U16(6),Right=U16(8),Top=U16(10),Bottom=U16(12),Flags=U16(14);
    Size={Left+W+Right,Top+H+Bottom};
    if(Size.X<1 || Size.Y<1 || Size.X>512 || Size.Y>512 || Stride<(W+7)/8 || int64(Stride)*H>Cell.Num()-16) return false;
    const int Bytes=Stride*H; const bool Mask=(Flags&3)!=0;
    if(Mask && int64(Bytes)*2>Cell.Num()-16) return false;
    BGRA.Init(255,Size.X*Size.Y*4);
    for(int Y=0;Y<H;Y++) for(int X=0;X<W;X++) {
        const int Byte=Y*Stride+X/8,Bit=7-X%8,Pixel=((Top+Y)*Size.X+Left+X)*4;
        const bool Visible=!Mask || (Cell[16+Bytes+Byte]&(1<<Bit))!=0;
        const uint8 Value=Visible && !(Cell[16+Byte]&(1<<Bit)) ? 0 : 255;
        BGRA[Pixel]=BGRA[Pixel+1]=BGRA[Pixel+2]=Value;
    }
    return true;
}

static FString PrepareBundled(const FString& Name,FString& Error) {
    const FString Storage=FCrankGameFiles::Root();
    if (Storage.IsEmpty()) { Error=TEXT("无法访问应用的游戏目录。"); return FString(); }
    const FString Source=FPaths::ProjectContentDir()/TEXT("Games")/(Name+TEXT(".pdx"));
    FString Text; TSharedPtr<FJsonObject> Manifest;
    if (!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Games")/(Name+TEXT("-manifest.json")))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Manifest)) {
        Error=TEXT("内置游戏资源不完整，请重新安装应用。"); return FString();
    }
    const FString Target=Storage/TEXT("Games")/(Name+TEXT("_")+FMD5::HashAnsiString(*Text).Left(12)+TEXT(".pdx"));
    // Publish readiness after every file was copied; a failed first launch
    // must retry missing audio/resources instead of accepting a partial PDX.
    if (FPaths::FileExists(Target/TEXT(".ready"))) return Target;
    IFileManager::Get().MakeDirectory(*Target,true);
    for (const auto& File:Manifest->GetArrayField(TEXT("files"))) {
        FString Relative=File->AsString();
        if (Relative.IsEmpty() || Relative.Contains(TEXT("..")) || !FPaths::IsRelative(Relative)) {
            Error=TEXT("内置游戏清单无效。"); return FString();
        }
        TArray<uint8> Data;
        const FString Destination=Target/Relative;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination),true);
        if (!FFileHelper::LoadFileToArray(Data,*(Source/Relative)) || !FFileHelper::SaveArrayToFile(Data,*Destination)) {
            Error=TEXT("无法准备内置游戏，请检查可用存储空间。"); return FString();
        }
    }
    if (!FFileHelper::SaveStringToFile(TEXT("ready"),*(Target/TEXT(".ready")))) {
        Error=TEXT("无法完成内置游戏安装。"); return FString();
    }
    return Target;
}
FString FCrankGameFiles::PrepareFixture(FString& Error) {return PrepareBundled(TEXT("fixture"),Error);}
FString FCrankGameFiles::PrepareLauncher(FString& Error) {return PrepareBundled(TEXT("launcher"),Error);}
bool FCrankGameFiles::BeginImport(bool Folder) {
#if PLATFORM_ANDROID
    auto* Env=AndroidJavaEnv::GetJavaEnv(); if (!Env) return false;
    jclass Class=ImportClass(Env); if (!Class) return false;
    jmethodID Method=Env->GetStaticMethodID(Class,"open","(Landroid/app/Activity;Z)Z");
    bool Started=Method && Env->CallStaticBooleanMethod(Class,Method,AndroidJavaEnv::GetGameActivityThis(),Folder);
    if (Env->ExceptionCheck()) { Env->ExceptionClear(); Started=false; }
    Env->DeleteLocalRef(Class); return Started;
#else
    return false;
#endif
}
FCrankImportResult FCrankGameFiles::PollImport() {
    FCrankImportResult Result;
#if PLATFORM_ANDROID
    auto* Env=AndroidJavaEnv::GetJavaEnv(); if (!Env) return Result;
    jclass Class=ImportClass(Env); if (!Class) return Result;
    jmethodID Method=Env->GetStaticMethodID(Class,"snapshot","()[Ljava/lang/String;");
    auto Array=Method ? static_cast<jobjectArray>(Env->CallStaticObjectMethod(Class,Method)) : nullptr;
    if (Array && Env->GetArrayLength(Array)>=3) {
        Result.State=FCString::Atoi(*JavaString(Env,static_cast<jstring>(Env->GetObjectArrayElement(Array,0))));
        Result.Directory=JavaString(Env,static_cast<jstring>(Env->GetObjectArrayElement(Array,1)));
        Result.Message=JavaString(Env,static_cast<jstring>(Env->GetObjectArrayElement(Array,2)));
        if(Env->GetArrayLength(Array)>=4) Result.CanRetry=JavaString(Env,static_cast<jstring>(Env->GetObjectArrayElement(Array,3)))==TEXT("1");
        if (Result.State>=3) {
            auto Clear=Env->GetStaticMethodID(Class,"acknowledge","()V");
            if (Clear && Result.State!=4) Env->CallStaticVoidMethod(Class,Clear);
        }
    }
    if (Array) Env->DeleteLocalRef(Array);
    if (Env->ExceptionCheck()) Env->ExceptionClear();
    Env->DeleteLocalRef(Class);
#endif
    return Result;
}
bool FCrankGameFiles::BeginUrlImport(const FString& Url) {
#if PLATFORM_ANDROID
    auto* Env=AndroidJavaEnv::GetJavaEnv(); if(!Env) return false;
    jclass Class=ImportClass(Env); if(!Class) return false;
    jmethodID Method=Env->GetStaticMethodID(Class,"download","(Landroid/app/Activity;Ljava/lang/String;)Z");
    jstring Text=Env->NewStringUTF(TCHAR_TO_UTF8(*Url));
    bool Started=Method && Text && Env->CallStaticBooleanMethod(Class,Method,AndroidJavaEnv::GetGameActivityThis(),Text);
    if(Text) Env->DeleteLocalRef(Text); if(Env->ExceptionCheck()) {Env->ExceptionClear(); Started=false;}
    Env->DeleteLocalRef(Class); return Started;
#else
    return false;
#endif
}
bool FCrankGameFiles::RetryImport() {
#if PLATFORM_ANDROID
    auto* Env=AndroidJavaEnv::GetJavaEnv(); if(!Env) return false;
    jclass Class=ImportClass(Env); if(!Class) return false;
    jmethodID Method=Env->GetStaticMethodID(Class,"retry","(Landroid/app/Activity;)Z");
    bool Started=Method && Env->CallStaticBooleanMethod(Class,Method,AndroidJavaEnv::GetGameActivityThis());
    if(Env->ExceptionCheck()) {Env->ExceptionClear(); Started=false;}
    Env->DeleteLocalRef(Class); return Started;
#else
    return false;
#endif
}
