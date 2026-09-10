"""Per-run VBV comparison; callback acknowledgement is not glass-to-glass latency."""
import json
import sys
from pathlib import Path
from analyze_latency import analyze

def compare(paths):
    runs=[]
    for path in paths:
        rows=[json.loads(line) for line in Path(path).read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        session=next((r for r in rows if r.get('type')=='session'),None)
        if session is None or 'vbvFrames' not in session:
            continue
        summary=analyze(path)
        stats=[r for r in rows if 'decodedFps' in r][5:]
        metric=summary['metrics']['sendToRenderAckMs']
        runs.append(dict(file=Path(path).name,vbvFrames=session['vbvFrames'],
            encoder=session['encoder'],encoderArguments=session.get('encoderArguments'),
            samples=summary['warmFrameAcknowledgements'],ackMs=metric,
            decodedFpsMean=sum(r['decodedFps'] for r in stats)/len(stats) if stats else None,
            statsWindows=len(stats),invalidRenderTimestamps=summary['invalidReceiverTimestamps']))
    return {'scope':'Per-run post-warmup callback acknowledgement. Excludes capture and encoding; no visual quality verdict or completion inferred from logs.', 'runs':runs}

if __name__=='__main__':
    print(json.dumps(compare(sys.argv[1:]),ensure_ascii=False,indent=2))
