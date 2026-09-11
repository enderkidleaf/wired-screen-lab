param([switch]$Encode)
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
    if($Encode){
        $encoded=Join-Path $root 'artifacts\idd-native.h264'
        $probeArguments='--encode-driver "'+$encoded+'"'
    } else { $probeArguments='--driver' }
    # Bounded child process with UTF-8 output, including on Windows PowerShell.
    [WiredScreen.Engine]::Command((Join-Path $root 'native\dist\GpuHandoffProbe.exe'),$probeArguments,25000) | Write-Output
    if($Encode){
        $checksums=Join-Path $root 'artifacts\idd-native.framemd5'
        & (Join-Path $root 'native\dist\ffmpeg.exe') -nostdin -hide_banner -loglevel error -xerror -f h264 -i $encoded -fps_mode passthrough -f framemd5 -y $checksums
        if($LASTEXITCODE -ne 0){throw 'Native IDD H264 decode failed'}
        $frames=@(Get-Content -LiteralPath $checksums | Where-Object {$_ -and -not $_.StartsWith('#')})
        if($frames.Count -ne 3){throw "Expected 3 decoded frames, got $($frames.Count)"}
        Write-Output 'PASS: 3 actual IDD frames encoded on GPU and independently decoded.'
    }
} finally {
    if($null -ne $pattern){if(-not $pattern.HasExited){$pattern.CloseMainWindow() | Out-Null;if(-not $pattern.WaitForExit(2000)){$pattern.Kill()}};$pattern.Dispose()}
    $display.Dispose()
}
