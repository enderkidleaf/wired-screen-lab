$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:PATH=(Join-Path $root 'native\dist')+';'+$env:PATH
[Reflection.Assembly]::LoadFrom((Join-Path $root 'native\dist\WiredScreen.exe')) | Out-Null
$ffmpeg=Join-Path $root 'native\dist\ffmpeg.exe'
$artifacts=Join-Path $root 'artifacts'
[IO.Directory]::CreateDirectory($artifacts) | Out-Null
$avi=Join-Path $artifacts 'gpu-roundtrip.avi'
$h264=Join-Path $artifacts 'gpu-roundtrip.h264'
$display=New-Object WiredScreen.VirtualDisplayController
try {
    $target=$display.Start()
    Start-Sleep -Seconds 10
    [WiredScreen.DisplayTopology]::ExtendDesktop()
    Start-Sleep -Seconds 2
    [uint32]$adapter=0; [uint32]$output=0
    $hr=[WiredScreen.DisplayTopology]::WiredScreenFindOutput($target.DeviceName,[ref]$adapter,[ref]$output)
    if($hr -lt 0){throw "Virtual display DXGI lookup failed: $hr"}
    $options=New-Object WiredScreen.Options
    $options.Source='desktop'; $options.Adapter=$adapter; $options.Screen=$output; $options.GpuFrames=$true
    $arguments=[WiredScreen.Engine]::Arguments($options,'h264_qsv')
    $arguments=$arguments.Replace('-f avi pipe:1',('-frames:v 3 -y -f avi "'+$avi+'"'))
    [WiredScreen.Engine]::Command($ffmpeg,$arguments,15000) | Out-Null
    $inputFile=[IO.File]::OpenRead($avi)
    $outputFile=[IO.File]::Create($h264)
    try {
        $reader=New-Object WiredScreen.EncodedPacketReader($inputFile)
        $packet=$null; $count=0
        while($reader.Read([ref]$packet)){$outputFile.Write($packet,0,$packet.Length);$count++}
        if($count -ne 3){throw "Expected 3 encoded packets, received $count"}
    } finally {$inputFile.Dispose();$outputFile.Dispose()}
    [WiredScreen.Engine]::Command($ffmpeg,('-hide_banner -loglevel error -xerror -f h264 -i "'+$h264+'" -f null -'),15000) | Out-Null
    Write-Output "PASS: virtual display $($target.DeviceName), adapter=$adapter output=$output; 3 GPU QSV packets extracted and decoded. No phone latency claim."
} finally {$display.Dispose()}
