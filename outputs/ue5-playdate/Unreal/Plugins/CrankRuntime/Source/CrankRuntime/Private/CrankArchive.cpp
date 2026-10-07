#include "CrankGameFiles.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "zlib.h"

// Central-directory sizes are checked before allocation. ZIP64, encryption,
// symlinks and unsupported compression are rejected rather than guessed.
namespace {
constexpr int64 MaxBytes=512ll*1024*1024;
uint16 U16(const uint8* P) {return uint16(P[0])|(uint16(P[1])<<8);}
uint32 U32(const uint8* P) {return uint32(U16(P))|(uint32(U16(P+2))<<16);}
bool SafePath(const FString& Name) {
    if(Name.IsEmpty() || Name.StartsWith(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT(":"))) return false;
    TArray<FString> Parts;Name.ParseIntoArray(Parts,TEXT("/"),false);
    if(Parts.Num()>32) return false;
    for(const auto& Part:Parts) {
        if(Part.IsEmpty() || Part==TEXT(".") || Part==TEXT("..") || Part.EndsWith(TEXT(".")) || Part.EndsWith(TEXT(" "))) return false;
        for(TCHAR C:Part) if(C<32 || C==127 || C==TEXT('*') || C==TEXT('?') || C==TEXT('"') || C==TEXT('<') || C==TEXT('>') || C==TEXT('|')) return false;
        const FString Base=FPaths::GetBaseFilename(Part).ToUpper();
        if(Base==TEXT("CON") || Base==TEXT("PRN") || Base==TEXT("AUX") || Base==TEXT("NUL") ||
          (Base.Len()==4 && (Base.StartsWith(TEXT("COM")) || Base.StartsWith(TEXT("LPT"))) && Base[3]>=TEXT('1') && Base[3]<=TEXT('9'))) return false;
    }
    return true;
}
}
FString FCrankGameFiles::InstallArchive(const FString& Archive,const FString& Library,FString& Error,const TAtomic<bool>* Cancelled) {
    auto& Files=IFileManager::Get();
    TUniquePtr<FArchive> Input(Files.CreateFileReader(*Archive));
    if(!Input || Input->TotalSize()<22 || Input->TotalSize()>MaxBytes) {Error=TEXT("所选文件不是有效 ZIP，或超过 512 MB。");return {};}
    const FString Base=FPaths::ConvertRelativePathToFull(Library);
    const FString Stage=Base/(TEXT(".import-")+FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if(!Files.MakeDirectory(*Stage,true)) {Error=TEXT("无法创建游戏临时目录。");return {};}
    auto Aborted=[&](){return Cancelled && Cancelled->Load();};
    auto Fail=[&](const TCHAR* Message)->FString {
        Error=Aborted() ? TEXT("已取消下载。") : Message;
        // Stage is generated immediately below the caller's physical library.
        Files.DeleteDirectory(*Stage,false,true);return {};
    };
    const int64 TailSize=FMath::Min<int64>(Input->TotalSize(),65557);
    TArray<uint8> Tail;Tail.SetNumUninitialized(TailSize);Input->Seek(Input->TotalSize()-TailSize);Input->Serialize(Tail.GetData(),TailSize);
    int End=-1;
    for(int I=Tail.Num()-22;I>=0;I--) if(U32(Tail.GetData()+I)==0x06054b50 && I+22+U16(Tail.GetData()+I+20)==Tail.Num()) {End=I;break;}
    if(End<0) return Fail(TEXT("下载的文件不是完整 ZIP 游戏包。"));
    const uint8* E=Tail.GetData()+End;const uint32 Count=U16(E+10),Size=U32(E+12),Offset=U32(E+16);
    const int64 EndOffset=Input->TotalSize()-TailSize+End;
    if(U16(E+4) || U16(E+6) || U16(E+8)!=Count || !Count || Count>20000 || Size>16*1024*1024 || uint64(Offset)+Size!=uint64(EndOffset)) return Fail(TEXT("不支持此 ZIP 格式或文件数量过多。"));
    TArray<uint8> Directory;Directory.SetNumUninitialized(Size);Input->Seek(Offset);Input->Serialize(Directory.GetData(),Size);
    int64 Total=0;int Position=0;TSet<FString> Names;TArray<FString> PdxRoots;
    uint8 Buffer[32768];
    for(uint32 Index=0;Index<Count;Index++) {
        if(Aborted()) return Fail(TEXT("已取消下载。"));
        if(Position+46>Directory.Num()) return Fail(TEXT("ZIP 目录损坏。"));
        const uint8* C=Directory.GetData()+Position;
        if(U32(C)!=0x02014b50) return Fail(TEXT("ZIP 目录损坏。"));
        const uint16 Flags=U16(C+8),Method=U16(C+10),NameSize=U16(C+28),Extra=U16(C+30),Comment=U16(C+32);
        const uint32 CRC=U32(C+16),Compressed=U32(C+20),Expanded=U32(C+24),Local=U32(C+42);
        if(Position+46+NameSize+Extra+Comment>Directory.Num() || (Flags&1) || (Method!=0 && Method!=8) || U16(C+34) || (U32(C+38)>>16 & 0170000)==0120000)
            return Fail(TEXT("ZIP 文件损坏、加密或含不支持的条目。"));
        for(uint16 N=0;N<NameSize;N++) if(!C[46+N]) return Fail(TEXT("ZIP 包含无效文件名。"));
        FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(C+46),NameSize);FString Name(Convert.Length(),Convert.Get());
        const bool IsDir=Name.EndsWith(TEXT("/"));if(IsDir) Name.LeftChopInline(1);
        if(!SafePath(Name) || Names.Contains(Name.ToLower())) return Fail(TEXT("ZIP 包含越界、重复或无效路径。"));
        Names.Add(Name.ToLower());Position+=46+NameSize+Extra+Comment;
        if(Name.StartsWith(TEXT("__MACOSX/")) || Name==TEXT("__MACOSX") || FPaths::GetCleanFilename(Name)==TEXT(".DS_Store")) continue;
        Total+=Expanded;if(Total>MaxBytes || uint64(Local)+30>Offset) return Fail(TEXT("ZIP 超过 512 MB，或条目大小无效。"));
        uint8 Header[30];Input->Seek(Local);Input->Serialize(Header,30);
        if(U32(Header)!=0x04034b50 || U16(Header+8)!=Method || U16(Header+6)!=Flags || U16(Header+26)!=NameSize || uint64(Local)+30+NameSize+U16(Header+28)+Compressed>Offset)
            return Fail(TEXT("ZIP 条目头部无效。"));
        TArray<uint8> LocalName;LocalName.SetNumUninitialized(NameSize);Input->Serialize(LocalName.GetData(),NameSize);
        if(FMemory::Memcmp(LocalName.GetData(),C+46,NameSize)!=0) return Fail(TEXT("ZIP 条目路径不一致。"));
        Input->Seek(uint64(Local)+30+NameSize+U16(Header+28));
        const FString Destination=Stage/Name;
        if(IsDir) {if(Expanded || !Files.MakeDirectory(*Destination,true)) return Fail(TEXT("ZIP 目录无效。"));continue;}
        if(!Files.MakeDirectory(*FPaths::GetPath(Destination),true)) return Fail(TEXT("无法创建游戏目录。"));
        TUniquePtr<FArchive> Output(Files.CreateFileWriter(*Destination));if(!Output) return Fail(TEXT("无法写入游戏，请检查存储空间。"));
        z_stream Stream{};bool Inflating=Method==8;if(Inflating && inflateInit2(&Stream,-MAX_WBITS)!=Z_OK) return Fail(TEXT("无法解压游戏。"));
        uint32 Remaining=Compressed,Written=0;uLong ActualCRC=crc32(0,nullptr,0);int Result=Z_OK;uint8 ExpandedBuffer[32768];
        bool Valid=true;
        while(Remaining && Valid && !Aborted()) {
            const uint32 Chunk=FMath::Min<uint32>(Remaining,sizeof(Buffer));Input->Serialize(Buffer,Chunk);Remaining-=Chunk;
            if(Method==0) {
                if(uint64(Written)+Chunk>Expanded) {Valid=false;break;}
                Output->Serialize(Buffer,Chunk);ActualCRC=crc32(ActualCRC,Buffer,Chunk);Written+=Chunk;
            } else {
                Stream.next_in=Buffer;Stream.avail_in=Chunk;
                do {
                    Stream.next_out=ExpandedBuffer;Stream.avail_out=sizeof(ExpandedBuffer);Result=inflate(&Stream,Z_NO_FLUSH);
                    const uint32 Produced=sizeof(ExpandedBuffer)-Stream.avail_out;
                    if(uint64(Written)+Produced>Expanded || (Result!=Z_OK && Result!=Z_STREAM_END)) {Valid=false;break;}
                    Output->Serialize(ExpandedBuffer,Produced);ActualCRC=crc32(ActualCRC,ExpandedBuffer,Produced);Written+=Produced;
                    if(Result==Z_STREAM_END) {if(Stream.avail_in || Remaining) Valid=false;break;}
                } while(Stream.avail_in || Stream.avail_out==0);
            }
            if(Input->IsError() || Output->IsError()) Valid=false;
        }
        if(Inflating) {Valid=Valid && Result==Z_STREAM_END;inflateEnd(&Stream);}
        Valid=Valid && Written==Expanded && ActualCRC==CRC && Output->Close();Output.Reset();
        if(!Valid || Aborted()) return Fail(TEXT("ZIP 解压或 CRC 校验失败，请重新下载。"));
        if(FPaths::GetCleanFilename(Name)==TEXT("pdxinfo")) PdxRoots.Add(FPaths::GetPath(Destination));
    }
    if(Position!=Directory.Num() || PdxRoots.Num()!=1) return Fail(TEXT("游戏包必须包含一个完整 PDX，请单独导入所需游戏。"));
    const FString Game=PdxRoots[0];FString Info;
    if(Files.FileSize(*(Game/TEXT("pdxinfo")))>65536 || !FFileHelper::LoadFileToString(Info,*(Game/TEXT("pdxinfo")))) return Fail(TEXT("游戏信息无效。"));
    bool Native=false;const FString Binary=Game/TEXT("pdex.bin");
    if(FPaths::FileExists(Binary)) {
        TUniquePtr<FArchive> Reader(Files.CreateFileReader(*Binary));uint8 H[48]{};
        if(!Reader || Reader->TotalSize()<48 || Reader->TotalSize()>64*1024*1024) return Fail(TEXT("pdex.bin 大小无效。"));
        Reader->Serialize(H,48);const uint32 Code=U32(H+32),Program=U32(H+36),Entry=U32(H+40),Reloc=U32(H+44);
        Native=FMemory::Memcmp(H,"Playdate PDX\0",13)==0 && Code>=4 && Program>=Code && Program<=64*1024*1024 && Entry<Code && Reloc<=Code/4 && uint64(Code)+Reloc*4<=64*1024*1024;
        if(!Native) return Fail(TEXT("pdex.bin 格式无效。"));
    }
    if(!Native && !FPaths::FileExists(Game/TEXT("main.pdz")) && !FPaths::FileExists(Game/TEXT("main.lua")) && !FPaths::FileExists(Game/TEXT("main.luac"))) return Fail(TEXT("未找到 Lua 入口或有效 pdex.bin。"));
    TArray<FString> Lines;Info.ParseIntoArrayLines(Lines);FString Identity=TEXT("game");
    for(const FString& Line:Lines) if(Line.StartsWith(TEXT("bundleID="))) Identity=Line.Mid(9).TrimStartAndEnd();
    for(TCHAR& Character:Identity) if(!FChar::IsAlnum(Character) && Character!=TEXT('.') && Character!=TEXT('_') && Character!=TEXT('-')) Character=TEXT('_');
    Identity=Identity.Left(64);if(Identity.IsEmpty() || Identity.StartsWith(TEXT("."))) Identity=TEXT("game");
    // A content digest gives identical downloads a stable directory without
    // changing the per-bundle save directory. Full archive bytes are streamed.
    FSHA1 Hash;Input->Seek(0);int64 Remaining=Input->TotalSize();
    while(Remaining>0) {if(Aborted()) return Fail(TEXT("已取消下载。"));const int64 N=FMath::Min<int64>(Remaining,sizeof(Buffer));Input->Serialize(Buffer,N);Hash.Update(Buffer,N);Remaining-=N;}
    Hash.Final();uint8 Digest[20];Hash.GetHash(Digest);FString Hex=BytesToHex(Digest,20).ToLower();
    const FString Target=Base/(Identity+TEXT("-")+Hex.Left(12)+TEXT(".pdx"));
    if(!Aborted() && FPaths::FileExists(Target/TEXT("pdxinfo"))) {Files.DeleteDirectory(*Stage,false,true);return Target;}
    if(Aborted() || !FPlatformFileManager::Get().GetPlatformFile().MoveFile(*Target,*Game)) return Fail(TEXT("无法完成游戏安装，请检查存储空间。"));
    Files.DeleteDirectory(*Stage,false,true);return Target;
}
