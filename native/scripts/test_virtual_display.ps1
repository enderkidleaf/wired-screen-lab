param([switch]$Extend)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:PATH = (Join-Path $root 'native\dist') + ';' + $env:PATH
Add-Type -Path (Join-Path $root 'native\pc\VirtualDisplay.cs')
Add-Type -AssemblyName System.Windows.Forms
$probe = New-Object WiredScreen.VirtualDisplayController
try {
    $target = $probe.Start()
    $target | Format-List
    if ($Extend) {
        Start-Sleep -Seconds 10
        [WiredScreen.DisplayTopology]::ExtendDesktop()
        Start-Sleep -Seconds 3
    }
    [System.Windows.Forms.Screen]::AllScreens | Select-Object DeviceName,Bounds,Primary | Format-Table
    Get-PnpDevice -Class Display | Where-Object FriendlyName -Like '*IddSample*' | Format-List Status,FriendlyName,InstanceId
    Get-PnpDevice -Class Monitor -PresentOnly | Format-List Status,FriendlyName,InstanceId
    if ($Extend -and -not ([WiredScreen.DisplayTopology]::WaitForSampleDisplay(1000)).AttachedToDesktop) {
        throw 'The driver enumerated but did not attach to the desktop.'
    }
    Write-Output 'PASS: Virtual display enumerated.'
} finally {
    $probe.Dispose()
}
