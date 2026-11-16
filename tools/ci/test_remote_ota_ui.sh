#!/usr/bin/env bash
# Actual Vue and browser, loopback-only HTTP boundary fixture.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus="${NEXUS_CORE_DIR:-${root}/../nexus-core}"
npm --prefix "$nexus/frontend" run check
npm --prefix "$nexus/frontend" run build
manifest_file="$(mktemp -t pet-ota-ui-manifest).json"
export PET_OTA_UI_MANIFEST="$manifest_file"
python3 - "$manifest_file" <<'PYFIXTURE'
import base64,json,sys
from pathlib import Path
payload=dict(format=1,project="nexus_pet",board="dodo-s3-lcd",version="0.4.1",modelSha256="none",partitionSha256="a"*64,imageSha256="c"*64,image="firmware.bin",imageSize=1024,releaseSequence=10,configSchema=1,growthSchema=4)
Path(sys.argv[1]).write_text(json.dumps(dict(payload=base64.b64encode(json.dumps(payload).encode()).decode(),signature=base64.b64encode(bytes(384)).decode())))
PYFIXTURE
python3 "$root/tools/ci/management_ui_fixture.py" --frontend "$nexus/target/frontend" --port 18801 > /tmp/pet-ota-ui-check.log 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true; rm -f "$manifest_file" "${manifest_file%.json}"' EXIT
for attempt in {1..50}; do
  if ! kill -0 "$fixture_pid" 2>/dev/null; then cat /tmp/pet-ota-ui-check.log; exit 1; fi
  if rg -q '^UI boundary fixture ready$' /tmp/pet-ota-ui-check.log; then break; fi
  sleep 0.1
done
rg -q '^UI boundary fixture ready$' /tmp/pet-ota-ui-check.log
ego-browser nodejs <<'JS'
const name='NEXUS remote OTA regression';
const existing=(await listTaskSpaces()).find(space=>space.name===name);
if(existing&&existing.ownership!=='agent')throw new Error('Existing space needs user confirmation before control');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const base='http://127.0.0.1:18801',checks=[];
async function control(body){return JSON.parse(await serverFetch(base+'/__qa/state',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)}));}
async function state(){return JSON.parse(await serverFetch(base+'/__qa/state',{headers:{'X-QA-Key':'isolated-ui-fixture-control'}}));}
async function expect(expression,label){for(let i=0;i<60;i++){if(await js(expression)){checks.push(label);return;}await wait(0.1);}throw new Error(label);}
async function tap(label){for(let i=0;i<60;i++){const ok=await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`);if(ok)return;await wait(0.1);}throw new Error('Unavailable '+label);}
async function selectRelease(){await js("(()=>{const el=document.querySelector('#remote-ota-panel select');el.value='release-ui';el.dispatchEvent(new Event('change',{bubbles:true}));})()");}
await control({reset:true,otaReset:true});await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');

await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await click('.login-card button.primary');await waitForElement('.sidebar');await click('.sidebar nav button[data-tab=devices]');await tap('固件升级');
await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('远程升级尚未启用') && !document.querySelector('#remote-ota-panel select')",'disabled OTA has no action controls');
await control({otaMode:'empty'});await tap('刷新状态');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('尚未收到设备升级状态') && document.querySelector('#remote-ota-panel select')?.disabled",'missing readback disables installation');
await control({otaMode:'no_releases'});await tap('刷新状态');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('没有匹配设备身份')",'empty compatible catalog is explained');
await control({otaMode:'stale'});await tap('刷新状态');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('状态已过期') && document.querySelector('#remote-ota-panel select').disabled",'stale readback disables actions');
await control({otaMode:'unavailable'});await tap('刷新状态');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('设备正在忙碌') && document.querySelector('#remote-ota-panel select').disabled",'device unavailable keeps actions disabled');
await control({otaMode:'fresh'});await tap('刷新状态');await expect("!document.querySelector('#remote-ota-panel select').disabled",'fresh device permits version selection');
await tap('登记版本');await waitForElement('#remote-ota-panel input[type=file]');
await fillInput('#remote-ota-panel input[pattern]','release-ui');await uploadFile('#remote-ota-panel input[type=file]',(await state()).otaManifestPath);
await expect("[...document.querySelectorAll('#remote-ota-panel button')].some(b=>b.innerText==='登记'&&!b.disabled)",'manifest file enables registration');await tap('登记');
await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('版本已登记') && !document.querySelector('#remote-ota-panel input[type=file]')",'manifest registration closes after acknowledgment');
await control({otaReset:true,otaMode:'fresh'});await tap('刷新状态');await selectRelease();
await control({otaLastError:'ota_boot_changed'});await tap('安装版本');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('设备已经重新启动')",'explicit conflict is shown');
await control({otaLastError:'',otaDrop:true});await tap('安装版本');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('提交结果未确认') && document.querySelector('#remote-ota-panel select').disabled",'unknown outcome retains request and blocks another install');
await tap('重试同一请求');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('等待下发')",'retry creates visible request record');
let observed=await state();if(observed.otaSubmissions.length!==3||observed.otaSubmissions[1].requestId!==observed.otaSubmissions[2].requestId)throw new Error('Unknown retry changed request ID');checks.push('unknown retry preserves exact request ID');
await control({otaStage:true});await tap('刷新状态');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('安装完成，等待确认重启')",'staging does not automatically reboot');
await tap('重启设备…');await expect("[...document.querySelectorAll('#remote-ota-panel button')].find(b=>b.innerText==='确认重启')?.disabled",'reboot needs explicit checkbox');
await control({otaBoot:'2'.repeat(32)});await tap('刷新状态');await expect("!document.querySelector('#remote-ota-panel input[type=checkbox]')",'boot change invalidates open confirmation');
await tap('重启设备…');await waitForElement('#remote-ota-panel input[type=checkbox]');await js("document.querySelector('#remote-ota-panel input[type=checkbox]').scrollIntoView({block:'center'})");await click('#remote-ota-panel input[type=checkbox]');await expect("[...document.querySelectorAll('#remote-ota-panel button')].some(b=>b.innerText==='确认重启' && !b.disabled)",'checkbox enables reboot');await tap('确认重启');await expect("document.querySelector('#remote-ota-panel')?.innerText.includes('等待重启结果')",'confirmed reboot waits for device result');
observed=await state();if(observed.otaSubmissions.length!==4||observed.otaSubmissions[3].confirm!=='REBOOT_DEVICE'||observed.otaSubmissions[3].bootId!=='2'.repeat(32))throw new Error('Reboot used stale confirmation');checks.push('reboot binds explicit current boot');
await tap('查看操作记录');await expect("document.querySelector('#remote-ota-panel .audit-list')?.innerText.includes('提交安装')",'audit renders readable events');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await expect("window.innerWidth===390 && document.documentElement.scrollWidth<=390",'OTA fits 390 CSS px');await cdp('Emulation.clearDeviceMetricsOverride');
await js("document.querySelector('#remote-ota-panel .record-heading .management-actions button:last-child').click()");await expect("!document.querySelector('#remote-ota-panel') && document.activeElement?.id==='device-ota-dodo-ui-fixture'",'close returns keyboard focus to device action');
await tap('退出登录');await expect("!!document.querySelector('.login-card') && document.querySelector('.token-label input').value===''",'logout clears temporary token');
cliLog({passed:checks.length,checks,scope:'actual Vue/browser with isolated HTTP fixture; not CORE, broker, Flash or hardware evidence'});
JS
# Close the agent-owned space after observing successful assertions.
ego-browser nodejs <<'JS'
cliLog(await completeTaskSpace('NEXUS remote OTA regression',{keep:false}));
JS
