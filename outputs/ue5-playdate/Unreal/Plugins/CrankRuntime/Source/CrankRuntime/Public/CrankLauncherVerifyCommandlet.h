#pragma once
#include "Commandlets/Commandlet.h"
#include "CrankLauncherVerifyCommandlet.generated.h"
UCLASS()
class UCrankLauncherVerifyCommandlet : public UCommandlet {
    GENERATED_BODY()
public:
    UCrankLauncherVerifyCommandlet();
    virtual int32 Main(const FString& Params) override;
};
