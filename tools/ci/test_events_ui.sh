#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-events"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/events_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
 if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
 if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
 sleep .1
done
{
cat "$repo_root/tools/ci/naive_ui_helpers.js"
cat <<'EOF'
async function waitForLogin(){for(let i=0;i<120;i++){if(await js("!!document.querySelector('.token-label input:not(:disabled)') && !document.querySelector('.login-card button.primary')?.disabled").catch(()=>false))return;await wait(.1);}throw new Error('Login form did not settle after session recovery');}
const name='NEXUS realtime onboarding',base='http://127.0.0.1:18819';const prior=(await listTaskSpaces()).find(s=>s.name===name);if(prior&&prior.ownership!=='agent')throw new Error('Space requires confirmation');await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});const checks=[];
async function qa(body={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)}));}
async function expect(e,label){for(let i=0;i<100;i++){if(await js(e).catch(()=>false)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label){await expect(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
async function nav(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});await qa({reset:true});await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login');await expect("document.querySelector('.setup-guide')?.textContent.includes('尚无设备')",'empty installation opens guide');await expect("document.querySelector('.setup-guide').textContent.includes('登记不代表') && document.querySelector('.setup-guide').textContent.includes('不代表设备已联网')",'guide distinguishes registration and broker from device credentials');
let state=await qa();if(!state.streams)throw new Error('SSE not connected');checks.push('authenticated SSE connected');await tap('收起引导');
const devices=[{deviceId:'first',name:'一号设备',enabled:true,online:false,lastSeen:null},{deviceId:'second',name:'二号设备',enabled:true,online:false,lastSeen:null}];
await qa({devices,revision:2});await expect("document.querySelector('.n-select[aria-label=设备视图]')?.textContent.includes('全部设备')",'global selector ready');const viewOptions=await ui.optionLabels('.n-select[aria-label=设备视图]',3);if(viewOptions.length!==3||!['全部设备','一号设备','二号设备'].every(v=>viewOptions.includes(v)))throw new Error('Invalidation did not refresh all device choices');checks.push('invalidation refreshes device views');await ui.choose('.n-select[aria-label=设备视图]','二号设备');await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.content').textContent.includes('二号设备')",'global view selects one device');
await tap('接入引导');await qa({setup:[{deviceId:'first',name:'一号设备',deviceObserved:true,receiptObserved:false,displayObserved:false}]});await tap('核对接入状态');await expect("document.querySelector('.setup-guide').textContent.includes('曾观察到设备连接，测试回执未确认')",'device seen is not a receipt');await qa({setup:[{deviceId:'first',name:'一号设备',deviceObserved:true,receiptObserved:true,displayObserved:false}]});await tap('核对接入状态');await expect("document.querySelector('.setup-guide').textContent.includes('已有接收回执，展示未确认')",'received is distinct from displayed');await tap('收起引导');
await nav('路由');await tap('创建路由');await fillInput('[aria-label=路由编辑] input','SSE 草稿');await qa({revision:3});await wait(.5);await expect("document.querySelector('[aria-label=路由编辑] input').value==='SSE 草稿'",'invalidation preserves unsaved draft');
const before=(await qa()).requests.filter(r=>r.method==='GET'&&r.path==='/api/v1/devices').length;await qa({events:'unavailable'});await wait(5.5);const after=(await qa()).requests.filter(r=>r.method==='GET'&&r.path==='/api/v1/devices').length;if(after<=before)throw new Error('Polling fallback stopped');checks.push('polling continues when SSE unavailable');await qa({events:'ready'});await wait(2.5);await expect("document.querySelector('[aria-label=路由编辑] input').value==='SSE 草稿'",'reconnect preserves draft');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await tap('接入引导');await expect("document.documentElement.scrollWidth<=390",'390px header and guide fit');cliLog(await captureScreenshot());await qa({events:'unauthorized'});await expect("!!document.querySelector('.login-card') && !document.querySelector('.setup-guide')",'revoked stream clears session and guide');cliLog({passed:checks.length,checks});
EOF
} | ego-browser nodejs
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS realtime onboarding',{keep:false}));
EOF
