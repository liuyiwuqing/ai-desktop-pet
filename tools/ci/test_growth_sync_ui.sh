#!/usr/bin/env bash
# Actual Vue/browser with a loopback HTTP fixture; no CORE or physical device.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus="${NEXUS_CORE_DIR:-${root}/../nexus-core}"
npm --prefix "$nexus/frontend" run check
npm --prefix "$nexus/frontend" run build
python3 "$root/tools/ci/management_ui_fixture.py" --frontend "$nexus/target/frontend" --port 18802 > /tmp/pet-growth-ui-fixture.log 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
  if ! kill -0 "$fixture_pid" 2>/dev/null; then cat /tmp/pet-growth-ui-fixture.log; exit 1; fi
  if rg -q '^UI boundary fixture ready$' /tmp/pet-growth-ui-fixture.log; then break; fi
  sleep 0.1
done
rg -q '^UI boundary fixture ready$' /tmp/pet-growth-ui-fixture.log
ego-browser nodejs <<'JS'
const name='NEXUS growth synchronization regression';
const existing=(await listTaskSpaces()).find(space=>space.name===name);
if(existing&&existing.ownership!=='agent')throw new Error('Existing space needs user confirmation before control');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const base='http://127.0.0.1:18802',checks=[];
async function control(body){return JSON.parse(await serverFetch(base+'/__qa/state',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)}));}
async function expect(expression,label){for(let i=0;i<60;i++){if(await js(expression)){checks.push(label);return;}await wait(0.1);}throw new Error(label);}
async function refresh(mode){await control({growthMode:mode});await js("[...document.querySelectorAll('button')].find(b=>b.innerText==='刷新设备成长').click()");}
await control({reset:true});await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');

await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await click('.login-card button.primary');
await expect("!!document.querySelector('.sidebar')",'authenticated');await click('.sidebar nav button[data-tab=pet]');
const panel="document.querySelector('[aria-labelledby=device-growth-title]')";
await expect(`${panel}?.innerText.includes('同步未启用')`,'disabled feature has an explicit state');
await refresh('empty');await expect(`${panel}?.innerText.includes('暂无已注册设备')`,'no registered devices');
await refresh('waiting');await expect(`${panel}?.innerText.includes('等待同步') && ${panel}?.innerText.includes('尚未收到')`,'missing snapshot does not invent a profile');
await refresh('fresh');await expect(`${panel}?.innerText.includes('已同步') && ${panel}?.innerText.includes('72')`,'fresh snapshot shown separately from CORE');
await expect("document.body.innerText.includes('CORE 专注档案') && document.body.innerText.includes('开始专注')",'original focus entry remains available');
await refresh('stale');await expect(`${panel}?.innerText.includes('数据已过期') && ${panel}?.innerText.includes('72')`,'stale snapshot remains readable');
for(const [mode,text] of [['unsaved','尚未落盘'],['readonly','存档不可写'],['recovered','回退至有效存档'],['time_unavailable','时间未就绪'],['device_disabled','设备已禁用']]) {
  await refresh(mode);await expect(`${panel}?.innerText.includes(${JSON.stringify(text)})`,'visible state '+mode);
}
await refresh('error');await expect(`${panel}?.querySelector('[role=alert]') && ${panel}?.innerText.includes('72')`,'failed refresh preserves the previous snapshot and exposes error');
await refresh('delayed');await expect(`${panel}?.getAttribute('aria-busy')==='true' && [...${panel}.querySelectorAll('button')].every(b=>b.disabled)`,'loading disables duplicate refresh');
await expect(`${panel}?.getAttribute('aria-busy')==='false' && !${panel}?.querySelector('[role=alert]')`,'refresh recovers after failure');
await control({deviceName:'<img src=x onerror=alert(1)>'});await refresh('fresh');
await expect(`${panel}?.innerText.includes('<img src=x onerror=alert(1)>') && !${panel}?.querySelector('img')`,'device names render as text');
await control({deviceName:'书房伙伴'});await refresh('fresh');
await expect(`${panel}?.innerText.includes('书房伙伴')`,'device name readback');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("window.innerWidth===390 && document.documentElement.scrollWidth<=390",'growth view fits 390 CSS pixels');
await cdp('Emulation.clearDeviceMetricsOverride');
cliLog({passed:checks.length,checks,scope:'actual Vue/browser; isolated HTTP fixture, no CORE or device'});
JS
ego-browser nodejs <<'JS'
cliLog(await completeTaskSpace('NEXUS growth synchronization regression',{keep:false}));
JS
