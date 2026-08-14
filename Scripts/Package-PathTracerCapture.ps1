[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [string]$Engine55Root = 'C:\Program Files\Epic Games\UE_5.5',
    [string]$Engine58Root = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$VersionName = '0.3.5',
    [int]$PluginVersion = 4
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot))
{
    $RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
else
{
    $RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
}

$PluginSource = (Resolve-Path -LiteralPath (Join-Path $RepoRoot 'Plugins\PathTracerCapture')).Path
$PluginFile = Join-Path $PluginSource 'PathTracerCapture.uplugin'
$DistRoot = Join-Path $RepoRoot 'dist'
# Keep staging below the workspace Saved directory, while keeping the path
# short enough for UnrealBuildTool's generated C++ paths on Windows.
$StagingRoot = Join-Path $RepoRoot 'Saved\PtcPkg'

function Get-FullPath([string]$Path)
{
    return [IO.Path]::GetFullPath($Path)
}

function Read-Utf8File([string]$Path)
{
    return [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($Path))
}

function Assert-ChildPath([string]$Root, [string]$Target, [string]$Label)
{
    $RootFull = (Get-FullPath $Root).TrimEnd('\') + '\'
    $TargetFull = (Get-FullPath $Target).TrimEnd('\')
    if (-not $TargetFull.StartsWith($RootFull, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "$Label is outside approved root: $TargetFull"
    }
    return $TargetFull
}

function Remove-VerifiedPath([string]$Path, [string]$ApprovedRoot, [string]$Label)
{
    $TargetFull = Assert-ChildPath $ApprovedRoot $Path $Label
    if (-not (Test-Path -LiteralPath $TargetFull))
    {
        return
    }

    $Item = Get-Item -LiteralPath $TargetFull -Force
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "$Label is a link; refusing to remove it: $TargetFull"
    }

    if ($Item.PSIsContainer)
    {
        [IO.Directory]::Delete($TargetFull, $true)
    }
    else
    {
        [IO.File]::Delete($TargetFull)
    }
}

function Get-ModuleBuildId([string]$ModulesPath)
{
    if (-not (Test-Path -LiteralPath $ModulesPath -PathType Leaf))
    {
        throw "UnrealEditor.modules not found: $ModulesPath"
    }

    $ModuleJson = Read-Utf8File $ModulesPath | ConvertFrom-Json
    $BuildId = [string]$ModuleJson.BuildId
    if ([string]::IsNullOrWhiteSpace($BuildId))
    {
        throw "BuildId missing from UnrealEditor.modules: $ModulesPath"
    }
    return $BuildId
}

function Get-EngineVersion([string]$UpluginPath)
{
    $Json = Read-Utf8File $UpluginPath | ConvertFrom-Json
    return [string]$Json.EngineVersion
}

function Assert-CleanPackage([string]$PackagePath, [string]$Label)
{
    $Forbidden = @(Get-ChildItem -LiteralPath $PackagePath -Recurse -File -Force | Where-Object {
        ($_.FullName -match '\\Intermediate(\\|$)') -or ($_.Extension -in @('.pdb', '.obj', '.lib'))
    })
    if ($Forbidden.Count -gt 0)
    {
        $Names = ($Forbidden | ForEach-Object { $_.FullName }) -join ', '
        throw "$Label contains forbidden release files: $Names"
    }

    $Required = @(
        (Join-Path $PackagePath 'PathTracerCapture.uplugin'),
        (Join-Path $PackagePath 'Binaries\Win64\UnrealEditor-PathTracerCaptureEditor.dll'),
        (Join-Path $PackagePath 'Binaries\Win64\UnrealEditor.modules'),
        (Join-Path $PackagePath 'Config'),
        (Join-Path $PackagePath 'Content'),
        (Join-Path $PackagePath 'Resources')
    )
    foreach ($RequiredPath in $Required)
    {
        if (-not (Test-Path -LiteralPath $RequiredPath))
        {
            throw "$Label is missing required release content: $RequiredPath"
        }
    }
}

function Assert-CleanZip([string]$ZipPath, [string]$Label)
{
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $Archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try
    {
        $Entries = @($Archive.Entries)
        if ($Entries.Count -eq 0)
        {
            throw "$Label ZIP is empty"
        }

        $Forbidden = @($Entries | Where-Object {
            ($_.FullName -match '(^|[\\/])Intermediate([\\/]|$)') -or ($_.FullName -match '\.(pdb|obj|lib)$')
        })
        if ($Forbidden.Count -gt 0)
        {
            $Names = ($Forbidden | ForEach-Object { $_.FullName }) -join ', '
            throw "$Label ZIP contains forbidden release entries: $Names"
        }

        $UnexpectedRoot = @($Entries | Where-Object {
            ($_.FullName -notmatch '^(Binaries|Config|Content|Resources)([\\/]|$)') -and ($_.FullName -ne 'PathTracerCapture.uplugin')
        })
        if ($UnexpectedRoot.Count -gt 0)
        {
            $Names = ($UnexpectedRoot | ForEach-Object { $_.FullName }) -join ', '
            throw "$Label ZIP has unexpected root entries: $Names"
        }

        return $Entries.Count
    }
    finally
    {
        $Archive.Dispose()
    }
}

function Copy-ReleaseContent([string]$StagingPath, [string]$ReleasePath)
{
    New-Item -ItemType Directory -Path $ReleasePath -Force | Out-Null

    Copy-Item -LiteralPath (Join-Path $StagingPath 'PathTracerCapture.uplugin') -Destination $ReleasePath -Force
    foreach ($DirectoryName in @('Config', 'Content', 'Resources'))
    {
        $SourceDirectory = Join-Path $StagingPath $DirectoryName
        if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container))
        {
            throw "Staging package is missing ${DirectoryName}: $SourceDirectory"
        }
        Copy-Item -LiteralPath $SourceDirectory -Destination $ReleasePath -Recurse -Force
    }

    $StagingBinaries = Join-Path $StagingPath 'Binaries\Win64'
    $ReleaseBinaries = Join-Path $ReleasePath 'Binaries\Win64'
    if (-not (Test-Path -LiteralPath $StagingBinaries -PathType Container))
    {
        throw "Staging package is missing Binaries\Win64: $StagingBinaries"
    }
    New-Item -ItemType Directory -Path $ReleaseBinaries -Force | Out-Null
    $BinaryFiles = @(Get-ChildItem -LiteralPath $StagingBinaries -File | Where-Object {
        $_.Extension -in @('.dll', '.modules')
    })
    foreach ($BinaryFile in $BinaryFiles)
    {
        Copy-Item -LiteralPath $BinaryFile.FullName -Destination $ReleaseBinaries -Force
    }
}

function Invoke-PluginPackageCore(
    [string]$Label,
    [string]$EngineRoot,
    [string]$EnginePrefix)
{
    $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
    $RunUat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    $EngineModules = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.modules'
    if (-not (Test-Path -LiteralPath $RunUat -PathType Leaf))
    {
        throw "$Label RunUAT.bat not found: $RunUat"
    }

    $StagingPath = Join-Path $StagingRoot "$Label-v$VersionName"
    $ReleasePath = Join-Path $DistRoot "PathTracerCapture-$Label-v$VersionName"
    $ZipPath = "$ReleasePath.zip"

    # Only these exact, resolved paths are ever cleaned by this script.
    Remove-VerifiedPath $StagingPath $StagingRoot "$Label staging"
    Remove-VerifiedPath $ReleasePath $DistRoot "$Label release directory"
    Remove-VerifiedPath $ZipPath $DistRoot "$Label release ZIP"
    New-Item -ItemType Directory -Path $StagingPath -Force | Out-Null

    Write-Host "[$Label] BuildPlugin staging: $StagingPath"
    & $RunUat BuildPlugin "-Plugin=$PluginFile" "-Package=$StagingPath" '-TargetPlatforms=Win64' '-Rocket'
    if ($LASTEXITCODE -ne 0)
    {
        throw "$Label RunUAT BuildPlugin failed with exit code $LASTEXITCODE"
    }

    # Some engine versions omit Config from BuildPlugin output; source Config is
    # copied into staging so final packaging still contains plugin defaults.
    $StagingConfig = Join-Path $StagingPath 'Config'
    if (-not (Test-Path -LiteralPath $StagingConfig -PathType Container))
    {
        Copy-Item -LiteralPath (Join-Path $PluginSource 'Config') -Destination $StagingPath -Recurse -Force
    }

    $EngineBuildId = Get-ModuleBuildId $EngineModules
    $StagingModules = Join-Path $StagingPath 'Binaries\Win64\UnrealEditor.modules'
    $StagingBuildId = Get-ModuleBuildId $StagingModules
    if ($EngineBuildId -ne $StagingBuildId)
    {
        Remove-VerifiedPath $ReleasePath $DistRoot "$Label invalid release directory"
        Remove-VerifiedPath $ZipPath $DistRoot "$Label invalid release ZIP"
        throw "$Label BuildId mismatch: engine=$EngineBuildId plugin=$StagingBuildId"
    }

    $StagingUplugin = Join-Path $StagingPath 'PathTracerCapture.uplugin'
    $StagingUpluginJson = Read-Utf8File $StagingUplugin | ConvertFrom-Json
    if ([int]$StagingUpluginJson.Version -ne $PluginVersion -or [string]$StagingUpluginJson.VersionName -ne $VersionName)
    {
        throw "$Label staging uplugin version mismatch: Version=$($StagingUpluginJson.Version), VersionName=$($StagingUpluginJson.VersionName)"
    }
    $StagingEngineVersion = [string]$StagingUpluginJson.EngineVersion
    if (-not $StagingEngineVersion.StartsWith($EnginePrefix, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "$Label staging EngineVersion mismatch: $StagingEngineVersion (expected $EnginePrefix.x)"
    }

    Copy-ReleaseContent $StagingPath $ReleasePath
    Assert-CleanPackage $ReleasePath $Label

    $ReleaseBuildId = Get-ModuleBuildId (Join-Path $ReleasePath 'Binaries\Win64\UnrealEditor.modules')
    if ($ReleaseBuildId -ne $EngineBuildId)
    {
        Remove-VerifiedPath $ReleasePath $DistRoot "$Label invalid release directory"
        throw "$Label release BuildId mismatch: engine=$EngineBuildId release=$ReleaseBuildId"
    }

    Compress-Archive -Path (Join-Path $ReleasePath '*') -DestinationPath $ZipPath -CompressionLevel Optimal -Force
    $EntryCount = Assert-CleanZip $ZipPath $Label
    $ZipInfo = Get-Item -LiteralPath $ZipPath
    $ZipHash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash
    Write-Host "[$Label] Version=$VersionName EngineVersion=$StagingEngineVersion BuildId=$EngineBuildId"
    Write-Host "[$Label] Directory=$ReleasePath"
    Write-Host "[$Label] ZIP=$ZipPath Entries=$EntryCount Size=$($ZipInfo.Length) SHA256=$ZipHash"

    return [pscustomobject]@{
        Label = $Label
        VersionName = $VersionName
        EngineVersion = $StagingEngineVersion
        BuildId = $EngineBuildId
        Directory = $ReleasePath
        Zip = $ZipPath
        ZipEntries = $EntryCount
        ZipSize = $ZipInfo.Length
        SHA256 = $ZipHash
    }
}

function Invoke-PluginPackage(
    [string]$Label,
    [string]$EngineRoot,
    [string]$EnginePrefix)
{
    $ReleasePath = Join-Path $DistRoot "PathTracerCapture-$Label-v$VersionName"
    $ZipPath = "$ReleasePath.zip"
    try
    {
        return Invoke-PluginPackageCore $Label $EngineRoot $EnginePrefix
    }
    catch
    {
        Remove-VerifiedPath $ReleasePath $DistRoot "$Label failed release directory"
        Remove-VerifiedPath $ZipPath $DistRoot "$Label failed release ZIP"
        throw
    }
}

if (-not (Test-Path -LiteralPath $PluginFile -PathType Leaf))
{
    throw "Plugin descriptor not found: $PluginFile"
}
if (-not (Test-Path -LiteralPath $DistRoot -PathType Container))
{
    New-Item -ItemType Directory -Path $DistRoot -Force | Out-Null
}
New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null

$SourceUpluginJson = Read-Utf8File $PluginFile | ConvertFrom-Json
if ([int]$SourceUpluginJson.Version -ne $PluginVersion -or [string]$SourceUpluginJson.VersionName -ne $VersionName)
{
    throw "Source uplugin must be Version=$PluginVersion and VersionName=$VersionName"
}

$Results = @()
try
{
    $Results += Invoke-PluginPackage 'UE5.5' $Engine55Root '5.5.'
    $Results += Invoke-PluginPackage 'UE5.8' $Engine58Root '5.8.'
}
catch
{
    Write-Error $_
    exit 1
}

Write-Host 'PathTracerCapture packaging completed successfully.'
$Results | Format-Table Label,EngineVersion,BuildId,ZipEntries,ZipSize,SHA256 -AutoSize
