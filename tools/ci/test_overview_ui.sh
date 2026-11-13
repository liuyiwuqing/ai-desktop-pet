#!/usr/bin/env bash
# Actual Vue and RAM HTTP fixture; no real CORE/database/Broker/device.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-overview"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/overview_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const base='http://127.0.0.1:18806',name='NEXUS overview regression';
const existing=(await listTaskSpaces()).find(s=>s.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing space needs user confirmation');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value){return JSON.parse(await serverFetch(base+'/__qa/overview',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<60;i++){if(await js(expression)){results.push(label);return;}await wait(0.1);}cliLog(await snapshotText());throw new Error('UI assertion failed: '+label);}
async function tap(text){if(!await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))throw new Error('Button unavailable: '+text);}
async function navigate(label){await js(`(()=>{const b=[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)}));if(!b)throw new Error('Navigation missing');b.click();})()`);}
async function login(){await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login succeeds');}
await openOrReuseTab(base,{wait:true,timeout:15});await gotoAndWait(base,{timeout:15});await login();
await expect("[...document.querySelectorAll('.sidebar nav button')].some(b=>b.textContent.includes('概览'))",'overview navigation exists');
await navigate('概览');
await expect("document.querySelector('h1')?.innerText==='运行概览' && document.querySelector('.overview-panel')?.innerText.includes('25.0%')",'overview loads its own authenticated HTTP snapshot');
await expect("document.querySelector('.overview-panel')?.innerText.includes('2026-11-13 · UTC') && document.querySelector('[data-metric=online] dd')?.innerText==='2' && document.querySelector('[data-metric=offline] dd')?.innerText==='3'",'UTC day and device counts are explicit');
await expect("document.querySelector('[data-metric=messages] dd')?.innerText==='7' && document.querySelector('[data-metric=backlog] dd')?.innerText==='6' && document.querySelector('.overview-panel')?.innerText.includes('停用 1 台')",'message, backlog and overlapping disabled counts are distinct');
await fixture({mode:'mqtt_down'});await tap('刷新概览');
await expect("document.querySelector('.overview-panel [role=status]')?.innerText.includes('MQTT 连接不可用') && document.querySelector('[data-metric=online] dd')?.innerText==='2'",'Broker failure never rewrites the online count');
await fixture({mode:'error'});await tap('刷新概览');
await expect("!!document.querySelector('.overview-panel [role=alert]') && !document.querySelector('.overview-panel dl')",'failed read hides stale counts instead of showing empty or healthy');
await fixture({mode:'empty'});await tap('刷新概览');
await expect("document.querySelector('[data-metric=messages] dd')?.innerText==='0' && document.querySelector('[data-metric=failure-rate] dd')?.innerText==='—' && document.querySelector('.overview-panel')?.innerText.includes('尚未登记设备')",'empty snapshot has zero counts and no fabricated zero-percent failure rate');
await fixture({mode:'success',delay:1});await tap('刷新概览');
await expect("document.querySelector('.overview-panel [aria-busy=true]')?.innerText==='正在读取概览…' && document.querySelector('.overview-panel button').disabled && !document.querySelector('.overview-panel dl')",'loading and repeat clicks are explicit');
await expect("document.querySelector('[data-metric=messages] dd')?.innerText==='7'",'delayed snapshot finishes');await fixture({delay:0});
const before=(await fixture({})).requests;
await expect("document.querySelector('[data-metric=messages] dd')?.innerText==='7'",'polling starts from current snapshot');
for(let i=0;i<65;i++){if((await fixture({})).requests>before)break;await wait(0.1);}
if((await fixture({})).requests<=before)throw new Error('Visible overview did not poll');results.push('visible overview polls');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && [...document.querySelectorAll('.overview-panel dl>div')].every(e=>e.getBoundingClientRect().right<=390)",'overview fits 390 CSS pixels');
cliLog({mobileScreenshot:await captureScreenshot()});
await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({mode:'empty',delay:1});await tap('刷新概览');await navigate('消息');
await wait(1.2);await expect("document.querySelector('h1')?.innerText==='消息' && !document.querySelector('.overview-panel')",'late response cannot restore an unmounted overview');
await fixture({mode:'success',delay:0});await navigate('概览');await expect("document.querySelector('[data-metric=messages] dd')?.innerText==='7'",'returning to overview reads a fresh snapshot');
await fixture({mode:'empty',delay:1});await tap('刷新概览');await tap('退出登录');await wait(1.2);
await expect("!!document.querySelector('.login-card') && !document.querySelector('.overview-panel') && document.querySelector('.token-label input').value===''",'logout discards delayed snapshot and token');
const beforeAuth=(await fixture({mode:'unauthorized',delay:0})).requests;
await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("!!document.querySelector('.login-card') && !document.querySelector('.overview-panel') && document.querySelector('.token-label input').value===''",'overview 401 ends the session');
if((await fixture({})).requests<=beforeAuth)throw new Error('401 check never reached overview HTTP');
cliLog({passed:results.length,checks:results,scope:'actual Vue and RAM-only HTTP; no database/Broker/device'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS overview regression',{keep:false}));
EOF
