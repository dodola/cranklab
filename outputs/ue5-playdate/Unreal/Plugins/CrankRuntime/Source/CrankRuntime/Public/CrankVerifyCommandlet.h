#pragma once
#include "Commandlets/Commandlet.h"
#include "CrankVerifyCommandlet.generated.h"
UCLASS()
class UCrankVerifyCommandlet : public UCommandlet {
    GENERATED_BODY()
public:
    UCrankVerifyCommandlet();
    virtual int32 Main(const FString& Params) override;
};
