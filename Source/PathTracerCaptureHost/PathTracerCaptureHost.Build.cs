using UnrealBuildTool;

public class PathTracerCaptureHost : ModuleRules
{
    public PathTracerCaptureHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore"
            }
        );
    }
}
