const {test,before,after}=require('node:test');
const assert=require('node:assert/strict');
const {createServer}=require('../server.cjs');
let server,base;
before(async()=>{server=createServer();await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));base=`http://127.0.0.1:${server.address().port}`;});
after(()=>new Promise(resolve=>server.close(resolve)));
async function request(route,{method='POST',body={},id,key,origin}={}){
 const r=await fetch(base+route,{method,headers:{'Content-Type':'application/json',...(id?{'X-Session-Id':id}:{}),...(key?{Authorization:`Bearer ${key}`} :{}),...(origin?{Origin:origin}:{})},body:method==='POST'?JSON.stringify(body):undefined});
 return {status:r.status,data:await r.json()};
}
test('pair, negotiate with isolated queues, and revoke session',async()=>{
 const {data:s}=await request('/api/session');
 const pair=new URL(s.localViewer).hash.slice(1).split('.')[1];
 assert.equal((await request('/api/join',{id:s.id,key:'bad'})).status,403);
 const {data:v}=await request('/api/join',{id:s.id,key:pair});
 assert.ok(v.key);
 assert.equal((await request('/api/join',{id:s.id,key:pair})).status,409);
 const ready=await request('/api/messages',{method:'GET',id:s.id,key:s.key});
 assert.equal(ready.data.messages[0].data.type,'ready');
 assert.equal((await request('/api/signal',{id:s.id,key:v.key,body:{type:'offer',sdp:'bad'}})).status,400);
 await request('/api/signal',{id:s.id,key:s.key,body:{type:'offer',sdp:'test-sdp'}});
 const offers=await request('/api/messages',{method:'GET',id:s.id,key:v.key});
 assert.equal(offers.data.messages.length,1);assert.equal(offers.data.messages[0].data.sdp,'test-sdp');
 const empty=await request('/api/messages?after='+offers.data.cursor,{method:'GET',id:s.id,key:v.key});assert.equal(empty.data.messages.length,0);
 assert.equal((await request('/api/stop',{id:s.id,key:v.key})).status,404);
 assert.equal((await request('/api/stop',{id:s.id,key:s.key})).status,200);
 assert.equal((await request('/api/messages',{method:'GET',id:s.id,key:v.key})).status,404);
});
test('reject foreign-origin creation and serve only allowlisted files',async()=>{
 assert.equal((await request('/api/session',{origin:'https://other.example'})).status,403);
 for(const route of ['/server.cjs','/package.json','/../server.cjs'])assert.equal((await fetch(base+route)).status,404);
 for(const route of ['/','/viewer','/app.js','/style.css'])assert.equal((await fetch(base+route)).status,200);
});
test('invalid JSON and oversized payloads are rejected',async()=>{
 const r=await fetch(base+'/api/session',{method:'POST',body:'{'});assert.equal(r.status,400);
 const large=await fetch(base+'/api/session',{method:'POST',body:'x'.repeat(100001)});assert.equal(large.status,413);
});
test('short code pairs only once and rate limits guessing',async()=>{
 const {data:s}=await request('/api/session');
 const paired=await request('/api/pair',{body:{code:s.code}});assert.equal(paired.status,200);assert.equal(paired.data.id,s.id);
 assert.equal((await request('/api/pair',{body:{code:s.code}})).status,409);
 let result;for(let i=0;i<11;i++)result=await request('/api/pair',{body:{code:'00000000'}});
 assert.equal(result.status,429);
});
