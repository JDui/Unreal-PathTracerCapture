using UnrealBuildTool;

public class PathTracerCaptureEditor : ModuleRules
{
    public PathTracerCaptureEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "ApplicationCore",
                "EditorFramework",
                "EditorSubsystem",
                "ImageWrapper",
                "InputCore",
                "LevelEditor",
                "Projects",
                "PropertyEditor",
                "RenderCore",
                "RHI",
                "Settings",
                "ToolMenus",
                "UnrealEd"
            }
        );
    }
}
