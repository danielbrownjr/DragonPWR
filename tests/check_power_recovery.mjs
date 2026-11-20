// Regression checks for DragonPWR's product-owned maintenance surface.
// Uses only Node built-ins and never contacts a device.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const html=readFileSync(new URL('../components/dp_portal/power.html',import.meta.url),'utf8');
const js=[...html.matchAll(/<script(?:[^>]*)>([\s\S]*?)<\/script>/g)].at(-1)?.[1];
assert.ok(js,'power page controller is present');
new Function(js);

async function scenario({v2=false}={}){
  const nodes=new Map(),listeners=new Map(),timers=[];
  const doc={getElementById(id){
    if(!nodes.has(id))nodes.set(id,{textContent:'',className:'',disabled:true,addEventListener(type,fn){listeners.set(id+':'+type,fn)}});
    return nodes.get(id);
  }};
  const counts={info:0,state:0,post:0},posted=[];
  let boot='boot-one',mains=v2?null:false;
  const capabilities=v2?['polling']:['polling','power_switch','usb_switch'];
  const mockFetch=async(path,opts={})=>{
    if(path==='/api/v2/state'){counts.state++;return {ok:true,json:async()=>({boot_id:boot,outputs:{mains,usb1:v2?null:false},heap:{free:10000}})}}
    if(path==='/api/v2/info'){counts.info++;return {ok:true,json:async()=>({boot_id:boot,firmware:'test',device_id:'device',capabilities,
      inactive_slot:{label:'ota_1',project:'dragonpwr',version:'old'}})}}
    if(path==='/api/v2/restart'||path==='/api/v2/boot-inactive'){
      counts.post++;posted.push({path,opts});return {ok:true,status:200,json:async()=>({ok:true})};
    }
    throw Error('unexpected route '+path);
  };
  new Function('document','localStorage','fetch','setTimeout','prompt','confirm',js)(
    doc,{getItem:()=>null,setItem(){},removeItem(){}},mockFetch,fn=>timers.push(fn),()=>null,()=>true);
  async function settle(){for(let i=0;i<20;i++)await Promise.resolve()}
  await settle();
  const node=id=>nodes.get(id);
  assert.equal(counts.info,1,'initial info read');
  assert.equal(counts.state,1,'initial state read');
  assert.equal(node('restart-button').disabled,v2,'restart capability gate');
  assert.equal(node('boot-inactive-button').disabled,v2,'slot capability gate');
  assert.equal(node('mains-button').disabled,v2,'output capability gate');

  timers.shift()();await settle();
  assert.equal(counts.info,1,'normal poll must not repeat info');
  assert.equal(counts.state,2,'state continues polling');

  if(!v2){
    listeners.get('restart-button:click')();
    await settle();
    assert.equal(counts.post,1,'restart sent');
    assert.equal(posted[0].path,'/api/v2/restart');
    assert.equal(posted[0].opts.headers['X-Dragon-Auth'],'web','existing token header preserved');
    assert.equal(node('restart-button').disabled,true,'request revokes controls');

    boot='boot-two';
    timers.shift()();await settle();
    assert.equal(counts.info,2,'fresh info on new boot');
    assert.equal(node('boot-id').textContent,'boot-two');
    assert.equal(node('restart-button').disabled,false,'safe controls restored after validated refresh');
    mains=true;
    timers.shift()();await settle();
    assert.equal(node('restart-button').disabled,true,'mains on blocks restart');
    assert.equal(node('boot-inactive-button').disabled,true,'mains on blocks slot switch');
  }else{
    listeners.get('restart-button:click')();
    listeners.get('boot-inactive-button:click')();
    await settle();
    assert.equal(counts.post,0,'V2 cannot send maintenance from unknown mains state');
    assert.equal(node('mains-text').textContent,'Unavailable');
  }
}
await scenario();
await scenario({v2:true});
console.log('DragonPWR power/recovery UI regressions: PASS');
