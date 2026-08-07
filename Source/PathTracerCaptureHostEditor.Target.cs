using UnrealBuildTool;
using System.Collections.Generic;

public class PathTracerCaptureHostEditorTarget : TargetRules
{
    public PathTracerCaptureHostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        bOverrideBuildEnvironment = true;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.AddRange(new List<string> { "PathTracerCaptureHost" });
    }
}
