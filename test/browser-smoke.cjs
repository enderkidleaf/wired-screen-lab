// Run with NODE_PATH pointing to an installed Playwright package.
const {chromium}=require('playwright');
const {createServer}=require('../server.cjs');
const fs=require('node:fs');
const assert=require('node:assert/strict');
(async()=>{
 const server=createServer();await new Promise(r=>server.listen(0,'127.0.0.1',r));
 let browser;
 try{
  browser=await chromium.launch({channel:'msedge',headless:true,args:['--disable-background-timer-throttling','--disable-renderer-backgrounding','--disable-backgrounding-occluded-windows']});
  const context=await browser.newContext({viewport:{width:1440,height:1100}});
  const errors=[];context.on('page',p=>p.on('pageerror',e=>errors.push(e.message)));
  const sender=await context.newPage();await sender.goto(`http://localhost:${server.address().port}`);
  await sender.getByRole('button',{name:'发送 1080p 动态测试画面'}).click();
  await sender.locator('#links').waitFor({state:'visible'});
  const url=await sender.locator('#local').getAttribute('href');
  const viewer=await context.newPage();await viewer.goto(url.split('#')[0]);await viewer.locator('#paircode').fill(await sender.locator('#code').textContent());await viewer.getByRole('button',{name:'连接电脑',exact:true}).click();
  try { await viewer.waitForFunction(()=>document.querySelector('video').videoWidth===1920,{},{timeout:15000}); }
  catch(e){console.log(JSON.stringify({sender:await sender.evaluate(()=>({log:$('log').textContent,state:pc?.connectionState,ice:pc?.iceConnectionState,sdp:pc?.localDescription?.sdp})),viewer:await viewer.evaluate(()=>({log:$('log').textContent,state:pc?.connectionState,ice:pc?.iceConnectionState,sdp:pc?.localDescription?.sdp})),errors},null,2));throw e;}
  await viewer.waitForFunction(()=>document.querySelector('#quality').textContent.includes('当前采样'),{},{timeout:15000});
  await new Promise(r=>setTimeout(r,Number(process.env.BENCHMARK_SECONDS||10)*1000));
  const sample=await viewer.evaluate(()=>({width:document.querySelector('video').videoWidth,height:document.querySelector('video').videoHeight,metrics:document.querySelector('#metrics').innerText,status:document.querySelector('#status').textContent,route:document.querySelector('#route').textContent}));
  assert.equal(sample.width,1920);assert.equal(sample.height,1080);assert.equal(sample.status,'画面已连接');assert.deepEqual(errors,[]);
  fs.mkdirSync('artifacts',{recursive:true});
  await sender.screenshot({path:'artifacts/sender.png',fullPage:true});await viewer.screenshot({path:'artifacts/viewer.png',fullPage:true});
  const downloadEvent=viewer.waitForEvent('download');await viewer.locator('#export').click();const download=await downloadEvent;await download.saveAs('artifacts/loopback-receiver.json');
  const senderDownloadEvent=sender.waitForEvent('download');await sender.locator('#export').click();await (await senderDownloadEvent).saveAs('artifacts/loopback-sender.json');
  await sender.locator('#stop').click();await viewer.waitForFunction(()=>document.querySelector('#status').textContent.includes('会话结束'),{},{timeout:12000});
  console.log(JSON.stringify({result:'PASS',type:'desktop loopback only; not USB or extended desktop',sample,errors},null,2));
 }finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);process.exitCode=1;});
