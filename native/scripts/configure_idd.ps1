$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$path = Join-Path $root 'native\idd\upstream\video\IndirectDisplay\IddSampleDriver\Driver.cpp'
$source = [IO.File]::ReadAllText($path)
$marker = '// WiredScreen: one EDID-less 1080p60 monitor.'
if ($source.Contains($marker)) { return }
$count = 'static constexpr DWORD IDD_SAMPLE_MONITOR_COUNT = 3;'
$edid = 'if (ConnectorIndex >= ARRAYSIZE(s_SampleMonitors))'
if (-not $source.Contains($count) -or -not $source.Contains($edid)) {
    throw 'Upstream sample changed; review monitor configuration before building.'
}
$source = $source.Replace($count, "${marker}`r`nstatic constexpr DWORD IDD_SAMPLE_MONITOR_COUNT = 1;")
$source = $source.Replace($edid, 'if (true) // WiredScreen uses the default modes without sample EDIDs.')
$source = [regex]::Replace($source, '(?s)(s_SampleDefaultModes\[\]\s*=\s*)\{.*?\};', '$1{ { 1920, 1080, 60 } };')
[IO.File]::WriteAllText($path, $source)
