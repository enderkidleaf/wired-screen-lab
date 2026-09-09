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
if ($null -eq $iddcx) {
    throw 'WDK IddCx headers were not found. Install Windows Driver Kit through Visual Studio Installer, then retry.'
}

$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer was not found.' }
$vs = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($vs)) { throw 'Visual Studio C++ tools were not found.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw 'MSBuild was not found.' }

& $msbuild $solution "/p:Configuration=$Configuration" "/p:Platform=$Platform" /m
if ($LASTEXITCODE -ne 0) { throw 'IDD driver build failed.' }

$sampleRoot = Split-Path $solution -Parent
$dll = Get-ChildItem $sampleRoot -Recurse -Filter IddSampleDriver.dll | Where-Object { $_.FullName -match "\\$Platform\\$Configuration\\" } | Select-Object -First 1
$inf = Get-ChildItem $sampleRoot -Recurse -Filter IddSampleDriver.inf | Where-Object { $_.FullName -match "\\$Platform\\$Configuration\\" } | Select-Object -First 1
if ($null -eq $dll -or $null -eq $inf) { throw 'Build completed but no installable DLL/INF output was found. Check the driver package settings in Visual Studio.' }

$package = Join-Path $root 'native\dist\idd'
New-Item -ItemType Directory -Force -Path $package | Out-Null
Copy-Item $dll.FullName, $inf.FullName -Destination $package -Force
$cat = Get-ChildItem $inf.Directory -Filter IddSampleDriver.cat -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -ne $cat) { Copy-Item $cat.FullName -Destination $package -Force }
Write-Host "IDD build output: $package"
Write-Host 'Install with a trusted test or production signature, then run pnputil /add-driver IddSampleDriver.inf /install as Administrator.'
