// Regression tests execute the actual product-owned browser controller.
// Dependency-free and device-free; run with node tests/check_power_recovery.mjs.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const html=readFileSync(new URL('../components/dp_portal/power.html',import.meta.url),'utf8');
const js=[...html.matchAll(/<script(?:[^>]*)>([\s\S]*?)<\/script>/g)].at(-1)?.[1];
assert.ok(js,'embedded controller exists');
new Function(js);

function harness({v2=false,noSlot=false}={}){
  const nodes=new Map(),events=new Map(),timers=[],posts=[];
  const counts={info:0,state:0};
  let boot='A',infoBoot=null,mains=v2?null:false,slot=!noSlot;
  let confirmHandler=()=>true,promptHandler=()=>null,responseHandler=()=>({status:200,ok:true,body:{ok:true}});
  let holdInfo=null;
  const doc={getElementById(id){
    if(!nodes.has(id))nodes.set(id,{textContent:'',className:'',disabled:true,addEventListener(t,cb){events.set(id+':'+t,cb)}});
    return nodes.get(id);
  }};
  const fetch=async(path,opts={})=>{
    if(path==='/api/v2/state'){
      counts.state++;
      return {ok:true,json:async()=>({boot_id:boot,outputs:{mains,usb1:v2?null:false},heap:{free:10240}})};
    }
    if(path==='/api/v2/info'){
      counts.info++;
      if(holdInfo)return await holdInfo;
      const returnedBoot=infoBoot??boot;
      return {ok:true,json:async()=>({boot_id:returnedBoot,firmware:'test',device_id:'device',
        capabilities:v2?['polling']:['polling','power_switch','usb_switch'],
        ...(slot?{inactive_slot:{label:'ota_1',project:'dragonpwr',version:'1.0'}}:{})})};
    }
    if(path==='/api/v2/restart'||path==='/api/v2/boot-inactive'){
      posts.push({path,opts,boot});
      const res=responseHandler(path,opts);
      return {status:res.status,ok:res.ok,json:async()=>res.body};
    }
    throw Error('unexpected endpoint '+path);
  };
  const stored=new Map();
  const localStorage={getItem:key=>stored.get(key)||null,setItem(key,value){stored.set(key,value)},removeItem(key){stored.delete(key)}};
  new Function('document','localStorage','fetch','setTimeout','prompt','confirm',js)(
    doc,localStorage,fetch,fn=>timers.push(fn),()=>promptHandler(),()=>confirmHandler());
  const node=id=>nodes.get(id);
  return {
    node,counts,posts,
    setBoot:b=>{boot=b},setInfoBoot:b=>{infoBoot=b},setMains:b=>{mains=b},setSlot:b=>{slot=b},
    onConfirm:fn=>{confirmHandler=fn},onPrompt:fn=>{promptHandler=fn},onResponse:fn=>{responseHandler=fn},
    holdNextInfo:promise=>{holdInfo=promise},clearHold:()=>{holdInfo=null},
    click:id=>events.get(id+':click')(),
    async settle(){for(let n=0;n<50;n++)await Promise.resolve()},
    async poll(){assert.ok(timers.length,'next poll scheduled');timers.shift()();await this.settle()}
  };
}

async function testNormalAndV2(){
  const h=harness();await h.settle();
  assert.equal(h.counts.info,1);
  assert.equal(h.counts.state,1);
  assert.equal(h.node('restart-button').disabled,false);
  await h.poll();assert.equal(h.counts.info,1,'steady-state poll must not refetch info');

  // Boot change without an intervening command must revoke and refresh capabilities.
  h.setBoot('B');await h.poll();
  assert.equal(h.counts.info,2,'independent boot change refreshes info');
  assert.equal(h.node('boot-id').textContent,'B');
  h.setMains(true);await h.poll();
  assert.equal(h.node('restart-button').disabled,true,'mains ON blocks maintenance');

  const v=harness({v2:true});await v.settle();
  assert.equal(v.node('restart-button').disabled,true);
  assert.equal(v.node('boot-inactive-button').disabled,true);
  v.click('restart-button');v.click('boot-inactive-button');await v.settle();
  assert.equal(v.posts.length,0,'V2 unknown mains and missing capability never POST');
  assert.equal(v.node('mains-text').textContent,'Unavailable');

  const no=harness({noSlot:true});await no.settle();
  assert.equal(no.node('boot-inactive-button').disabled,true);
  no.click('boot-inactive-button');await no.settle();
  assert.equal(no.posts.length,0,'absent slot must prevent POST');
}

async function testDialogsAndConcurrency(){
  const h=harness();await h.settle();
  let promptSeen='';
  h.onConfirm(()=>{promptSeen=h.node('inactive-slot').textContent;return true});
  h.click('boot-inactive-button');
  assert.equal(h.node('mains-button').disabled,true,'maintenance locks output buttons');
  h.click('usb1-button');await h.settle();
  assert.equal(h.posts.length,1);
  assert.equal(h.posts[0].path,'/api/v2/boot-inactive');
  assert.ok(promptSeen.includes('ota_1'),'slot identity presented before switch');
  assert.equal(h.posts[0].opts.headers['X-Dragon-Auth'],'web');
  assert.equal(h.node('restart-button').disabled,true,'successful action revokes controls');

  const stale=harness();await stale.settle();
  stale.onConfirm(()=>{stale.setBoot('NEW');return true});
  stale.click('boot-inactive-button');await stale.settle();
  assert.equal(stale.posts.length,0,'boot change during confirm must block POST');
  assert.equal(stale.node('restart-button').disabled,true);

  const on=harness();await on.settle();
  on.onConfirm(()=>{on.setMains(true);return true});
  on.click('restart-button');await on.settle();
  assert.equal(on.posts.length,0,'mains ON after confirm must block POST');

  const retry=harness();await retry.settle();
  let requests=0;
  retry.onResponse(()=>{requests++;return requests===1?
    {status:403,ok:false,body:{error:'authorization required'}}:
    {status:200,ok:true,body:{ok:true}}});
  retry.onPrompt(()=> 'new-token');
  retry.click('restart-button');await retry.settle();
  assert.equal(retry.posts.length,2,'token 403 prompts a single retry');
  assert.equal(retry.posts[1].opts.headers['X-Dragon-Auth'],'new-token','prompt token used for retry');
  assert.ok(retry.counts.state>=3,'retry revalidates live state');

  const changeOnPrompt=harness();await changeOnPrompt.settle();
  changeOnPrompt.onResponse(()=>({status:403,ok:false,body:{error:'unauthorized'}}));
  changeOnPrompt.onPrompt(()=>{changeOnPrompt.setBoot('AFTER-PROMPT');return 'token'});
  changeOnPrompt.click('restart-button');await changeOnPrompt.settle();
  assert.equal(changeOnPrompt.posts.length,1,'changing boot at token prompt blocks retry');

  const conflict=harness();await conflict.settle();
  conflict.onResponse(()=>({status:409,ok:false,body:{error:'mains must be off'}}));
  conflict.click('restart-button');await conflict.settle();
  assert.match(conflict.node('maintenance-message').textContent,/mains must be off/);
}

async function testPendingInfoAndMismatch(){
  const h=harness();await h.settle();
  let release;
  const pending=new Promise(resolve=>{release=resolve});
  h.setBoot('B');h.holdNextInfo(pending);
  const p=h.poll();
  await h.settle();
  assert.equal(h.node('restart-button').disabled,true,'pending info revokes controls immediately');
  release({ok:true,json:async()=>({boot_id:'B',firmware:'test',device_id:'device',
    capabilities:['polling','power_switch'],inactive_slot:{label:'ota_1',project:'dragonpwr',version:'1.0'}})});
  await p;
  await h.settle(); // the deferred info response completes after poll()'s initial microtask drain
  assert.equal(h.node('restart-button').disabled,false);

  const mismatch=harness();await mismatch.settle();
  mismatch.setBoot('C');mismatch.setInfoBoot('D');await mismatch.poll();
  assert.equal(mismatch.node('restart-button').disabled,true,'mixed boot snapshots fail closed');
}
await testNormalAndV2();
await testDialogsAndConcurrency();
await testPendingInfoAndMismatch();
console.log('DragonPWR power/recovery UI regressions: PASS');
