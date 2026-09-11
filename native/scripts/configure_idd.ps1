$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$path = Join-Path $root 'native\idd\upstream\video\IndirectDisplay\IddSampleDriver\Driver.cpp'
$source = [IO.File]::ReadAllText($path)
$marker = '// WiredScreen: one EDID-less 1080p60 monitor.'
if (-not $source.Contains($marker)) {
$count = 'static constexpr DWORD IDD_SAMPLE_MONITOR_COUNT = 3;'
$edid = 'if (ConnectorIndex >= ARRAYSIZE(s_SampleMonitors))'
if (-not $source.Contains($count) -or -not $source.Contains($edid)) {
    throw 'Upstream sample changed; review monitor configuration before building.'
}
$source = $source.Replace($count, "${marker}`r`nstatic constexpr DWORD IDD_SAMPLE_MONITOR_COUNT = 1;")
$source = $source.Replace($edid, 'if (true) // WiredScreen uses the default modes without sample EDIDs.')
$source = [regex]::Replace($source, '(?s)(s_SampleDefaultModes\[\]\s*=\s*)\{.*?\};', '$1{ { 1920, 1080, 60 } };')
}
$handoffMarker='// WiredScreen: optional GPU handoff worker.'
if (-not $source.Contains($handoffMarker)) {
    $anchor='    // Acquire and release buffers in a loop'
    $frameAnchor='            AcquiredBuffer.Attach(Buffer.MetaData.pSurface);'
    if (-not $source.Contains($anchor) -or -not $source.Contains($frameAnchor)) { throw 'Upstream frame loop changed; review GPU handoff integration.' }
    $source=$source.Replace('#include "Driver.h"', "#include `"Driver.h`"`r`n#ifdef WIRED_SCREEN_GPU_HANDOFF`r`n#include `"GpuHandoff.h`"`r`n#endif")
    $setup=@'
    // WiredScreen: optional GPU handoff worker.
#ifdef WIRED_SCREEN_GPU_HANDOFF
    WiredScreenGpu::HandoffServer handoff;
    const HRESULT handoffInit = handoff.Init(m_Device->Device.Get());
    uint64_t sourceSequence = 0;
#endif

'@
    $source=$source.Replace($anchor,$setup+"`r`n"+$anchor)
    $copy=@'

#ifdef WIRED_SCREEN_GPU_HANDOFF
            if (SUCCEEDED(handoffInit)) {
                LARGE_INTEGER captured; QueryPerformanceCounter(&captured);
                ComPtr<ID3D11Texture2D> sourceTexture;
                if (SUCCEEDED(AcquiredBuffer.As(&sourceTexture))) {
                    // No pipe I/O or consumer wait in the swapchain loop.
                    handoff.Publish(sourceTexture.Get(), ++sourceSequence, captured.QuadPart);
                }
            }
#endif
'@
    $source=$source.Replace($frameAnchor,$frameAnchor+"`r`n"+$copy)
}
foreach($header in @('SharedTexturePool.h','GpuHandoff.h')) {
    Copy-Item -LiteralPath (Join-Path $root "native\pc\$header") -Destination (Join-Path (Split-Path $path -Parent) $header) -Force
}
[IO.File]::WriteAllText($path, $source)
