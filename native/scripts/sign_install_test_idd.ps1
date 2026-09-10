[CmdletBinding(SupportsShouldProcess)]
param(
    [switch]$EnableTestSigning,
    [switch]$UseExistingTrustedCertificate
)

$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated PowerShell window.'
    }
}

Assert-Administrator
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$package = Join-Path $root 'native\dist\idd'
$inf = Join-Path $package 'IddSampleDriver.inf'
$catalog = Get-ChildItem -Path $package -Filter '*.cat' -ErrorAction SilentlyContinue | Select-Object -First 1
$signTool = Join-Path $root '.tools\sdk-core-nuget-28000\c\bin\10.0.28000.0\x86\signtool.exe'

if (-not (Test-Path -LiteralPath $inf) -or $null -eq $catalog) {
    throw 'No IDD driver package was found. Run native/scripts/build_idd.ps1 first.'
}
if (-not (Test-Path -LiteralPath $signTool)) {
    throw 'signtool.exe was not found in the project-local Windows SDK package.'
}

if ($EnableTestSigning) {
    if ($PSCmdlet.ShouldProcess('Windows boot configuration', 'Enable test-signing mode (restart required)')) {
        & bcdedit /set testsigning on
        if ($LASTEXITCODE -ne 0) { throw 'Could not enable test-signing mode.' }
    }
    Write-Host 'Test-signing mode was requested. Restart Windows, then rerun this script without -EnableTestSigning.'
    return
}

$subject = 'CN=WiredScreen Test Driver'
$certificate = Get-ChildItem 'Cert:\LocalMachine\My' |
    Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
    Select-Object -First 1
if ($UseExistingTrustedCertificate) {
    if ($null -eq $certificate) { throw 'No existing signing certificate was found.' }
    foreach ($store in @('Root','TrustedPublisher')) {
        if (-not (Test-Path ("Cert:\LocalMachine\" + $store + '\' + $certificate.Thumbprint))) {
            throw "The existing certificate is not trusted in $store."
        }
    }
}
if ($null -eq $certificate) {
    if ($PSCmdlet.ShouldProcess('LocalMachine\\My', "Create $subject code-signing certificate")) {
        $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
            -CertStoreLocation 'Cert:\LocalMachine\My' -KeyUsage DigitalSignature -KeySpec Signature
    }
    else { return }
}

$certificateFile = Join-Path $package 'WiredScreenTestDriver.cer'
if (-not $UseExistingTrustedCertificate -and $PSCmdlet.ShouldProcess($certificateFile, 'Export the public test certificate')) {
    Export-Certificate -Cert $certificate -FilePath $certificateFile | Out-Null
}
if (-not $UseExistingTrustedCertificate -and $PSCmdlet.ShouldProcess('LocalMachine\\Root', 'Trust the local test certificate root')) {
    Import-Certificate -FilePath $certificateFile -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
}
if (-not $UseExistingTrustedCertificate -and $PSCmdlet.ShouldProcess('LocalMachine\\TrustedPublisher', 'Trust the local test driver publisher')) {
    Import-Certificate -FilePath $certificateFile -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
}
if ($PSCmdlet.ShouldProcess($catalog.FullName, 'Sign the IDD catalog with the local test certificate')) {
    & $signTool sign /sm /s My /fd SHA256 /sha1 $certificate.Thumbprint $catalog.FullName
    if ($LASTEXITCODE -ne 0) { throw 'Catalog signing failed.' }
}
if ($PSCmdlet.ShouldProcess($inf, 'Install the signed virtual-display driver')) {
    & pnputil /add-driver $inf /install
    if ($LASTEXITCODE -ne 0) { throw 'Driver installation failed.' }
}

Write-Host 'Test driver installed. Start WiredScreen to register its virtual display, then select Extend in Windows display settings.'
