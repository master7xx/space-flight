using UnrealBuildTool;

public class SpaceFlight : ModuleRules
{
    public SpaceFlight(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "AudioMixer", "RenderCore" });
    }
}
