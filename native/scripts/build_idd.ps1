[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64','ARM64')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$solution = Join-Path $root 'native\idd\upstream\video\IndirectDisplay\IddSampleDriver.sln'
if (-not (Test-Path $solution)) {
    throw 'Microsoft IDD sample source is missing. Populate native/idd/upstream/video/IndirectDisplay first.'
}

$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$kits = Join-Path $programFilesX86 'Windows Kits\10\Include'
$iddcx = Get-ChildItem $kits -Recurse -Filter iddcx.h -ErrorAction SilentlyContinue | Select-Object -First 1
$nugetHeader = Join-Path $root '.tools\wdk-nuget-28000\c\Include\10.0.28000.0\um\iddcx\1.0\IddCx.h'
if ($null -eq $iddcx) {
    if (-not (Test-Path $nugetHeader)) {
        throw 'WDK IddCx headers were not found. Install Windows Driver Kit or run the project bootstrap to acquire the official WDK NuGet package.'
    }
    Write-Host 'Using the project-local official WDK NuGet package.'
}

$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer was not found.' }
$vs = & $vswhere -all -products * -property installationPath |
    Where-Object { Test-Path (Join-Path $_ 'MSBuild\Current\Bin\MSBuild.exe') } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($vs)) { throw 'A Visual Studio installation with MSBuild was not found.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path $msbuild)) {
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
}
if (-not (Test-Path $msbuild)) { throw 'MSBuild was not found.' }

# The headers in the WDK NuGet package are enough for code completion, but the
# official sample also needs Visual Studio's WDK platform toolset.  Check it
# explicitly so a partial WDK installation produces an actionable message.
$vsMajor = Split-Path (Split-Path $vs -Parent) -Leaf
$vcToolsRoot = Join-Path $vs "MSBuild\Microsoft\VC\v${vsMajor}0\Platforms\$Platform\PlatformToolsets"
$driverToolset = Join-Path $vcToolsRoot 'WindowsUserModeDriver10.0'
if (-not (Test-Path $driverToolset)) {
    throw "The WindowsUserModeDriver10.0 platform toolset is not installed for Visual Studio. Complete the Windows Driver Kit installation (including Visual Studio integration), then retry. Expected: $driverToolset"
}

$directoryProps = Join-Path $root 'native\idd\Directory.Build.props'
$analysisOverride = Join-Path $root 'native\idd\NoStaticAnalysis.targets'
& $msbuild $solution "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:WindowsTargetPlatformVersion=10.0.28000.0" "/p:EnableTestSign=false" "/p:RunCodeAnalysis=false" "/p:DirectoryBuildPropsPath=$directoryProps" "/p:ForceImportAfterCppTargets=$analysisOverride" /m
if ($LASTEXITCODE -ne 0) { throw 'IDD driver build failed.' }

$sampleRoot = Split-Path $solution -Parent
$driverPackage = Get-ChildItem $sampleRoot -Recurse -Directory -Filter IddSampleDriver |
    Where-Object {
        $_.FullName -match "\\$Platform\\$Configuration\\" -and
        (Test-Path (Join-Path $_.FullName 'IddSampleDriver.dll')) -and
        (Test-Path (Join-Path $_.FullName 'IddSampleDriver.inf'))
    } |
    Select-Object -First 1
if ($null -eq $driverPackage) { throw 'Build completed but no installable driver package was found. Check the driver package settings in Visual Studio.' }
$dll = Get-Item (Join-Path $driverPackage.FullName 'IddSampleDriver.dll')
$inf = Get-Item (Join-Path $driverPackage.FullName 'IddSampleDriver.inf')

$package = Join-Path $root 'native\dist\idd'
New-Item -ItemType Directory -Force -Path $package | Out-Null
Copy-Item $dll.FullName, $inf.FullName -Destination $package -Force
$cat = Get-ChildItem $driverPackage.FullName -Filter '*.cat' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -ne $cat) { Copy-Item $cat.FullName -Destination $package -Force }
Write-Host "IDD build output: $package"
Write-Host 'Install with a trusted test or production signature, then run pnputil /add-driver IddSampleDriver.inf /install as Administrator.'
