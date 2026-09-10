"""Analyze v2 diagnostics. These are NOT source-to-photon latency measurements."""
import json, sys, math
from pathlib import Path

def percentile(values, p):
    values=sorted(values)
    return values[max(0, math.ceil(len(values)*p)-1)] if values else None

def analyze(path):
    rows=[json.loads(line) for line in Path(path).read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    frames=[row for row in rows if row.get('type')=='frame']
    for row in frames:
        row.setdefault('receiveToCallbackMs',row['receiveToRenderMs']+row['renderCallbackLagMs'])
    stats=[row for row in rows if 'decodedFps' in row]
    result={'file':str(path),'frameAcknowledgements':len(frames),'statsSamples':len(stats),
            'scope':'Send-to-ack includes return transit; receive-to-render starts after packet reception. Neither includes source capture/encode. No cross-device clock subtraction.',
            'metrics':{}}
    # Drop five seconds by sequence at the nominal 60 fps. This is only a
    # warmup approximation until capture timestamps are available.
    warm=[row for row in frames if row['sequence']>=300]
    result['warmup']='sequence >= 300; nominal five seconds, not source timing'
    result['warmFrameAcknowledgements']=len(warm)
    result['invalidReceiverTimestamps']=sum(row['receiveToRenderMs']<0 or row['renderCallbackLagMs']<0 for row in frames)
    for key in ('sendToRenderAckMs','receiveToCallbackMs','receiveToRenderMs','renderCallbackLagMs'):
        values=[row[key] for row in warm if row[key]>=0 and (key in ('sendToRenderAckMs','receiveToCallbackMs') or (row['receiveToRenderMs']>=0 and row['renderCallbackLagMs']>=0))]
        result['metrics'][key]={name:percentile(values,p) for name,p in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
    return result

if __name__=='__main__':
    print(json.dumps(analyze(sys.argv[1]),ensure_ascii=False,indent=2))
