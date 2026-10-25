<#
.SYNOPSIS
Interactively build and package x64 MinGW or MSVC DC++ artifacts.

.DESCRIPTION
Interactive helper for producing a distribution zip. The zip contains the
DCPlusPlus.exe, its matching GNU debug companion or native Microsoft PDB,
changelog-bfe.txt, and the distributable theme, emoticon, and icon-pack collections.

Package names use:
DCPlusPlus-Experimental-VERSION-COMPILER-CONFIGURATION-RUNTIME-BUILDSYSTEM-gitrev-yyyyMMdd-HHmmssZ.zip

By default the script prompts for the compiler and build configuration and writes packages
to the root-level dist directory. It can also be used non-interactively:

  powershell -ExecutionPolicy Bypass -File scripts\dist-build.ps1 -Configuration Both
  powershell -ExecutionPolicy Bypass -File scripts\dist-build.ps1 -Compiler MSVC -Configuration Release
  powershell -ExecutionPolicy Bypass -File scripts\dist-build.ps1 -Compiler MinGW -BuildSystem CMake -Configuration Debug

MinGW supports SCons (the default) or CMake. MSVC uses the native CMake solution.
Supplying Configuration without Compiler retains the existing MinGW/SCons behavior.
Use -NoBuild to package existing outputs. MSVC Release packages require the Visual
C++ runtime; MSVC Debug packages require the Visual Studio debug runtime. These
runtimes are not bundled in the zip.

.NOTES
Copyright (C) 2026 iceman50
#>

[CmdletBinding()]
param(
	[ValidateSet("Debug", "Release", "Both")]
	[string]$Configuration,

	[ValidateSet("MinGW", "MSVC")]
	[string]$Compiler,

	[ValidateSet("Auto", "SCons", "CMake")]
	[string]$BuildSystem = "Auto",

	[ValidateSet("default", "nehalem", "x86-64-v2", "x86-64-v3")]
	[string]$CpuTarget = "default",

	[string]$OutputDirectory,

	[string]$SConsCommand = "scons",

	[string]$CMakeCommand = "cmake",

	[ValidateRange(1, 256)]
	[int]$Jobs = 8,

	[string]$CompilerLabel,

	[switch]$NoBuild,

	[switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
	return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

function Read-Configuration {
	Write-Host ""
	Write-Host "Select x64 build configuration:"
	Write-Host "  1. Debug"
	Write-Host "  2. Release"
	Write-Host "  3. Both"

	while($true) {
		$choice = Read-Host "Build configuration [3]"
		if([string]::IsNullOrWhiteSpace($choice)) {
			return "Both"
		}

		switch($choice.Trim().ToLowerInvariant()) {
			"1" { return "Debug" }
			"2" { return "Release" }
			"3" { return "Both" }
			"debug" { return "Debug" }
			"release" { return "Release" }
			"both" { return "Both" }
			default { Write-Host "Please enter 1, 2, or 3." }
		}
	}
}

function Read-Compiler {
	Write-Host ""
	Write-Host "Select x64 compiler:"
	Write-Host "  1. MinGW-w64"
	Write-Host "  2. MSVC (Visual Studio 2026)"
	while($true) {
		$choice = Read-Host "Compiler [1]"
		if([string]::IsNullOrWhiteSpace($choice)) { return "MinGW" }
		switch($choice.Trim().ToLowerInvariant()) {
			"1" { return "MinGW" }
			"mingw" { return "MinGW" }
			"2" { return "MSVC" }
			"msvc" { return "MSVC" }
			default { Write-Host "Please enter 1 or 2." }
		}
	}
}

function Read-BuildSystem {
	Write-Host ""
	Write-Host "Select MinGW build system:"
	Write-Host "  1. SCons"
	Write-Host "  2. CMake (isolated build)"
	while($true) {
		$choice = Read-Host "Build system [1]"
		if([string]::IsNullOrWhiteSpace($choice)) { return "SCons" }
		switch($choice.Trim().ToLowerInvariant()) {
			"1" { return "SCons" }
			"scons" { return "SCons" }
			"2" { return "CMake" }
			"cmake" { return "CMake" }
			default { Write-Host "Please enter 1 or 2." }
		}
	}
}

function Get-BuildDirectory {
	param([string]$RepoRoot, [string]$Mode)
	$suffix = Get-CpuSuffix
	if($BuildSystem -eq "SCons") { return Join-Path $RepoRoot "build\$Mode-mingw-x64$suffix" }
	if($Compiler -eq "MSVC") { return Join-Path $RepoRoot "build\cmake\msvc" }
	return Join-Path $RepoRoot "build\cmake\mingw-$Mode$suffix"
}

function Get-CpuSuffix {
	if($CpuTarget -in @("default", "nehalem")) { return "" }
	return "-$CpuTarget"
}

function Get-BinaryDirectory {
	param([string]$RepoRoot, [string]$Mode)
	$buildDirectory = Get-BuildDirectory -RepoRoot $RepoRoot -Mode $Mode
	if($BuildSystem -eq "SCons") { return Join-Path $buildDirectory "bin" }
	return Join-Path $buildDirectory "bin\$(Get-PackageConfigurationName -Mode $Mode)"
}

function Get-ModeList {
	param(
		[string]$SelectedConfiguration
	)

	switch($SelectedConfiguration.ToLowerInvariant()) {
		"debug" { return @("debug") }
		"release" { return @("release") }
		"both" { return @("debug", "release") }
		default { throw "Unknown configuration: $SelectedConfiguration" }
	}
}

function Assert-RequiredFile {
	param(
		[string]$Path,
		[string]$Description
	)

	if(-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "$Description was not found: $Path"
	}
}

function Assert-PathUnderRoot {
	param(
		[string]$Path,
		[string]$Root
	)

	$fullPath = [System.IO.Path]::GetFullPath($Path)
	$fullRoot = [System.IO.Path]::GetFullPath($Root)
	if(-not $fullRoot.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
		$fullRoot += [System.IO.Path]::DirectorySeparatorChar
	}

	if(-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to remove path outside expected root. Path: $fullPath Root: $fullRoot"
	}
}

function Resolve-OutputDirectory {
	param(
		[string]$Path,
		[string]$RepoRoot
	)

	if([System.IO.Path]::IsPathRooted($Path)) {
		return [System.IO.Path]::GetFullPath($Path)
	}

	return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Get-VersionNumber {
	param(
		[string]$RepoRoot
	)

	$versionHeader = Join-Path $RepoRoot "dcpp\version.h"
	$versionLine = Select-String -LiteralPath $versionHeader -Pattern '^\s*#define\s+VERSIONSTRING\s+"([^"]+)"' | Select-Object -First 1
	if($null -eq $versionLine) {
		throw "Unable to find VERSIONSTRING in $versionHeader"
	}

	return $versionLine.Matches[0].Groups[1].Value
}

function Get-GitRevision {
	param(
		[string]$RepoRoot
	)

	Push-Location -LiteralPath $RepoRoot
	try {
		$revision = (& git rev-parse --short HEAD 2>$null)
		if($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
			throw "git rev-parse failed"
		}
		return $revision.Trim()
	} catch {
		$revisionFile = Join-Path $RepoRoot "dcpp\version-rev-id.inc"
		$revisionLine = Select-String -LiteralPath $revisionFile -Pattern '\[([0-9a-fA-F]+)\]' | Select-Object -First 1
		if($null -eq $revisionLine) {
			throw "Unable to determine Git revision from Git or $revisionFile"
		}
		return $revisionLine.Matches[0].Groups[1].Value
	} finally {
		Pop-Location
	}
}

function ConvertTo-PackageToken {
	param(
		[string]$Value
	)

	$token = $Value.Trim() -replace '[^\w.-]+', '-'
	$token = $token.Trim("-")
	if([string]::IsNullOrWhiteSpace($token)) {
		throw "Package token cannot be empty."
	}
	return $token
}

function Get-CompilerLabel {
	param([string]$RepoRoot, [string]$Mode)
	if($BuildSystem -eq "CMake") {
		$directory = Get-BuildDirectory -RepoRoot $RepoRoot -Mode $Mode
		$metadata = Get-ChildItem -LiteralPath (Join-Path $directory "CMakeFiles") -Filter CMakeCXXCompiler.cmake -Recurse -ErrorAction SilentlyContinue |
			Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
		if($null -eq $metadata) { throw "CMake compiler metadata is missing in $directory. Build first or pass -CompilerLabel." }
		$contents = Get-Content -LiteralPath $metadata.FullName -Raw
		$versionMatch = [regex]::Match($contents, 'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)')
		$expectedId = if($Compiler -eq "MSVC") { "MSVC" } else { "GNU" }
		if(-not $versionMatch.Success -or $contents -notmatch ('set\(CMAKE_CXX_COMPILER_ID "' + $expectedId + '"\)')) {
			throw "CMake compiler metadata does not match the selected $Compiler compiler: $($metadata.FullName)"
		}
		$label = if($Compiler -eq "MSVC") { "MSVC" } else { "MinGW-w64" }
		return ConvertTo-PackageToken -Value "$label-$($versionMatch.Groups[1].Value)"
	}
	$compilerCandidates = @(
		"x86_64-w64-mingw32-g++",
		"g++"
	)

	foreach($compiler in $compilerCandidates) {
		$command = Get-Command $compiler -ErrorAction SilentlyContinue
		if($null -eq $command) {
			continue
		}

		$version = (& $command.Source -dumpfullversion -dumpversion 2>$null | Select-Object -First 1)
		if($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($version)) {
			return ConvertTo-PackageToken -Value "MinGW-w64-$($version.Trim())"
		}
	}

	throw "Unable to determine MinGW compiler version. Pass -CompilerLabel to set it explicitly."
}

function Get-PackageConfigurationName {
	param(
		[string]$Mode
	)

	switch($Mode.ToLowerInvariant()) {
		"debug" { return "Debug" }
		"release" { return "Release" }
		default { throw "Unknown mode: $Mode" }
	}
}

function Invoke-DistBuild {
	param(
		[string]$RepoRoot,
		[string]$Mode
	)

	Write-Host ""
	Write-Host "Building $Mode x64 $Compiler with $BuildSystem (CPU: $CpuTarget)..."
	Push-Location -LiteralPath $RepoRoot
	try {
		if($BuildSystem -eq "SCons") {
			$suffix = Get-CpuSuffix
			$sconsArgs = @("tools=mingw", "arch=x64", "mode=$Mode", "cpu=$CpuTarget", "-j$Jobs", "build/$Mode-mingw-x64$suffix/bin/DCPlusPlus.exe")
			Write-Host "$SConsCommand $($sconsArgs -join ' ')"
			& $SConsCommand @sconsArgs
			if($LASTEXITCODE -ne 0) { throw "SCons failed for $Mode with exit code $LASTEXITCODE." }
		} else {
			$preset = if($Compiler -eq "MSVC") { "msvc" } else { "mingw-$Mode" }
			$configureArgs = @("-S", (Join-Path $RepoRoot "cmake"), "--preset", $preset)
			$buildDirectory = Get-BuildDirectory -RepoRoot $RepoRoot -Mode $Mode
			$configureArgs += @("-B", $buildDirectory, "-DDCPP_CPU_TARGET=$CpuTarget")
			if($Compiler -eq "MinGW" -and -not (Test-Path -LiteralPath (Join-Path $buildDirectory "CMakeCache.txt"))) {
				$ninja = Get-Command ninja -ErrorAction SilentlyContinue
				if($null -ne $ninja) {
					$configureArgs += "-DCMAKE_MAKE_PROGRAM=$($ninja.Source)"
				} elseif(${env:ProgramFiles(x86)}) {
					$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
					if(Test-Path -LiteralPath $vswhere) {
						$ninjaPath = & $vswhere -latest -products '*' -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' | Select-Object -First 1
						if($LASTEXITCODE -eq 0 -and $ninjaPath) { $configureArgs += "-DCMAKE_MAKE_PROGRAM=$ninjaPath" }
					}
				}
			}
			& $CMakeCommand @configureArgs
			if($LASTEXITCODE -ne 0) { throw "CMake configuration failed for $preset with exit code $LASTEXITCODE." }
			& $CMakeCommand --build $buildDirectory --config (Get-PackageConfigurationName -Mode $Mode) --target application --parallel $Jobs
			if($LASTEXITCODE -ne 0) { throw "CMake build failed for $preset/$Mode with exit code $LASTEXITCODE." }
		}
	} finally {
		Pop-Location
	}
}

function New-DistPackage {
	param(
		[string]$RepoRoot,
		[string]$Mode,
		[string]$OutputRoot,
		[string]$VersionNumber,
		[string]$CompilerName,
		[string]$GitRevision,
		[string]$BuildTimestamp,
		[bool]$OverwriteExisting
	)

	$cpuSuffix = Get-CpuSuffix
	$buildName = "$Mode-$Compiler-$BuildSystem-x64$cpuSuffix"
	$binDir = Get-BinaryDirectory -RepoRoot $RepoRoot -Mode $Mode
	$exePath = Join-Path $binDir "DCPlusPlus.exe"
	$pdbPath = Join-Path $binDir "DCPlusPlus.pdb"
	$changelogPath = Join-Path $RepoRoot "changelog-bfe.txt"
	$themesPath = Join-Path $RepoRoot "Themes"
	$emoticonPath = Join-Path $RepoRoot "Emoticons"
	$iconPacksPath = Join-Path $RepoRoot "IconPacks"

	Assert-RequiredFile -Path $exePath -Description "$Compiler executable"
	Assert-RequiredFile -Path $pdbPath -Description "Debug companion PDB"
	Assert-RequiredFile -Path $changelogPath -Description "BFE changelog"
	if(-not (Test-Path -LiteralPath $themesPath -PathType Container)) {
		throw "Themes directory was not found: $themesPath"
	}
	if(-not (Test-Path -LiteralPath $emoticonPath -PathType Container)) {
		throw "Emoticons directory was not found: $emoticonPath"
	}
	if(-not (Test-Path -LiteralPath $iconPacksPath -PathType Container)) {
		throw "IconPacks directory was not found: $iconPacksPath"
	}
	Assert-RequiredFile -Path (Join-Path $iconPacksPath "Dark.dcico") -Description "Bundled dark icon package"
	Assert-RequiredFile -Path (Join-Path $iconPacksPath "Neon-Circuit.dcico") -Description "Bundled Neon Circuit icon package"
	foreach($packName in @("Aurora", "Copper", "Paper")) {
		Assert-RequiredFile -Path (Join-Path $iconPacksPath "$packName.dcico") -Description "Bundled $packName icon package"
	}

	$configurationName = Get-PackageConfigurationName -Mode $Mode
	$runtimeName = if($Compiler -eq "MinGW") { "MSVCRT" } elseif($Mode -eq "debug") { "DebugCRT" } else { "UCRT" }
	$zipName = "DCPlusPlus-Experimental-$VersionNumber-$CompilerName-$configurationName-$runtimeName-$BuildSystem$cpuSuffix-$GitRevision-$BuildTimestamp.zip"
	$zipPath = Join-Path $OutputRoot $zipName
	$stageDir = Join-Path $OutputRoot ".stage-$buildName-$([Guid]::NewGuid().ToString('N'))"
	$temporaryZip = "$stageDir.zip"

	if((Test-Path -LiteralPath $zipPath) -and -not $OverwriteExisting) {
		throw "Package already exists: $zipPath. Re-run with -Force to replace it."
	}

	New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
	New-Item -ItemType Directory -Path $stageDir | Out-Null

	try {
		Copy-Item -LiteralPath $exePath -Destination (Join-Path $stageDir "DCPlusPlus.exe") -Force
		Copy-Item -LiteralPath $pdbPath -Destination (Join-Path $stageDir "DCPlusPlus.pdb") -Force
		Copy-Item -LiteralPath $changelogPath -Destination (Join-Path $stageDir "changelog-bfe.txt") -Force
		Copy-Item -LiteralPath $themesPath -Destination (Join-Path $stageDir "Themes") -Recurse -Force
		Copy-Item -LiteralPath $emoticonPath -Destination (Join-Path $stageDir "Emoticons") -Recurse -Force
		Copy-Item -LiteralPath $iconPacksPath -Destination (Join-Path $stageDir "IconPacks") -Recurse -Force

		Get-ChildItem -LiteralPath $stageDir | Compress-Archive -DestinationPath $temporaryZip -CompressionLevel Optimal
		Move-Item -LiteralPath $temporaryZip -Destination $zipPath -Force:$OverwriteExisting
		Write-Host "Created $zipPath"
		return $zipPath
	} finally {
		if(Test-Path -LiteralPath $temporaryZip) {
			Assert-PathUnderRoot -Path $temporaryZip -Root $OutputRoot
			Remove-Item -LiteralPath $temporaryZip -Force
		}
		if(Test-Path -LiteralPath $stageDir) {
			Assert-PathUnderRoot -Path $stageDir -Root $OutputRoot
			Remove-Item -LiteralPath $stageDir -Recurse -Force
		}
	}
}

$repoRoot = Get-RepositoryRoot
$CpuTarget = $CpuTarget.ToLowerInvariant()
$interactive = -not $PSBoundParameters.ContainsKey("Configuration")

if([string]::IsNullOrWhiteSpace($Compiler)) {
	$Compiler = if($interactive) { Read-Compiler } else { "MinGW" }
}

if([string]::IsNullOrWhiteSpace($Configuration)) {
	$Configuration = Read-Configuration
}

if($BuildSystem -eq "Auto") {
	$BuildSystem = if($Compiler -eq "MSVC") { "CMake" } elseif($interactive) { Read-BuildSystem } else { "SCons" }
}
if($Compiler -eq "MSVC" -and $BuildSystem -ne "CMake") {
	throw "MSVC distribution builds use CMake. Select -BuildSystem CMake or omit -BuildSystem."
}
if($Compiler -eq "MSVC" -and $CpuTarget -ne "default") {
	throw "-CpuTarget requires MinGW. Use -CpuTarget default for MSVC."
}

$defaultOutputDirectory = Join-Path $repoRoot "dist"
if([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = $defaultOutputDirectory
}
$OutputDirectory = Resolve-OutputDirectory -Path $OutputDirectory -RepoRoot $repoRoot
$versionNumber = Get-VersionNumber -RepoRoot $repoRoot
$gitRevision = Get-GitRevision -RepoRoot $repoRoot
$buildTimestamp = [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss'Z'", [System.Globalization.CultureInfo]::InvariantCulture)

$modes = Get-ModeList -SelectedConfiguration $Configuration
$packages = @()

foreach($mode in $modes) {
	if(-not $NoBuild) {
		Invoke-DistBuild -RepoRoot $repoRoot -Mode $mode
	}
	$compilerName = if([string]::IsNullOrWhiteSpace($CompilerLabel)) {
		Get-CompilerLabel -RepoRoot $repoRoot -Mode $mode
	} else {
		ConvertTo-PackageToken -Value $CompilerLabel
	}
	$packages += New-DistPackage -RepoRoot $repoRoot -Mode $mode -OutputRoot $OutputDirectory -VersionNumber $versionNumber -CompilerName $compilerName -GitRevision $gitRevision -BuildTimestamp $buildTimestamp -OverwriteExisting ([bool]$Force)
}

Write-Host ""
Write-Host "Distribution package(s):"
foreach($package in $packages) {
	Write-Host "  $package"
}
