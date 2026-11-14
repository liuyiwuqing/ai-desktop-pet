#!/usr/bin/env bash
# Actual Vue, RAM-only HTTP; no CORE, database, supplier, device or real deletion.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-automation-lifecycle"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/automation_lifecycle_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const name='NEXUS automation lifecycle',base='http://127.0.0.1:18814',id='11111111-1111-4111-8111-111111111111';
const prior=(await listTaskSpaces()).find(s=>s.name===name);
if(prior && prior.ownership!=='agent')throw new Error('Existing space requires confirmation');
await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true,timeout:15});
await cdp('Network.setCacheDisabled',{cacheDisabled:true});
const checks=[];
async function qa(value={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<100;i++){if(await js(expression)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label,scope='.content > section'){await expect(`(()=>{const b=[...document.querySelectorAll(${JSON.stringify(scope)}+' button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
async function nav(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
async function reset(){await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});await qa({reset:true});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login');await nav('自动化');await expect(`document.getElementById('delete-rule-${id}')?.disabled===false`,'deletion becomes available');}
async function openDelete(){await js(`document.getElementById('delete-rule-${id}').click()`);await expect("!!document.querySelector('.rule-delete')",'delete confirmation opens');}
async function confirmName(){await fillInput('[aria-label="确认删除的规则名称"]','隔离每日提醒');}
function deletions(s){return s.requests.filter(r=>r.method==='DELETE');}
await reset();
await expect("document.getElementById('delete-rule-22222222-2222-4222-8222-222222222222').disabled && document.getElementById('delete-rule-44444444-4444-4444-8444-444444444444').disabled",'enabled/in-flight deletion blocked');
await tap('测试预演','.record-card');await expect("document.querySelector('.rule-preview')?.innerText.includes('候选启用设备：2')",'preview shows candidate count');
await expect("document.querySelector('.rule-preview').innerText.includes('不创建消息')",'dry run clearly has no effects');
let state=await qa();if(state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/preview')).length)throw new Error('Preview executed an action');checks.push('preview sends no execution/write action');
await tap('关闭预演','.rule-preview');await openDelete();
await expect("document.querySelector('.rule-delete button.primary').disabled",'name confirmation required');
await fillInput('[aria-label="确认删除的规则名称"]','错误名称');await expect("document.querySelector('.rule-delete button.primary').disabled",'wrong name blocked');await confirmName();
await qa({write_delay:1});await tap('确认删除规则','.rule-delete');await expect("document.querySelector('.rule-delete button.primary').disabled",'request blocks duplicate submission');
await expect("!document.querySelector('.rule-delete') && !document.getElementById('delete-rule-11111111-1111-4111-8111-111111111111')",'confirmed deletion removes rule');
state=await qa();if(deletions(state).length!==1||deletions(state)[0].version!==1)throw new Error('Wrong deletion identity');checks.push('one delete uses observed version');
await reset();await openDelete();await confirmName();await qa({next_write:'drop_after'});await tap('确认删除规则','.rule-delete');
await expect("document.querySelector('.rule-delete')?.innerText.includes('尚未确认')",'lost confirmation keeps unknown result');
await nav('消息');await nav('自动化');await expect("!!document.querySelector('.rule-delete')",'navigation keeps original deletion');await tap('核对删除结果','.rule-delete');await expect("!document.querySelector('.rule-delete')",'read-only check confirms deletion');
state=await qa();if(deletions(state).length!==1)throw new Error('Readback replayed deletion');checks.push('checking does not repeat delete');
await reset();await openDelete();await confirmName();await qa({next_write:'drop_before'});await tap('确认删除规则','.rule-delete');await expect("document.querySelector('.rule-delete')?.innerText.includes('尚未确认')",'pre-write loss keeps original version');await tap('重试原版本删除','.rule-delete');await expect("!document.querySelector('.rule-delete')",'explicit retry confirms deletion');
state=await qa();if(deletions(state).length!==2||deletions(state).some(r=>r.version!==1))throw new Error('Retry changed version');checks.push('retry preserves fixed version');
await reset();await openDelete();await confirmName();await qa({next_write:'conflict'});await tap('确认删除规则','.rule-delete');await expect("document.querySelector('.rule-delete')?.innerText.includes('尚未确认')",'conflict keeps request for readback');await tap('关闭','.rule-delete');await expect("!document.querySelector('.rule-delete')",'close does not delete rule');
await reset();await qa({write_delay:1});await tap('测试预演','.record-card');await nav('消息');await wait(1.2);await expect("!document.querySelector('.rule-preview')",'late preview cannot change another page');
await reset();await openDelete();await confirmName();await qa({next_write:'unauthorized'});await tap('确认删除规则','.rule-delete');await expect("!!document.querySelector('.login-card') && !document.querySelector('.rule-delete')",'401 clears deletion and session');
await reset();await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await openDelete();await expect("document.documentElement.scrollWidth<=390",'390px confirmation fits viewport');cliLog(await captureScreenshot());
cliLog({passed:checks.length,checks});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS automation lifecycle',{keep:false}));
EOF
