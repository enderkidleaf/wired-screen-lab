const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const token = () => crypto.randomBytes(24).toString('hex');
const loopback = ip => ['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(ip);
const files = new Map([
  ['/', ['index.html', 'text/html']], ['/viewer', ['viewer.html', 'text/html']],
  ['/app.js', ['app.js', 'text/javascript']], ['/style.css', ['style.css', 'text/css']]
]);
function createServer() {
  const rooms = new Map();
  const attempts = new Map();
  const send = (res, status, body) => { res.writeHead(status, {'Content-Type':'application/json'}); res.end(JSON.stringify(body)); };
  const server = http.createServer(async (req, res) => {
    res.setHeader('Cache-Control','no-store');
    res.setHeader('Referrer-Policy','no-referrer');
    res.setHeader('X-Content-Type-Options','nosniff');
    res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; media-src 'self' blob:; frame-ancestors 'none'");
    try {
      const url = new URL(req.url, 'http://localhost');
      if (req.method === 'GET' && files.has(url.pathname)) {
        const [file, mime] = files.get(url.pathname);
        res.writeHead(200, {'Content-Type':`${mime}; charset=utf-8`});
        return res.end(fs.readFileSync(path.join(__dirname, 'public', file)));
      }
      if (!url.pathname.startsWith('/api/')) return send(res,404,{error:'不存在的地址'});
      // Reject browser cross-origin writes; paired clients are same-origin.
      if (req.headers.origin && req.headers.origin !== `http://${req.headers.host}`) return send(res,403,{error:'不允许跨站请求'});
      let body = {};
      if (req.method === 'POST') {
        let text = '';
        for await (const chunk of req) { text += chunk; if (text.length > 100000) return send(res,413,{error:'消息过大'}); }
        try { body = JSON.parse(text || '{}'); } catch { return send(res,400,{error:'无效 JSON'}); }
      }
      const now = Date.now();
      for (const [id, r] of rooms) if (now-r.touched > 600000) rooms.delete(id);
      if (req.method === 'POST' && url.pathname === '/api/session') {
        if (!loopback(req.socket.remoteAddress)) return send(res,403,{error:'请在电脑本机创建会话'});
        if (rooms.size >= 20) return send(res,429,{error:'会话过多，请停止旧会话'});
        const id=token(), host=token(), pair=token();
        let code;do{code=String(crypto.randomInt(10000000,100000000));}while([...rooms.values()].some(r=>r.code===code));
        rooms.set(id,{host,pair,code,viewer:null,messages:[],sequence:0,touched:now});
        const port=server.address().port;
        const addresses=Object.entries(os.networkInterfaces()).flatMap(([name, list]) => list.filter(a=>a.family==='IPv4'&&!a.internal).map(a=>({name,url:`http://${a.address}:${port}/viewer#${id}.${pair}`})));
        return send(res,200,{id,key:host,code,addresses,localViewer:`http://localhost:${port}/viewer#${id}.${pair}`});
      }
      if(req.method==='POST' && url.pathname==='/api/pair') {
        for(const [ip,value] of attempts)if(now-value.start>60000)attempts.delete(ip);
        const ip=req.socket.remoteAddress;
        const rate=attempts.get(ip)||{start:now,count:0};rate.count++;attempts.set(ip,rate);
        if(rate.count>10)return send(res,429,{error:'尝试过多，请一分钟后再试'});
        const found=[...rooms.entries()].find(([,r])=>r.code===body?.code);
        if(!found)return send(res,403,{error:'配对码无效或已过期'});
        const [id,r]=found;if(r.viewer)return send(res,409,{error:'已有设备连接，请电脑重新开始会话'});
        r.viewer=token();r.touched=now;r.messages.push({seq:++r.sequence,to:'host',data:{type:'ready'}});
        return send(res,200,{id,key:r.viewer});
      }
      const id = req.headers['x-session-id'];
      const room = rooms.get(id);
      if (!room) return send(res,404,{error:'会话已结束，请重新连接'});
      const key = req.headers.authorization?.replace(/^Bearer /,'');
      if (req.method==='POST' && url.pathname==='/api/join') {
        if (key!==room.pair) return send(res,403,{error:'配对链接无效'});
        if(room.viewer) return send(res,409,{error:'已有平板连接，请在电脑重新开始会话'});
        room.viewer=token(); room.touched=now;
        room.messages.push({seq:++room.sequence,to:'host',data:{type:'ready'}});
        return send(res,200,{key:room.viewer});
      }
      const role=key===room.host?'host':key===room.viewer?'viewer':null;
      if(!role) return send(res,403,{error:'认证失败'});
      room.touched=now;
      if(req.method==='GET' && url.pathname==='/api/messages') {
        const after=Number(url.searchParams.get('after')||0);
        return send(res,200,{messages:room.messages.filter(m=>m.to===role&&m.seq>after),cursor:room.sequence});
      }
      if(req.method==='POST' && url.pathname==='/api/signal') {
        const valid = body && (
          (body.type==='ice' && body.candidate && typeof body.candidate.candidate==='string') ||
          (body.type===(role==='host'?'offer':'answer') && typeof body.sdp==='string') || body.type==='bye');
        if(!valid) return send(res,400,{error:'无效信令'});
        if(room.messages.length>=500) return send(res,429,{error:'信令过多，请重新开始'});
        room.messages.push({seq:++room.sequence,to:role==='host'?'viewer':'host',data:body});
        return send(res,200,{ok:true});
      }
      if(req.method==='POST' && url.pathname==='/api/stop' && role==='host') {
        rooms.delete(id); return send(res,200,{ok:true});
      }
      return send(res,404,{error:'不存在的操作'});
    } catch(e) { if(!res.headersSent) send(res,500,{error:'服务异常'}); else res.end(); }
  });
  return server;
}
if(require.main===module) {
  const port=Number(process.env.PORT||3210);
  createServer().listen(port,'0.0.0.0',()=>console.log(`Wired Screen Lab: http://localhost:${port}`));
}
module.exports={createServer};
