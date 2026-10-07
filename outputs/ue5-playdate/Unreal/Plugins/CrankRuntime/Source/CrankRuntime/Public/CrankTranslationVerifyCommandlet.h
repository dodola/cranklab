#pragma once
#include "Commandlets/Commandlet.h"
#include "CrankTranslationVerifyCommandlet.generated.h"
UCLASS()
class UCrankTranslationVerifyCommandlet : public UCommandlet {
    GENERATED_BODY()
public:
    UCrankTranslationVerifyCommandlet();
    int32 Main(const FString& Params) override;
};
