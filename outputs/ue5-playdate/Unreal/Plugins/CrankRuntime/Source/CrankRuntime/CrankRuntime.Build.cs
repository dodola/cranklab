using UnrealBuildTool;
using System.IO;
public class CrankRuntime : ModuleRules {
    public CrankRuntime(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new [] {"Core", "CoreUObject", "Engine"});
        PrivateDependencyModuleNames.AddRange(new [] {"Projects", "RHI", "RenderCore", "Json", "HTTP"});
        AddEngineThirdPartyPrivateStaticDependencies(Target,"zlib");
        if (Target.Platform == UnrealTargetPlatform.Win64) {
            PublicSystemLibraries.Add("winhttp.lib");
            string NativePath = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Win64");
            foreach (string Dll in new [] {"crank-core.dll", "SDL2.dll", "SDL2_mixer.dll"}) {
                RuntimeDependencies.Add("$(TargetOutputDir)/" + Dll, Path.Combine(NativePath, Dll));
            }

        }
        if (Target.Platform == UnrealTargetPlatform.Android) {
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(ModuleDirectory,"Android","CrankRuntime_APL.xml"));
        }
    }
}
