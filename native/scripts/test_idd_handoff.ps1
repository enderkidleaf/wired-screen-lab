$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:PATH=(Join-Path $root 'native\dist')+';'+$env:PATH
[Reflection.Assembly]::LoadFrom((Join-Path $root 'native\dist\WiredScreen.exe')) | Out-Null
$display=New-Object WiredScreen.VirtualDisplayController
$pattern=$null
try {
    $target=$display.Start()
    Start-Sleep -Seconds 10
    [WiredScreen.DisplayTopology]::ExtendDesktop()
    Start-Sleep -Seconds 2
    $pattern=Start-Process -FilePath (Join-Path $root 'native\dist\WiredScreen.exe') -ArgumentList @('--pattern','--display',$target.DeviceName) -PassThru -WindowStyle Hidden
    & (Join-Path $root 'native\dist\GpuHandoffProbe.exe') --driver
    if($LASTEXITCODE -ne 0){throw 'IDD shared texture probe failed'}
} finally {
    if($null -ne $pattern){if(-not $pattern.HasExited){$pattern.CloseMainWindow() | Out-Null;if(-not $pattern.WaitForExit(2000)){$pattern.Kill()}};$pattern.Dispose()}
    $display.Dispose()
}
