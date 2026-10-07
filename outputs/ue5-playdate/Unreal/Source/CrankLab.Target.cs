using UnrealBuildTool;
public class CrankLabTarget : TargetRules {
    public CrankLabTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("CrankLab");
    }
}
