const fs=require('node:fs');
function analyze(record){
 const all=record.samples||[];
 const rows=all.filter(s=>s.role==='receiver'&&Number.isFinite(s.fps));
 if(!rows.length)return {qualified:false,reason:'没有有效接收端数据。请导出接收页记录。'};
 const times=rows.map(s=>Date.parse(s.time));
 const duration=(times.at(-1)-times[0])/1000;
 const gaps=times.slice(1).filter((t,i)=>t-times[i]>2500||t<=times[i]).length;
 const correct=rows.filter(s=>s.width===1920&&s.height===1080).length;
 const good=rows.filter(s=>s.width===1920&&s.height===1080&&s.fps>=57).length;
 const sorted=rows.map(s=>s.fps).sort((a,b)=>a-b);
 return {qualified:duration>=300&&gaps===0&&correct===rows.length&&good/rows.length>=.95,
   scope:'仅统计接收解码数据；不能证明 USB 路由、实际呈现帧率或端到端延迟。',
   criterion:'至少 300 秒连续动态画面；全部 1920×1080；95% 以上采样 ≥57 fps；无超过 2.5 秒的采样间断。',
   seconds:duration,samples:rows.length,averageFps:rows.reduce((n,s)=>n+s.fps,0)/rows.length,
   p5Fps:sorted[Math.floor((sorted.length-1)*.05)],minFps:sorted[0],resolution1080pRatio:correct/rows.length,near60Ratio:good/rows.length,gaps};
}
if(require.main===module){
 try{if(!process.argv[2])throw new Error('用法：node scripts/analyze.cjs 接收端记录.json');console.log(JSON.stringify(analyze(JSON.parse(fs.readFileSync(process.argv[2],'utf8'))),null,2));}
 catch(e){console.error(e.message);process.exitCode=1;}
}
module.exports={analyze};
