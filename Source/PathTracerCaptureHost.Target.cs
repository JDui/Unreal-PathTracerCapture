using UnrealBuildTool;
using System.Collections.Generic;

public class PathTracerCaptureHostTarget : TargetRules
{
    public PathTracerCaptureHostTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.AddRange(new List<string> { "PathTracerCaptureHost" });
    }
}
