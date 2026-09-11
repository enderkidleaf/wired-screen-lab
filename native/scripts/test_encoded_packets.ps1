$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
[Reflection.Assembly]::LoadFrom((Join-Path $root 'native\dist\WiredScreen.exe')) | Out-Null
$ffmpeg=Join-Path $root 'native\dist\ffmpeg.exe'
$options=New-Object WiredScreen.Options
$arguments=[WiredScreen.Engine]::Arguments($options,'h264_nvenc')
$arguments=$arguments.Replace('-f avi pipe:1','-frames:v 3 -f avi pipe:1')
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=$ffmpeg
$info.Arguments=$arguments
$info.UseShellExecute=$false
$info.CreateNoWindow=$true
$info.RedirectStandardOutput=$true
$info.RedirectStandardError=$true
$process=[Diagnostics.Process]::Start($info)
$errors=$process.StandardError.ReadToEndAsync()
$output=Join-Path $root 'artifacts\packet-roundtrip.h264'
$file=[IO.File]::Create($output)
try {
    $reader=New-Object WiredScreen.EncodedPacketReader($process.StandardOutput.BaseStream)
    $packet=$null
    $count=0
    while($reader.Read([ref]$packet)){$file.Write($packet,0,$packet.Length);$count++}
    if(-not $process.WaitForExit(5000)){throw 'Encoder did not exit'}
    if($process.ExitCode -ne 0){throw $errors.Result}
    if($count -ne 3){throw "Expected 3 encoded packets, received $count"}
} finally {
    $file.Dispose()
    if(-not $process.HasExited){$process.Kill()}
    $process.Dispose()
}
& $ffmpeg -hide_banner -loglevel error -xerror -f h264 -i $output -f null -
if($LASTEXITCODE -ne 0){throw 'Extracted H264 failed decoding'}
Write-Output 'PASS: 3 hardware-encoded packets read from non-seekable stdout and decoded.'
