const {test}=require('node:test');const assert=require('node:assert/strict');const {analyze}=require('../scripts/analyze.cjs');
const fixture=()=>({samples:Array.from({length:301},(_,i)=>({role:'receiver',time:new Date(1700000000000+i*1000).toISOString(),width:1920,height:1080,fps:60}))});
test('benchmark requires full duration, correct resolution and sustained frame rate',()=>{
 assert.equal(analyze(fixture()).qualified,true);
 const short=fixture();short.samples.pop();assert.equal(analyze(short).qualified,false);
 const slow=fixture();slow.samples.forEach(s=>s.fps=39);assert.equal(analyze(slow).qualified,false);
 const scaled=fixture();scaled.samples[0].width=1280;assert.equal(analyze(scaled).qualified,false);
 const gap=fixture();gap.samples.splice(100,3);assert.equal(analyze(gap).qualified,false);
 const sender=fixture();sender.samples.forEach(s=>s.role='sender');assert.equal(analyze(sender).qualified,false);
});
