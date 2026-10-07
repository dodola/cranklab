using UnrealBuildTool;
public class CrankLab : ModuleRules {
    public CrankLab(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new [] {"Core", "CoreUObject", "Engine", "CrankRuntime", "ProceduralMeshComponent", "InputCore"});
        PrivateDependencyModuleNames.AddRange(new [] {"Json", "Slate", "SlateCore", "UMG", "ApplicationCore", "RenderCore"});
    }
}
