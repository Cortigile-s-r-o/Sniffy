[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$RepoUrl = "https://github.com/stlink-org/stlink.git",
    [string]$SourceRepoPath,
    [string]$Ref,
    [string]$DestinationRoot = (Join-Path $PSScriptRoot "..\..\libs\stlink"),
    [string]$WorkRoot = (Join-Path ([System.IO.Path]::GetTempPath()) "sniffy-stlink-sync"),
    [string]$ArtifactRoot,
    [switch]$SkipHeaders,
    [switch]$SkipChips,
    [switch]$SkipBinaries,
    [switch]$KeepWorktree
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)

    Write-Host "[sync-stlink] $Message"
}

function Resolve-AbsolutePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [System.IO.Path]::GetFullPath($Path)
}

function Ensure-Directory {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [switch]$BypassWhatIf
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -WhatIf:$(-not $BypassWhatIf) | Out-Null
    }
}

function Sync-DirectoryContents {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Source directory does not exist: $Source"
    }

    Ensure-Directory -Path $Destination

    Get-ChildItem -LiteralPath $Destination -Force | Remove-Item -Recurse -Force
    Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

function Find-GitCommand {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) {
        throw "git was not found in PATH. Provide -SourceRepoPath to an existing upstream checkout or install git."
    }

    return $git.Source
}

function Get-UpstreamVersionInfo {
    param([Parameter(Mandatory = $true)][string]$RepoPath)

    $versionText = $null
    $treeRef = $null

    $gitDir = Join-Path $RepoPath ".git"
    if (Test-Path -LiteralPath $gitDir) {
        $gitExe = Find-GitCommand
        $describeOutput = & $gitExe -C $RepoPath describe --always --tags 2>$null
        if ($LASTEXITCODE -eq 0 -and $describeOutput) {
            $versionText = $describeOutput.Trim()
        }

        $headOutput = & $gitExe -C $RepoPath rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $headOutput) {
            $treeRef = $headOutput.Trim()
        }
    }

    if (-not $versionText) {
        $versionFile = Join-Path $RepoPath ".version"
        if (-not (Test-Path -LiteralPath $versionFile)) {
            throw "Unable to determine stlink version. Neither git metadata nor .version were available in $RepoPath"
        }

        $versionText = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    }

    $normalized = $versionText.TrimStart('v')
    if ($normalized -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)') {
        throw "Unable to parse semantic version prefix from '$versionText'"
    }

    return [pscustomobject]@{
        DisplayVersion = $normalized
        Major = [int]$Matches.major
        Minor = [int]$Matches.minor
        Patch = [int]$Matches.patch
        TreeRef = $treeRef
    }
}

function New-VersionHeaderContent {
    param([Parameter(Mandatory = $true)]$VersionInfo)

    return @"
#ifndef VERSION_H
#define VERSION_H

#define STLINK_VERSION       "$($VersionInfo.DisplayVersion)"
#define STLINK_VERSION_MAJOR $($VersionInfo.Major)
#define STLINK_VERSION_MINOR $($VersionInfo.Minor)
#define STLINK_VERSION_PATCH $($VersionInfo.Patch)

#endif // VERSION_H
"@
}

function Copy-HeaderIfNew {
    param(
        [Parameter(Mandatory = $true)][string]$SourceFile,
        [Parameter(Mandatory = $true)][string]$DestinationFile
    )

    if (Test-Path -LiteralPath $DestinationFile) {
        $sourceHash = (Get-FileHash -LiteralPath $SourceFile -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $DestinationFile -Algorithm SHA256).Hash
        if ($sourceHash -ne $destinationHash) {
            Write-Step "Overriding duplicate header with later upstream copy: $([System.IO.Path]::GetFileName($SourceFile))"
        }
    }

    Copy-Item -LiteralPath $SourceFile -Destination $DestinationFile -Force -WhatIf:$false
}

function Sync-Headers {
    param(
        [Parameter(Mandatory = $true)][string]$RepoPath,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [Parameter(Mandatory = $true)]$VersionInfo,
        [Parameter(Mandatory = $true)][string]$StageRoot
    )

    $stageInclude = Join-Path $StageRoot "include"
    if (Test-Path -LiteralPath $stageInclude) {
        Remove-Item -LiteralPath $stageInclude -Recurse -Force -WhatIf:$false
    }
    New-Item -ItemType Directory -Path $stageInclude -WhatIf:$false | Out-Null

    $headerDirectories = @(
        (Join-Path $RepoPath "inc"),
        (Join-Path $RepoPath "src\stlink-lib")
    )

    foreach ($headerDirectory in $headerDirectories) {
        if (-not (Test-Path -LiteralPath $headerDirectory)) {
            throw "Missing upstream header directory: $headerDirectory"
        }

        Get-ChildItem -LiteralPath $headerDirectory -Filter "*.h" -File | ForEach-Object {
            $destinationFile = Join-Path $stageInclude $_.Name
            Copy-HeaderIfNew -SourceFile $_.FullName -DestinationFile $destinationFile
        }
    }

    Set-Content -LiteralPath (Join-Path $stageInclude "version.h") -Value (New-VersionHeaderContent -VersionInfo $VersionInfo) -Encoding Ascii -WhatIf:$false
    Sync-DirectoryContents -Source $stageInclude -Destination $DestinationPath
}

function Find-ArtifactFile {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$PreferredRelativePaths,
        [Parameter(Mandatory = $true)][string]$FileName
    )

    foreach ($relativePath in $PreferredRelativePaths) {
        $candidate = Join-Path $Root $relativePath
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-AbsolutePath -Path $candidate)
        }
    }

    $found = Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object { $_.Name -eq $FileName } | Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    return $null
}

function Copy-BuildArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$ArtifactRootPath,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    if (-not (Test-Path -LiteralPath $ArtifactRootPath)) {
        throw "Artifact root does not exist: $ArtifactRootPath"
    }

    $binDestination = Join-Path $DestinationPath "bin"
    $libDestination = Join-Path $DestinationPath "lib"
    Ensure-Directory -Path $binDestination
    Ensure-Directory -Path $libDestination

    $dllSource = Find-ArtifactFile -Root $ArtifactRootPath -PreferredRelativePaths @("bin\libstlink.dll", "lib\libstlink.dll") -FileName "libstlink.dll"
    $staticSource = Find-ArtifactFile -Root $ArtifactRootPath -PreferredRelativePaths @("lib\libstlink.a") -FileName "libstlink.a"
    $importSource = Find-ArtifactFile -Root $ArtifactRootPath -PreferredRelativePaths @("lib\libstlink.dll.a", "lib\libstlink.lib") -FileName "libstlink.dll.a"
    if (-not $importSource) {
        $importSource = Find-ArtifactFile -Root $ArtifactRootPath -PreferredRelativePaths @("lib\libstlink.lib") -FileName "libstlink.lib"
    }

    if ($dllSource) {
        Copy-Item -LiteralPath $dllSource -Destination (Join-Path $binDestination "libstlink.dll") -Force
    } else {
        Write-Warning "libstlink.dll was not found under $ArtifactRootPath. Existing vendored DLL was left untouched."
    }

    if ($staticSource) {
        Copy-Item -LiteralPath $staticSource -Destination (Join-Path $libDestination "libstlink.a") -Force
    } else {
        Write-Warning "libstlink.a was not found under $ArtifactRootPath. Existing vendored static library was left untouched."
    }

    if ($importSource) {
        $targetName = [System.IO.Path]::GetFileName($importSource)
        Copy-Item -LiteralPath $importSource -Destination (Join-Path $libDestination $targetName) -Force
    } else {
        Write-Warning "No import library (libstlink.dll.a or libstlink.lib) was found under $ArtifactRootPath."
    }
}

function Get-PreparedSourceRepoPath {
    if ($SourceRepoPath) {
        $resolvedSourceRepo = Resolve-AbsolutePath -Path $SourceRepoPath
        if (-not (Test-Path -LiteralPath $resolvedSourceRepo)) {
            throw "Source repo path does not exist: $resolvedSourceRepo"
        }

        if ($Ref) {
            Write-Warning "-Ref is ignored when -SourceRepoPath points to an existing local tree. Use a checkout already positioned on the desired ref."
        }

        return $resolvedSourceRepo
    }

    $gitExe = Find-GitCommand
    $cloneRoot = Join-Path (Resolve-AbsolutePath -Path $WorkRoot) "upstream"

    if (Test-Path -LiteralPath $cloneRoot) {
        Remove-Item -LiteralPath $cloneRoot -Recurse -Force
    }
    Ensure-Directory -Path (Split-Path -Parent $cloneRoot) -BypassWhatIf

    Write-Step "Cloning upstream stlink from $RepoUrl"
    $cloneArgs = @("clone", "--depth", "1")
    if ($Ref) {
        $cloneArgs += @("--branch", $Ref)
    }
    $cloneArgs += @($RepoUrl, $cloneRoot)

    & $gitExe @cloneArgs
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE"
    }

    return $cloneRoot
}

$resolvedDestinationRoot = Resolve-AbsolutePath -Path $DestinationRoot
Ensure-Directory -Path $resolvedDestinationRoot

$preparedSourceRepo = Get-PreparedSourceRepoPath
$resolvedWorkRoot = Resolve-AbsolutePath -Path $WorkRoot
Ensure-Directory -Path $resolvedWorkRoot -BypassWhatIf

$versionInfo = Get-UpstreamVersionInfo -RepoPath $preparedSourceRepo

Write-Step "Using upstream tree: $preparedSourceRepo"
Write-Step "Detected stlink version: $($versionInfo.DisplayVersion)"

if (-not $SkipHeaders) {
    Write-Step "Syncing vendored headers"
    Sync-Headers -RepoPath $preparedSourceRepo -DestinationPath (Join-Path $resolvedDestinationRoot "include") -VersionInfo $versionInfo -StageRoot $resolvedWorkRoot
}

if (-not $SkipChips) {
    Write-Step "Syncing chip definitions"
    Sync-DirectoryContents -Source (Join-Path $preparedSourceRepo "config\chips") -Destination (Join-Path $resolvedDestinationRoot "config\chips")
}

if (-not $SkipBinaries) {
    if ($ArtifactRoot) {
        $resolvedArtifactRoot = Resolve-AbsolutePath -Path $ArtifactRoot
        Write-Step "Syncing built DLL/import archives from $resolvedArtifactRoot"
        Copy-BuildArtifacts -ArtifactRootPath $resolvedArtifactRoot -DestinationPath $resolvedDestinationRoot
    } else {
        Write-Warning "-ArtifactRoot was not provided, so bin/lib artifacts were left untouched. Only source-derived files were synced."
    }
}

if (-not $KeepWorktree -and -not $SourceRepoPath) {
    Write-Step "Cleaning temporary upstream checkout"
    Remove-Item -LiteralPath $preparedSourceRepo -Recurse -Force
}

Write-Step "Sync complete"
Write-Host ""
Write-Host "Next step: review the follow-up notes in sniffy/tools/sync_stlink/README.md before relying on the new vendor bundle at runtime."