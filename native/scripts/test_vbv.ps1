param([int]$Seconds=60)
$ErrorActionPreference='Stop'
if ($Seconds -lt 10) { throw 'Use at least 10 seconds per run.' }
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe=Join-Path $root 'native\dist\WiredScreen.exe'
$adb=Join-Path $root 'native\dist\adb.exe'
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run in administrator PowerShell.' }
& $adb -d get-state
if ($LASTEXITCODE -ne 0) { throw 'Connect and authorize the USB phone first.' }
# Reverse ordering in the middle round reduces systematic warmup/order bias.
foreach ($round in 1..3) {
    $order=if ($round -eq 2) { @(4,2,1,0) } else { @(0,1,2,4) }
    foreach ($frames in $order) {
        Write-Host "Round $round, VBV frames $frames"
        & $adb -d shell am force-stop com.wiredscreen.usb
        if ($LASTEXITCODE -ne 0) { throw 'Receiver reset failed.' }
        & $exe --virtual --dynamic --seconds $Seconds --vbv-frames $frames
        if ($LASTEXITCODE -ne 0) { throw "Run failed: round $round VBV $frames. Inspect native/dist/logs before continuing." }
    }
}
