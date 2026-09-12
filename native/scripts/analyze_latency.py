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
    native=[row for row in rows if row.get('type')=='native-frame']
    result={'file':str(path),'frameAcknowledgements':len(frames),'statsSamples':len(stats),
            'scope':'Send-to-ack includes return transit; receive-to-render starts after packet reception. Neither includes source capture/encode. No cross-device clock subtraction.',
            'metrics':{}}
    # Drop five seconds by sequence at the nominal 60 fps. This is only a
    # warmup approximation until capture timestamps are available.
    warm=[row for row in frames if row['sequence']>=300]
    result['warmup']='sequence >= 300; nominal five seconds, not source timing'
    if native:
        start=native[0]['sendQpc']
        warm_native=[row for row in native if (row['sendQpc']-start)/row['qpcFrequency']>=5]
        by_sequence={row['sequence']:row for row in warm_native}
        warm=[row for row in frames if row['sequence'] in by_sequence]
        result['warmup']='five actual PC-clock seconds since first native send'
        result['nativeFrames']=len(native)
        result['nativeMetrics']={}
        for key in ('captureToEncodedMs','encodedToSendMs'):
            values=[row[key] for row in warm_native]
            result['nativeMetrics'][key]={name:percentile(values,p) for name,p in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
        values=[by_sequence[row['sequence']]['captureToEncodedMs']+by_sequence[row['sequence']]['encodedToSendMs']+row['sendToRenderAckMs'] for row in warm]
        result['nativeMetrics']['captureToAckMs']={name:percentile(values,p) for name,p in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
        result['scope']='Native capture starts at driver surface acquisition; capture-to-ack includes encode, USB, receiver callback handling and return transit. Not source-to-photon. All PC timestamps share QPC; receiver clocks are not subtracted.'
    result['warmFrameAcknowledgements']=len(warm)
    result['invalidReceiverTimestamps']=sum(row['receiveToRenderMs']<0 or row['renderCallbackLagMs']<0 for row in frames)
    for key in ('sendToRenderAckMs','receiveToCallbackMs','receiveToRenderMs','renderCallbackLagMs'):
        values=[row[key] for row in warm if row[key]>=0 and (key in ('sendToRenderAckMs','receiveToCallbackMs') or (row['receiveToRenderMs']>=0 and row['renderCallbackLagMs']>=0))]
        result['metrics'][key]={name:percentile(values,p) for name,p in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
    sends=[row for row in rows if row.get('type')=='send']
    if sends:
        start=sends[0]['sendQpc']
        selected=[row for row in sends if (row['sendQpc']-start)/row['qpcFrequency']>=5]
        by_sequence={row['sequence']:row for row in selected}
        result['senderMetrics']={}
        for key in ('readyToSendMs','writeMs'):
            values=[row[key] for row in selected]
            result['senderMetrics'][key]={name:percentile(values,p) for name,p in [('p50',.5),('p95',.95),('max',1)]}
        values=[by_sequence[row['sequence']]['readyToSendMs']+row['sendToRenderAckMs'] for row in frames if row['sequence'] in by_sequence]
        result['senderMetrics']['packetReadyToAckMs']={name:percentile(values,p) for name,p in [('p50',.5),('p95',.95),('max',1)]}
        result['senderScope']='Five actual PC-clock seconds warmup. Ready includes publisher wait and queue residence; write is local socket acceptance, not USB delivery. Packet-ready excludes capture, encoding and unread pipe backlog; ack is not photon timing.'
    return result

if __name__=='__main__':
    print(json.dumps(analyze(sys.argv[1]),ensure_ascii=False,indent=2))
