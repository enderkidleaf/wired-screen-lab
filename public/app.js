'use strict';
const $ = id => document.getElementById(id);
const host = document.body.dataset.role === 'host';
let session, pc, stream, channel, timer, polling = false, generation = 0, cursor = 0;
let pendingIce = [], history = [], previous = null, remote = null, demoFrame = 0, lastRemoteAt = 0;
const log = text => { $('log').textContent = `${new Date().toLocaleTimeString()} ${text}\n` + $('log').textContent.slice(0,3500); };
const status = text => $('status').textContent = text;
async function api(route, body) {
  const response = await fetch(`/api/${route}`, {
    method:body===undefined?'GET':'POST',
    headers:{'Content-Type':'application/json', ...(session?{'X-Session-Id':session.id,Authorization:`Bearer ${session.key}`}:{})},
    body:body===undefined?undefined:JSON.stringify(body), signal:AbortSignal.timeout(8000)
  });
  const data=await response.json();
  if(!response.ok) throw new Error(data.error||`HTTP ${response.status}`);
  return data;
}
function metrics(items) {
  $('metrics').replaceChildren(...items.map(([label,value])=>{
    const box=document.createElement('div'); box.className='metric';
    const name=document.createElement('span'); name.textContent=label;
    const number=document.createElement('strong'); number.textContent=value;
    box.append(name,number); return box;
  }));
}
const number = (n, suffix='') => Number.isFinite(n)?`${n.toFixed(1)}${suffix}`:'—';
function cleanup() {
  generation++; polling=false; clearInterval(timer); timer=null;
  cancelAnimationFrame(demoFrame);
  if(pc) {pc.onconnectionstatechange=null;pc.onicecandidate=null;pc.close();} pc=null;
  if(stream) {stream.getTracks().forEach(t=>{t.onended=null;t.stop();});} stream=null;
  $('video').srcObject=null; channel=null; pendingIce=[];previous=null;remote=null;cursor=0;lastRemoteAt=0;
}
async function stop() {
  const old=session; cleanup();
  if(old && host) {try{await api('stop',{});}catch(e){log(e.message);}}
  session=null;status('已停止');
  if(host){$('start').disabled=false;$('demo').disabled=false;$('stop').disabled=true;$('links').hidden=true;}
}
function makePeer() {
  const peer=new RTCPeerConnection({iceServers:[],bundlePolicy:'max-bundle'}); pc=peer;
  peer.onicecandidate=event=>{if(event.candidate) api('signal',{type:'ice',candidate:event.candidate.toJSON()}).catch(e=>log(e.message));};
  peer.onconnectionstatechange=()=>{
    if(peer!==pc)return;
    const names={new:'准备连接',connecting:'正在建立画面连接',connected:'画面已连接',disconnected:'连接中断，等待恢复',failed:'连接失败，请在电脑停止后重试',closed:'已关闭'};
    status(names[peer.connectionState]);
    if(peer.connectionState==='connected') log('已建立本地 WebRTC 连接；请确认实际媒体走 USB 网卡。');
  };
  peer.ontrack=event=>{$('video').srcObject=event.streams[0]||new MediaStream([event.track]);$('video').play().catch(()=>log('请点击画面开始播放'));};
  peer.ondatachannel=event=>wireChannel(event.channel);
  if(host) wireChannel(peer.createDataChannel('measurements',{ordered:false,maxRetransmits:0}));
  timer=setInterval(()=>collectStats().catch(e=>log(e.message)),1000);
  return peer;
}
function wireChannel(value){channel=value;channel.onmessage=event=>{try{const data=JSON.parse(event.data);if(data.type==='stats'){remote=data.stats;lastRemoteAt=Date.now();}}catch{}};}
async function processMessage(data) {
  if(data.type==='bye') {cleanup();status('对方已停止，请重新开始会话');return;}
  if(data.type==='ready'&&host){
    const offer=await pc.createOffer();await pc.setLocalDescription(offer);
    await api('signal',{type:'offer',sdp:offer.sdp});
  } else if(data.type==='offer'&&!host){
    await pc.setRemoteDescription(data); await flushIce();
    const answer=await pc.createAnswer();await pc.setLocalDescription(answer);
    await api('signal',{type:'answer',sdp:answer.sdp});
  } else if(data.type==='answer'&&host){await pc.setRemoteDescription(data);await flushIce();}
  else if(data.type==='ice'){if(pc.remoteDescription)await pc.addIceCandidate(data.candidate);else pendingIce.push(data.candidate);}
}
async function flushIce(){for(const ice of pendingIce)await pc.addIceCandidate(ice);pendingIce=[];}
async function poll(){
  const run=generation;polling=true;
  while(polling && run===generation){
    try{
      const result=await api(`messages?after=${cursor}`);
      if(run!==generation)return;
      for(const message of result.messages) {await processMessage(message.data);if(run!==generation)return;}
      cursor=result.cursor;
    }catch(e){if(run!==generation)return;log(e.message);if(/会话已结束|认证失败/.test(e.message)){cleanup();status('会话结束，请重新配对');return;}}
    await new Promise(resolve=>setTimeout(resolve,400));
  }
}
function demoStream(){
  const canvas=document.createElement('canvas');canvas.width=1920;canvas.height=1080;
  const ctx=canvas.getContext('2d');let count=0;
  function draw(t){
    ctx.fillStyle='#0a1422';ctx.fillRect(0,0,1920,1080);
    for(let x=0;x<1920;x+=80){ctx.strokeStyle='#233b52';ctx.strokeRect(x,0,80,1080);}
    ctx.fillStyle='#91e5c2';ctx.fillRect((t*.5)%1920,0,30,1080);
    ctx.font='64px sans-serif';ctx.fillText('WIRED SCREEN LAB · 1080p60',100,180);
    ctx.font='42px monospace';ctx.fillText(`Frame ${count++} / ${new Date().toISOString()}`,100,280);
    ctx.fillStyle='#e8edf6';ctx.font='30px sans-serif';ctx.fillText('动态测试源 — 实际帧率请查看接收端',100,380);
    demoFrame=requestAnimationFrame(draw);
  }draw(performance.now());return canvas.captureStream(60);
}
async function start(demo=false){
  if(session)await stop();
  history=[];$('start').disabled=true;$('demo').disabled=true;status('准备画面');
  try{
    if(!window.isSecureContext)throw new Error('请在电脑使用 http://localhost:3210 打开控制页');
    stream=demo?demoStream():await navigator.mediaDevices.getDisplayMedia({video:{width:{ideal:1920,max:1920},height:{ideal:1080,max:1080},frameRate:{ideal:60,max:60}},audio:false});
    const track=stream.getVideoTracks()[0];track.contentHint=$('hint').value;
    track.onended=()=>stop();
    const settings=track.getSettings();log(`捕获设置：${settings.width} × ${settings.height}，请求帧率 ${settings.frameRate??60}；此值不是接收实测。`);
    session=await api('session',{});
    const peer=makePeer();const sender=peer.addTrack(track,stream);
    const codec=$('codec').value;
    const transceiver=peer.getTransceivers().find(t=>t.sender===sender);
    if(codec!=='auto'&&transceiver?.setCodecPreferences){
      const codecs=RTCRtpSender.getCapabilities('video').codecs;
      transceiver.setCodecPreferences([...codecs.filter(c=>c.mimeType===`video/${codec}`),...codecs.filter(c=>c.mimeType!==`video/${codec}`)]);
    }
    const p=sender.getParameters();if(!p.encodings?.length)p.encodings=[{}];
    p.encodings[0].maxBitrate=Number($('bitrate').value)*1000000;p.encodings[0].maxFramerate=60;p.degradationPreference='maintain-resolution';
    await sender.setParameters(p);
    $('video').srcObject=stream;
    $('addresses').replaceChildren(...session.addresses.map(address=>{
      const paragraph=document.createElement('p');const label=document.createElement('span');label.textContent=`${address.name}： `;
      const a=document.createElement('a');a.href=address.url.split('#')[0];a.textContent=a.href;paragraph.append(label,a);return paragraph;
    }));
    if(!session.addresses.length)$('addresses').textContent='未找到外部 IPv4 地址；开启 USB 网络共享后重新开始。';
    $('code').textContent=session.code;$('local').href=session.localViewer;$('links').hidden=false;$('stop').disabled=false;$('export').disabled=false;
    status('等待平板打开配对链接');poll();
  }catch(e){log(e.message);await stop();status('未开始：'+e.message);}
}
async function connect(){
  $('connect').disabled=true;
  try{
    const [id,key]=location.hash.slice(1).split('.');
    if(id&&key){session={id,key};const joined=await api('join',{});session.key=joined.key;}
    else {session=null;const code=$('paircode').value.trim();if(!/^\d{8}$/.test(code))throw new Error('请输入电脑显示的 8 位配对码');session=await api('pair',{code});}
    history=[];makePeer();poll();status('等待电脑画面');$('export').disabled=false;
  }catch(e){log(e.message);status('连接未完成');$('connect').disabled=false;}
}
async function collectStats(){
  const peer=pc;if(!peer)return;
  const reports=await peer.getStats();if(peer!==pc)return;
  let videoReport,pair,codec,source;
  reports.forEach(r=>{if(r.type===(host?'outbound-rtp':'inbound-rtp')&&r.kind==='video'&&!r.isRemote)videoReport=r;if(r.type==='transport'&&r.selectedCandidatePairId)pair=reports.get(r.selectedCandidatePairId);if(r.type==='media-source'&&r.kind==='video')source=r;});
  if(!videoReport)return;
  codec=reports.get(videoReport.codecId)?.mimeType;
  const r=videoReport,seconds=previous?(r.timestamp-previous.timestamp)/1000:0;
  const count=host?r.framesEncoded:r.framesDecoded,bytes=host?r.bytesSent:r.bytesReceived;
  const measuredFps=previous&&seconds>0?(count-previous.count)/seconds:null;
  const mbps=previous&&seconds>0?(bytes-previous.bytes)*8/seconds/1e6:null;
  const sample={time:new Date().toISOString(),role:host?'sender':'receiver',width:r.frameWidth??null,height:r.frameHeight??null,fps:measuredFps,mbps,codec:codec??null,framesDropped:r.framesDropped??null,qualityLimitation:r.qualityLimitationReason??null,rttMs:pair?.currentRoundTripTime!=null?pair.currentRoundTripTime*1000:null};
  sample.sourceFps=source?.framesPerSecond??null;
  const processing=host?r.totalEncodeTime:r.totalDecodeTime;
  sample.processingMs=previous&&count>previous.count&&Number.isFinite(processing)?(processing-previous.processing)*1000/(count-previous.count):null;
  const localCandidate=pair?reports.get(pair.localCandidateId):null,remoteCandidate=pair?reports.get(pair.remoteCandidateId):null;
  sample.route={local:localCandidate?.address??null,remote:remoteCandidate?.address??null,protocol:localCandidate?.protocol??null};
  previous={timestamp:r.timestamp,count,bytes,processing};
  history.push(sample);if(history.length>1800)history.shift();
  if(!host&&channel?.readyState==='open')channel.send(JSON.stringify({type:'stats',stats:sample}));
  const recv=host?(Date.now()-lastRemoteAt<3500?remote:null):sample;
  const dims=recv?.width&&recv?.height?`${recv.width} × ${recv.height}`:'—';
  metrics([['接收分辨率',dims],[host?'发送 / 接收 fps':'接收解码 fps',host?`${number(sample.fps)} / ${number(recv?.fps)}`:number(sample.fps)],['视频码率',number(sample.mbps,' Mbps')],['网络往返（非显示延迟）',number(sample.rttMs,' ms')]]);
  const meets=recv?.width===1920&&recv?.height===1080&&recv?.fps>=57;
  $('quality').textContent=meets?'当前采样接近 1080p60；仍需连续 5 分钟动态画面验证。':`目标 1920 × 1080 / 60 fps · ${recv?'当前采样未达到目标；静止桌面可能主动降低帧率。':'等待接收端真实测量。'}`;
  $('route').textContent=`编码：${codec||'—'} · ${host?`源帧率：${number(sample.sourceFps)} · 编码限制：${sample.qualityLimitation||'—'}`:'接收端'} · 平均${host?'编码':'解码'}耗时：${number(sample.processingMs,' ms')} · 媒体路径：${sample.route.local||'浏览器隐藏地址'} → ${sample.route.remote||'浏览器隐藏地址'} (${sample.route.protocol||'—'}) · 仅有 HTTP 配对走 USB 不足以证明视频走 USB；实机测试需关闭平板 Wi-Fi。`;
}
$('export').onclick=()=>{
  const data={target:{width:1920,height:1080,fps:60},note:'本文件为采样记录，不是达标证书。RTT 不是端到端显示延迟。',userAgent:navigator.userAgent,samples:history};
  const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));
  const a=document.createElement('a');a.href=url;a.download=`wired-screen-${host?'sender':'receiver'}-${Date.now()}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
};
$('video').onclick=()=>$('video').play();
if(host){$('start').onclick=()=>start();$('demo').onclick=()=>start(true);$('stop').onclick=()=>stop();}
else{$('connect').onclick=connect;$('fullscreen').onclick=()=>$('video').requestFullscreen().catch(e=>log(e.message));}
window.addEventListener('beforeunload',()=>{if(session){fetch('/api/'+(host?'stop':'signal'),{method:'POST',headers:{'Content-Type':'application/json','X-Session-Id':session.id,Authorization:`Bearer ${session.key}`},body:JSON.stringify(host?{}:{type:'bye'}),keepalive:true}).catch(()=>{});}});
