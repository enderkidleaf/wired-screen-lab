"""Short synthetic GPU encode/decode test. No phone or driver registration."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
DIST = ROOT / "native" / "dist"
ARTIFACTS = ROOT / "artifacts"
ARTIFACTS.mkdir(exist_ok=True)
encoded = ARTIFACTS / "native-encode-test.h264"
subprocess.run([str(DIST / "GpuHandoffProbe.exe"), "--color-test"], timeout=10, check=True)
result = subprocess.run(
    [str(DIST / "GpuHandoffProbe.exe"), "--encode-test", str(encoded)],
    capture_output=True, timeout=25, check=True,
)
log = result.stdout.decode("utf-8", errors="replace")
(ARTIFACTS / "native-encode-test.txt").write_text(log, encoding="utf-8")
decoded = subprocess.run(
    [str(DIST / "ffmpeg.exe"), "-nostdin", "-hide_banner", "-loglevel", "error",
     "-xerror", "-f", "h264", "-i", str(encoded), "-frames:v", "4",
     "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"],
    capture_output=True, timeout=15, check=True,
).stdout
frame_bytes = 1920 * 1080 * 3
assert len(decoded) == frame_bytes * 3, "Expected exactly three 1080p RGB frames"
for index in range(3):
    frame = decoded[index * frame_bytes:(index + 1) * frame_bytes]
    means = [sum(frame[channel::3]) / (1920 * 1080) for channel in range(3)]
    assert all(abs(actual - expected) < 4 for actual, expected in zip(means, (204, 68, 34))), means
    print(f"Frame {index + 1}: decoded RGB mean {means}")
print("PASS: 3 native GPU-encoded frames decoded; BT.709 color roundtrip within tolerance.")
