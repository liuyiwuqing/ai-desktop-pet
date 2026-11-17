#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-sources"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/sources_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
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
const name='NEXUS webhook sources',base='http://127.0.0.1:18816';
const prior=(await listTaskSpaces()).find(s=>s.name===name);if(prior&&prior.ownership!=='agent')throw new Error('Space requires confirmation');await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});let checks=[];
async function qa(body={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)}));}
async function expect(e,label){for(let i=0;i<100;i++){if(await js(e).catch(()=>false)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label){await expect(`(()=>{const b=[...document.querySelectorAll('.content button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
async function nav(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
async function create(){await tap('新增来源');await fillInput('[aria-label=新建来源] input[aria-label=名称]','隔离来源');await ui.choose('[aria-label=新建来源] .n-select','回归测试设备');}
await qa({reset:true});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login');await nav('接入');await expect("document.querySelector('.content').textContent.includes('暂无 Webhook')",'source list empty');await create();await tap('保存来源');await expect("document.querySelector('.content').textContent.includes('本次凭据')",'new credentials shown once');await expect("[...document.querySelectorAll('.content input[readonly]')].every(e=>e.type==='password')",'credentials masked');await tap('已保存，隐藏凭据');await expect("!document.querySelector('.content input[readonly]')",'secrets removed after close');
await tap('启用来源');await expect("[...document.querySelectorAll('.content button')].some(b=>b.textContent.trim()==='停用来源')",'source enabled');await expect("[...document.querySelectorAll('.content button')].find(b=>b.textContent.trim()==='轮换来源凭据').disabled",'enabled credentials cannot rotate');await tap('测试来源');let state=await qa();if(!state.requests.some(r=>r.path.endsWith('/test')&&r.body.requestKey))throw new Error('Missing test identity');checks.push('source test has explicit request key');await tap('停用来源');await expect("[...document.querySelectorAll('.content button')].some(b=>b.textContent.trim()==='启用来源')",'source disabled');await tap('轮换来源凭据');await expect("[...document.querySelectorAll('.content button')].find(b=>b.textContent.trim()==='确认轮换来源凭据').disabled",'rotation requires source name');await js("(()=>{const e=[...document.querySelectorAll('label')].find(l=>l.textContent.includes('输入来源名')).closest('.n-form-item').querySelector('input');e.value='隔离来源';e.dispatchEvent(new Event('input',{bubbles:true}));})()");await tap('确认轮换来源凭据');await expect("document.querySelector('.content input[readonly]')?.value==='fixture-token-rotated-not-real'",'rotation returns new credentials');await tap('已保存，隐藏凭据');
await create();await qa({next_write:'drop_after'});await tap('保存来源');await expect("document.querySelector('.content').textContent.includes('重试原来源操作')",'response loss keeps source request');await nav('消息');await nav('接入');await tap('重试原来源操作');await expect("!document.querySelector('.content').textContent.includes('重试原来源操作')",'same source retry confirms');state=await qa();const created=state.requests.filter(r=>r.method==='PUT'&&r.path.startsWith('/api/v1/sources'));if(created.length!==3||JSON.stringify(created[1])!==JSON.stringify(created[2]))throw new Error('Source retry identity changed');checks.push('source creation retry preserves id and body');await expect("!document.querySelector('.content input[readonly]')",'lost creation secret is not replayed');
await qa({read_mode:'error'});await tap('刷新来源');await expect("!!document.querySelector('.content section [role=alert]')",'read failure is visible');await qa({read_mode:'success'});await tap('刷新来源');await expect("!document.querySelector('.content section [role=alert]')",'read recovers');await qa({next_write:'drop_after'});await tap('测试来源');await expect("document.querySelector('.content').textContent.includes('放弃重试并只读核对')",'a second loss still offers the discard path');await tap('放弃重试并只读核对');await expect("!document.querySelector('.content').textContent.includes('放弃重试并只读核对') && document.activeElement?.tagName==='H2' && document.activeElement.innerText.trim()==='Webhook 来源'",'discarding an unconfirmed source request returns focus to the section heading');await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await expect("document.documentElement.scrollWidth<=390",'390px source page fits');cliLog(await captureScreenshot());
await qa({next_write:'unauthorized'});await tap('启用来源');await expect("!!document.querySelector('.login-card') && !document.querySelector('.content input[readonly]')",'401 clears session and credentials');cliLog({passed:checks.length,checks});
EOF
} | ego-browser nodejs
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS webhook sources',{keep:false}));
EOF
