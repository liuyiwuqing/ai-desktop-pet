#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-pull"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/pull_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
 if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
 if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
 sleep .1
done
ego-browser nodejs <<'EOF'
const name='NEXUS pull sources',base='http://127.0.0.1:18822',checks=[];
const prior=(await listTaskSpaces()).find(s=>s.name===name);if(prior&&prior.ownership!=='agent')throw new Error('Existing space requires confirmation');
await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true,timeout:15});
async function qa(v={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(v)}));}
async function expect(e,label){for(let i=0;i<100;i++){if(await js(e).catch(()=>false)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label,scope='.pull-panel'){await expect(`(()=>{const b=[...document.querySelectorAll(${JSON.stringify(scope)}+' button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
async function nav(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
async function login(role='admin'){await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});await qa({reset:true,role});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",role+' login');await nav('接入');await expect("document.querySelector('.pull-panel')?.innerText.includes('隔离 RSS')",'configured pull source loads');}
await login();await expect("[...document.querySelectorAll('.pull-panel button')].find(b=>b.textContent==='立即拉取').disabled",'disabled source cannot poll');await tap('测试连接');await expect("document.querySelector('.pull-preview')?.innerText.includes('未创建消息、投递或同步基线')",'probe shows data without writes');let state=await qa();if(state.pulls[0].baseline||state.requests.filter(r=>r.path.startsWith('/api/v1/connectors/pull/')&&!r.path.endsWith('/probe')).length)throw new Error('Probe changed state');checks.push('probe does not poll or enable');await tap('关闭连接测试');
await qa({next_write:'drop_after'});await tap('启用拉取');await expect("document.querySelector('.pull-panel')?.innerText.includes('来源状态尚未确认')",'lost enable retains original target');await nav('消息');await nav('接入');await tap('重试原启停操作');await expect("!document.querySelector('.pull-panel')?.innerText.includes('来源状态尚未确认') && document.querySelector('.pull-panel')?.innerText.includes('停用拉取')",'retry confirms the same enabled state');state=await qa();const toggles=state.requests.filter(r=>r.method==='PUT');if(toggles.length!==2||JSON.stringify(toggles[0].body)!==JSON.stringify(toggles[1].body)||state.pulls[0].version!==2)throw new Error('Enable retry changed target or version');checks.push('enable retry preserves configuration and version');
await qa({next_write:'drop_after'});await tap('立即拉取');await expect("document.querySelector('.pull-panel')?.innerText.includes('同步未确认')",'lost poll keeps check-only notice');await nav('消息');await nav('接入');await expect("document.querySelector('.pull-panel')?.innerText.includes('同步未确认')",'navigation preserves unknown poll');await tap('核对最近同步');state=await qa();if(state.requests.filter(r=>r.path.endsWith('/poll')).length!==1)throw new Error('Readback replayed pull');checks.push('readback never repeats provider poll');await expect("document.querySelector('.pull-panel')?.innerText.includes('同步正常')",'health confirms completed baseline');
await tap('查看接收消息');await expect("document.querySelector('[aria-label=来源标识]')?.value==='rss.demo'",'source action opens the matching message filter');await nav('接入');await expect("!!document.querySelector('.pull-panel')",'source panel remains reachable');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await expect("document.documentElement.scrollWidth<=390",'390px source panel fits');cliLog(await captureScreenshot());
await login('viewer');await expect("!document.querySelector('.pull-panel .management-actions')",'viewer reads status without provider controls');await login('operator');await expect("!document.querySelector('.pull-panel .management-actions')",'operator cannot manage provider configuration');await login();await qa({next_write:'unauthorized'});await tap('启用拉取');await expect("!!document.querySelector('.login-card')",'401 clears pull session');cliLog({passed:checks.length,checks});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS pull sources',{keep:false}));
EOF
