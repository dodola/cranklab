#include "CrankTranslation.h"
#include "CrankGameFiles.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
TSharedPtr<FJsonObject> Parse(const FString& Text) {TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
FString Field(const TSharedPtr<FJsonObject>& O,const TCHAR* Name) {FString V;if(O)O->TryGetStringField(Name,V);return V;}
FString Encode(const TSharedPtr<FJsonObject>& O) {FString T;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&T));return T;}
bool ValidProvider(const FString& P) {return P==TEXT("auto") || P==TEXT("microsoft") || P==TEXT("tencent");}
}
FCrankTranslation::FCrankTranslation() {
    Storage=FCrankGameFiles::Root()/TEXT("Translation");IFileManager::Get().MakeDirectory(*Storage,true);
    FString Settings;
    if(FFileHelper::LoadFileToString(Settings,*(Storage/TEXT("provider.json")))) {
        const FString Saved=Field(Parse(Settings),TEXT("provider"));if(ValidProvider(Saved))Provider=Saved;
    }
#if !UE_BUILD_SHIPPING
    FString Override;
    if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationProvider="),Override) && ValidProvider(Override))Provider=Override;
#endif
    // Remove only this feature's obsolete, regenerable app-private models.
    // User-selected files, descriptions, game saves and translation caches stay.
    const FString Models=Storage/TEXT("Models");
    for(const TCHAR* Name:{TEXT("Hy-MT2-1.8B-1.25Bit.gguf"),TEXT("Hy-MT2-1.8B-Q4_K_M.gguf")}) {
        IFileManager::Get().Delete(*(Models/Name),false,true);
        TArray<FString> Partials;IFileManager::Get().FindFiles(Partials,*(Models/(FString(Name)+TEXT(".part-*"))),true,false);
        for(const FString& Partial:Partials)IFileManager::Get().Delete(*(Models/Partial),false,true);
    }
    IFileManager::Get().Delete(*(Storage/TEXT("model.json")),false,true);
}
FCrankTranslation::~FCrankTranslation(){Shutdown();}
FString FCrankTranslation::Normalize(const FString& Text) {
    FString Clean=Text;Clean.ReplaceInline(TEXT("\r\n"),TEXT("\n"));Clean.ReplaceInline(TEXT("\r"),TEXT("\n"));
    Clean.ReplaceInline(TEXT("&nbsp;"),TEXT(" "));Clean.ReplaceInline(TEXT("&amp;"),TEXT("&"));return Clean.TrimStartAndEnd();
}
FString FCrankTranslation::CacheKey(int32 CatalogId,const FString& Text,const FString& P) {
    FTCHARToUTF8 Bytes(*(FString::FromInt(CatalogId)+TEXT("|online-")+P+TEXT("|zh-CN|terms-2|")+Normalize(Text)));
    FMD5 Hash;Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());uint8 Digest[16];Hash.Final(Digest);return BytesToHex(Digest,16).ToLower();
}
bool FCrankTranslation::SaveJson(const FString& Path,const FString& Text) {
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);const FString Temp=Path+TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && IFileManager::Get().Move(*Path,*Temp,true,true);
}
bool FCrankTranslation::SelectProvider(const FString& P) {
    if(bClosed || !ValidProvider(P))return false;if(P==Provider)return true;
    Cancel();Provider=P;Translated.Empty();State=Source.IsEmpty()?TEXT("idle"):TEXT("ready");Status.Empty();ReadTranslationCache();
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("provider"),Provider);
    if(!SaveJson(Storage/TEXT("provider.json"),Encode(O))){Status=TEXT("翻译服务已切换，但设置未能保存。");return false;}return true;
}
FString FCrankTranslation::ProviderMessage() const {
    return Provider==TEXT("auto")?TEXT("优先使用微软；不可用时尝试腾讯。首次翻译需联网，已缓存译文可离线阅读。")
        :Provider==TEXT("microsoft")?TEXT("使用微软在线翻译。无需下载模型，已缓存译文可离线阅读。")
        :TEXT("使用腾讯在线翻译。无需下载模型，已缓存译文可离线阅读。");
}
void FCrankTranslation::Open(int32 CatalogId) {
    if(bClosed || CatalogId<=0)return;
    Cancel();bTranslationRequested=true;Id=CatalogId;Title=TEXT("游戏介绍");Source.Empty();Translated.Empty();State=TEXT("loading");Status=TEXT("正在读取游戏介绍…");
    const uint64 Version=Revision;FString Cached;
    if(FFileHelper::LoadFileToString(Cached,*(Storage/TEXT("Descriptions")/(FString::FromInt(Id)+TEXT(".json"))))) {
        auto O=Parse(Cached);Title=Field(O,TEXT("title"));Source=Normalize(Field(O,TEXT("description")));
        if(Source.Len()>24000){State=TEXT("error");Status=TEXT("介绍过长，暂时无法翻译。原文仍可阅读。");}
        else if(!Source.IsEmpty() && !ReadTranslationCache()){State=TEXT("ready");Translate();}
    }
    FString Url=FString::Printf(TEXT("https://pdgamez.com/game/%d.json"),Id);
#if !UE_BUILD_SHIPPING
    FString Base;if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationDetailBase="),Base))Url=Base/FString::Printf(TEXT("game/%d.json"),Id);
#endif
    auto R=FHttpModule::Get().CreateRequest();DetailRequest=R;R->SetURL(Url);R->SetVerb(TEXT("GET"));R->SetTimeout(8);
    R->OnRequestProgress64().BindLambda([](FHttpRequestPtr Q,uint64,uint64 N){if(N>1024*1024)Q->CancelRequest();});
    R->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version](FHttpRequestPtr,FHttpResponsePtr Response,bool Ok) {
        auto S=Weak.Pin();if(!S || S->bClosed || Version!=S->Revision)return;S->DetailRequest.Reset();
        if(Ok && Response && Response->GetResponseCode()==200 && Response->GetContent().Num()<=1024*1024){S->AcceptDescription(Response->GetContentAsString());return;}
        if(!S->Source.IsEmpty()) {
            if(S->State==TEXT("translated"))S->Status=TEXT("网络不可用，显示已保存译文。");
            else if(S->State!=TEXT("translating") && S->State!=TEXT("error")){S->State=TEXT("ready");if(S->bTranslationRequested)S->Translate();}
        }else{S->State=TEXT("error");S->Status=TEXT("读取介绍失败，请检查网络后重试。");}
    });
    if(!R->ProcessRequest()) {DetailRequest.Reset();if(Source.IsEmpty()){State=TEXT("error");Status=TEXT("无法读取介绍，请重试。");}}
}
void FCrankTranslation::AcceptDescription(const FString& Text) {
    auto O=Parse(Text);double CatalogId=0;
    if(!O || !O->TryGetNumberField(TEXT("id"),CatalogId) || CatalogId!=Id){if(Source.IsEmpty()){State=TEXT("error");Status=TEXT("游戏介绍数据无效，请重试。");}return;}
    Title=Field(O,TEXT("title"));const FString Next=Normalize(Field(O,TEXT("description")));
    if(Next==Source && (State==TEXT("translating") || State==TEXT("translated") || State==TEXT("error")))return;
    ++TranslationRevision;if(TranslationRequest){TranslationRequest->CancelRequest();TranslationRequest.Reset();}
    Source=Next;Translated.Empty();
    if(Source.Len()>24000){State=TEXT("error");Status=TEXT("介绍过长，暂时无法翻译。原文仍可阅读。");return;}
    auto Public=MakeShared<FJsonObject>();Public->SetStringField(TEXT("title"),Title);Public->SetStringField(TEXT("description"),Source);
    SaveJson(Storage/TEXT("Descriptions")/(FString::FromInt(Id)+TEXT(".json")),Encode(Public));
    if(Source.IsEmpty()){State=TEXT("empty");Status=TEXT("这个游戏暂时没有介绍。");return;}
    State=TEXT("ready");Status.Empty();if(!ReadTranslationCache() && bTranslationRequested)Translate();
}
bool FCrankTranslation::ReadTranslationCache() {
    if(Source.IsEmpty())return false;
    const TArray<FString> Providers=Provider==TEXT("auto")?TArray<FString>{TEXT("microsoft"),TEXT("tencent")}:TArray<FString>{Provider};
    for(const FString& P:Providers) {
        FString Text;const FString Key=CacheKey(Id,Source,P);
        if(FFileHelper::LoadFileToString(Text,*(Storage/TEXT("Cache")/(Key+TEXT(".json"))))) {
            auto O=Parse(Text);if(Field(O,TEXT("key"))==Key && !Field(O,TEXT("translation")).IsEmpty()) {
                Translated=Field(O,TEXT("translation"));State=TEXT("translated");Status=TEXT("已读取本地译文");return true;
            }
        }
    }return false;
}
FCrankTranslation::FPart FCrankTranslation::Protect(const FString& Text) {
    FPart P;
    const FRegexPattern Pattern(TEXT("(?i)https?://[^\\s<>()]+|\\bplay\\.date/[a-zA-Z0-9/._-]+|\\b[\\w.-]+\\.pdx(?:\\.zip)?\\b|\\bundock the crank\\b|\\bdock the crank\\b|\\bPlaydate\\b|\\bD-pad\\b|[ⒶⒷ✛▲▼◀▶]"));
    FRegexMatcher M(Pattern,Text);int32 Last=0;
    FString Prefix=TEXT("ZXQK");while(Text.Contains(Prefix))Prefix+=TEXT("X");
    while(M.FindNext()) {
        P.Text+=Text.Mid(Last,M.GetMatchBeginning()-Last);FString Value=M.GetCaptureGroup(0);
        if(Value.Equals(TEXT("D-pad"),ESearchCase::IgnoreCase))Value=TEXT("方向键");
        else if(Value.Equals(TEXT("Playdate"),ESearchCase::IgnoreCase))Value=TEXT("Playdate");
        else if(Value.Equals(TEXT("undock the crank"),ESearchCase::IgnoreCase))Value=TEXT("展开摇柄");
        else if(Value.Equals(TEXT("dock the crank"),ESearchCase::IgnoreCase))Value=TEXT("收起摇柄");
        const FString Marker=FString::Printf(TEXT("%s%03dQXZ"),*Prefix,P.Keep.Num());P.Keep.Emplace(Marker,Value);P.Text+=Marker;Last=M.GetMatchEnding();
    }
    P.Text+=Text.Mid(Last);return P;
}
bool FCrankTranslation::Restore(const FPart& P,FString& Text) {
    for(const auto& Keep:P.Keep) {
        int32 Digit=0;while(Digit<Keep.Key.Len() && !FChar::IsDigit(Keep.Key[Digit]))++Digit;
        const FString Pattern=TEXT("(?i)")+Keep.Key.Left(Digit)+TEXT("\\s*")+Keep.Key.Mid(Digit,Keep.Key.Len()-Digit-3)+TEXT("\\s*QXZ");
        FRegexMatcher M(FRegexPattern(Pattern),Text);
        if(!M.FindNext())return false;const int32 Start=M.GetMatchBeginning(),End=M.GetMatchEnding();
        if(M.FindNext())return false;
        Text=Text.Left(Start)+Keep.Value+Text.Mid(End);
    }
    Text.ReplaceInline(TEXT("曲柄"),TEXT("摇柄"));return true;
}
void FCrankTranslation::Translate() {
    if(bClosed || TranslationRequest || Source.IsEmpty() || Source.Len()>24000)return;
    bTranslationRequested=true;if(ReadTranslationCache())return;
    Parts.Reset();int32 Start=0;
    while(Start<Source.Len()) {
        int32 End=FMath::Min(Start+3500,Source.Len());
        if(End<Source.Len()) {
            const int32 Break=Source.Find(TEXT("\n\n"),ESearchCase::CaseSensitive,ESearchDir::FromEnd,End);
            if(Break>Start+500)End=Break+2;
            else {const int32 Space=Source.Find(TEXT(" "),ESearchCase::CaseSensitive,ESearchDir::FromEnd,End);if(Space>Start+500)End=Space+1;}
            if(End>Start && uint32(Source[End-1])>=0xd800 && uint32(Source[End-1])<=0xdbff)--End;
        }
        FPart Part=Protect(Source.Mid(Start,End-Start));
        // Placeholders can expand glyph-heavy descriptions; bound the actual
        // payload as well as the source. Keep UTF-16 pairs together.
        while(Part.Text.Len()>3500 && End>Start+1) {
            End=Start+FMath::Max(1,(End-Start)*3500/Part.Text.Len());
            if(uint32(Source[End-1])>=0xd800 && uint32(Source[End-1])<=0xdbff) {if(End>Start+1)--End;else ++End;}
            Part=Protect(Source.Mid(Start,End-Start));
        }
        int32 Tail=End;while(Tail>Start && FChar::IsWhitespace(Source[Tail-1]))--Tail;
        Part.JoinAfter=Source.Mid(Tail,End-Tail);Parts.Add(MoveTemp(Part));Start=End;
    }
    Attempt=0;Translated.Empty();State=TEXT("translating");Status=TEXT("正在联网翻译…");StartTranslationRequest(++TranslationRevision);
}
void FCrankTranslation::StartTranslationRequest(uint64 Version) {
    if(bClosed || Version!=TranslationRevision)return;
    ActiveProvider=Provider==TEXT("auto")?(Attempt==0?TEXT("microsoft"):TEXT("tencent")):Provider;
    FString Body,Url;TArray<TSharedPtr<FJsonValue>> Texts;
    for(const auto& P:Parts)Texts.Add(MakeShared<FJsonValueString>(P.Text));
    if(ActiveProvider==TEXT("microsoft")) {
        FJsonSerializer::Serialize(Texts,TJsonWriterFactory<>::Create(&Body));
        Url=TEXT("https://edge.microsoft.com/translate/translatetext?from=en&to=zh-Hans&isEnterpriseClient=false");
    }else {
        Url=TEXT("https://transmart.qq.com/api/imt");auto O=MakeShared<FJsonObject>();auto Header=MakeShared<FJsonObject>();
        Header->SetStringField(TEXT("fn"),TEXT("auto_translation"));Header->SetStringField(TEXT("client_key"),TEXT("browser-chrome-110.0.0-Android-")+FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens)+TEXT("-")+LexToString(FDateTime::UtcNow().ToUnixTimestamp()*1000));
        O->SetObjectField(TEXT("header"),Header);O->SetStringField(TEXT("type"),TEXT("plain"));O->SetStringField(TEXT("model_category"),TEXT("normal"));
        auto Input=MakeShared<FJsonObject>();Input->SetArrayField(TEXT("text_list"),Texts);Input->SetStringField(TEXT("lang"),TEXT("en"));O->SetObjectField(TEXT("source"),Input);
        auto Target=MakeShared<FJsonObject>();Target->SetStringField(TEXT("lang"),TEXT("zh"));O->SetObjectField(TEXT("target"),Target);Body=Encode(O);
    }
#if !UE_BUILD_SHIPPING
    FString Base;if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationApiBase="),Base))Url=Base/ActiveProvider;
#endif
    auto R=FHttpModule::Get().CreateRequest();TranslationRequest=R;R->SetURL(Url);R->SetVerb(TEXT("POST"));R->SetTimeout(8);R->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));
    R->SetHeader(TEXT("User-Agent"),TEXT("Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/110.0.0.0 Mobile Safari/537.36"));
    if(ActiveProvider==TEXT("tencent"))R->SetHeader(TEXT("Referer"),TEXT("https://transmart.qq.com/zh-CN/index"));R->SetContentAsString(Body);RequestStarted=FPlatformTime::Seconds();
    R->OnRequestProgress64().BindLambda([](FHttpRequestPtr Q,uint64,uint64 N){if(N>1024*1024)Q->CancelRequest();});
    R->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version,P=ActiveProvider](FHttpRequestPtr,FHttpResponsePtr Response,bool Ok) {
        auto S=Weak.Pin();if(!S || S->bClosed || Version!=S->TranslationRevision)return;S->TranslationRequest.Reset();
        TArray<FString> Output;
        if(Ok && Response && Response->GetResponseCode()==200 && Response->GetContent().Num()<=1024*1024) {
            const FString Raw=Response->GetContentAsString();const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
            if(P==TEXT("microsoft")) {
                TArray<TSharedPtr<FJsonValue>> Array;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw),Array) && Array.Num()==S->Parts.Num())for(const auto& Value:Array) {
                    const auto O=Value->Type==EJson::Object?Value->AsObject():nullptr;
                    if(O && O->TryGetArrayField(TEXT("translations"),Items) && Items->Num()==1) {
                        const auto T=(*Items)[0]->Type==EJson::Object?(*Items)[0]->AsObject():nullptr;
                        if(Field(T,TEXT("to"))==TEXT("zh-Hans"))Output.Add(Field(T,TEXT("text")));
                    }
                }
            }else {
                const auto O=Parse(Raw);const TSharedPtr<FJsonObject>* Header=nullptr;
                if(O && O->TryGetObjectField(TEXT("header"),Header) && Field(*Header,TEXT("ret_code"))==TEXT("succ") && O->TryGetArrayField(TEXT("auto_translation"),Items) && Items->Num()==S->Parts.Num())
                    for(const auto& Item:*Items)if(Item->Type==EJson::String)Output.Add(Item->AsString());
            }
        }
        bool Valid=Output.Num()==S->Parts.Num();FString Completed;
        if(Valid)for(int32 I=0;I<Output.Num();++I) {
            Valid&=!Output[I].TrimStartAndEnd().IsEmpty() && Restore(S->Parts[I],Output[I]);Completed+=Output[I].TrimStartAndEnd()+S->Parts[I].JoinAfter;
        }
        bool Chinese=false;for(TCHAR C:Completed)Chinese|=C>=0x4e00 && C<=0x9fff;Valid&=Chinese;
        if(!Valid){S->TranslationFailed(Version);return;}
        S->Translated=Completed.TrimStartAndEnd();S->State=TEXT("translated");const FString Key=CacheKey(S->Id,S->Source,P);auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("key"),Key);O->SetStringField(TEXT("translation"),S->Translated);O->SetStringField(TEXT("provider"),P);
        S->Status=S->SaveJson(S->Storage/TEXT("Cache")/(Key+TEXT(".json")),Encode(O))?TEXT("在线翻译完成 · 已缓存"):TEXT("翻译已完成，但缓存未能保存。");
        UE_LOG(LogTemp,Display,TEXT("Crank online translation · provider=%s · elapsedMs=%.1f · parts=%d"),*P,(FPlatformTime::Seconds()-S->RequestStarted)*1000,S->Parts.Num());
    });
    if(!R->ProcessRequest()){TranslationRequest.Reset();TranslationFailed(Version);}
}
void FCrankTranslation::TranslationFailed(uint64 Version) {
    if(bClosed || Version!=TranslationRevision)return;
    if(Provider==TEXT("auto") && Attempt++==0){Status=TEXT("正在尝试备用翻译服务…");StartTranslationRequest(Version);return;}
    State=TEXT("error");Translated.Empty();Status=TEXT("在线翻译暂时不可用，请检查网络后重试；原文仍可阅读。");
}
void FCrankTranslation::Cancel() {
    ++Revision;++TranslationRevision;bTranslationRequested=false;
    if(DetailRequest){DetailRequest->CancelRequest();DetailRequest.Reset();}
    if(TranslationRequest){TranslationRequest->CancelRequest();TranslationRequest.Reset();}
    if(State==TEXT("loading") || State==TEXT("translating")){State=Source.IsEmpty()?TEXT("idle"):TEXT("ready");Translated.Empty();Status=TEXT("已取消翻译，原文仍可阅读。");}
}
void FCrankTranslation::Shutdown(){if(bClosed)return;bClosed=true;Cancel();}
