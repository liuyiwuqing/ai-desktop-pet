#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-routing"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/routing_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
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
const space='NEXUS message routing',base='http://127.0.0.1:18815';
const prior=(await listTaskSpaces()).find(s=>s.name===space);if(prior&&prior.ownership!=='agent')throw new Error('Space requires confirmation');
await useOrCreateTaskSpace(space);await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});
let checks=[];
async function qa(body={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)}));}
async function expect(expression,label){for(let i=0;i<100;i++){if(await js(expression).catch(()=>false)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label){await expect(`(()=>{const b=[...document.querySelectorAll('.content button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
async function nav(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
async function reset(){await gotoAndWait(base+'/?t='+Date.now(),{timeout:15});await qa({reset:true});await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login');await nav('路由');await expect("document.querySelector('.content').textContent.includes('默认广播')",'broadcast mode displayed');}
async function draft(){await tap('创建路由');await fillInput('[aria-label="路由编辑"] input','我的路由');await ui.choose('[aria-label=路由编辑] .n-select[aria-label=目标设备]','回归测试设备（dodo-ui-fixture）');}
await reset();await draft();await nav('消息');await nav('路由');await expect("document.querySelector('[aria-label=路由编辑] input')?.value==='我的路由'",'draft survives navigation');
await qa({next_write:'drop_after'});await tap('保存路由');await expect("document.querySelector('[aria-label=路由编辑]').textContent.includes('未确认')",'lost save keeps original submission');await tap('重试原版本保存');await expect("!document.querySelector('[aria-label=路由编辑]') && document.querySelector('.record-card h2')?.textContent.trim()==='我的路由'",'retry confirms one route');
let state=await qa(),writes=state.requests.filter(r=>r.method==='PUT'&&r.path.includes('/rules/'));if(writes.length!==2||JSON.stringify(writes[0])!==JSON.stringify(writes[1])||state.routes.length!==1)throw new Error('Route retry changed identity');checks.push('same id/version/definition on retry');
await tap('切换模式');await qa({next_write:'drop_after'});await tap('确认切换模式');await expect("document.querySelector('.content').textContent.includes('按规则路由')",'policy applied despite response loss');await tap('重试原目标模式');await expect("!document.querySelector('.content').textContent.includes('重试原目标模式')",'policy retry confirmed');state=await qa();if(state.policy.mode!=='rules')throw new Error('Retry reversed routing policy');checks.push('policy unknown retry never reverses mode');
await tap('测试与预演');await tap('预演匹配设备');await expect("document.querySelector('.content').textContent.includes('匹配：dodo-ui-fixture')",'preview reports candidate device');
await ui.choose('.n-select[aria-label=测试设备]','回归测试设备');await qa({next_write:'drop_after'});await tap('发送到选择设备');await expect("document.querySelector('.content').textContent.includes('重试同一测试消息')",'test response loss keeps request');await nav('消息');await nav('路由');await tap('重试同一测试消息');await expect("document.querySelector('.content').textContent.includes('消息编号：')",'targeted test confirms');state=await qa();let tests=state.requests.filter(r=>r.path.endsWith('/test'));if(tests.length!==2||JSON.stringify(tests[0])!==JSON.stringify(tests[1]))throw new Error('Target test retry changed identity');checks.push('targeted test preserves selected devices and body');
await tap('删除路由');await expect("[...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='确认删除路由').disabled",'deletion requires route name');await js("(()=>{let e=[...document.querySelectorAll('label')].find(e=>e.textContent.includes('输入路由名称')).closest('.n-form-item').querySelector('input');e.value='我的路由';e.dispatchEvent(new Event('input',{bubbles:true}));})()");await tap('确认删除路由');await expect("!document.querySelector('.record-card')",'route removed');
await qa({read_mode:'error'});await tap('刷新');await expect("!!document.querySelector('.content [role=alert]') && !document.querySelector('.record-card')",'read error hides stale routes');await qa({read_mode:'success'});await tap('刷新');await expect("!document.querySelector('.content [role=alert]')",'read recovers');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await draft();await expect("document.documentElement.scrollWidth<=390",'390px form fits');cliLog(await captureScreenshot());
await qa({next_write:'unauthorized'});await tap('保存路由');await expect("!!document.querySelector('.login-card')",'401 clears session');cliLog({passed:checks.length,checks});
EOF
} | ego-browser nodejs
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS message routing',{keep:false}));
EOF
