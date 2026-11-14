#!/usr/bin/env bash
# Actual Vue; RAM-only HTTP boundary. No real rule, message, CORE, DB or device.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-automation-conditions"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/automation_edit_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const name='NEXUS automation conditions',base='http://127.0.0.1:18813',checks=[];
const prior=(await listTaskSpaces()).find(s=>s.name===name);if(prior&&prior.ownership!=='agent')throw new Error('Existing space requires confirmation');
await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true,timeout:15});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
async function qa(value={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<100;i++){if(await js(expression)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label,scope='.content > section'){await expect(`(()=>{const b=[...document.querySelectorAll(${JSON.stringify(scope)}+' button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
async function nav(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
await qa({reset:true});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login');await nav('自动化');await tap('创建规则');
await js("(()=>{const f=document.querySelector('.content form:not(.rule-editor)');f.querySelector('input').value='条件规则';f.querySelector('input').dispatchEvent(new Event('input',{bubbles:true}));const s=f.querySelector('select');s.value='incoming';s.dispatchEvent(new Event('change',{bubbles:true}));})()");
await expect("!!document.querySelector('[aria-label=匹配来源]') && !document.querySelector('input[type=time]')",'incoming rule exposes conditions without daily schedule');
await js("(()=>{const f=document.querySelector('.content form:not(.rule-editor)');const labels=[...f.querySelectorAll('label')];for(const [label,value]of [['提醒标题','条件标题'],['提醒内容','条件正文']]){const n=labels.find(l=>l.textContent.trim().startsWith(label)).querySelector('input,textarea');n.value=value;n.dispatchEvent(new Event('input',{bubbles:true}));}})()");
await fillInput('[aria-label=匹配来源]','wh.demo');await fillInput('[aria-label=匹配事件类型]','alert.created');await fillInput('[aria-label=包含关键词]','紧急');await js("(()=>{const n=document.querySelector('[aria-label=匹配优先级]');n.value='high';n.dispatchEvent(new Event('change',{bubbles:true}));})()");
await qa({next_write:'drop_after'});await tap('保存规则');await expect("document.querySelector('.content').innerText.includes('规则保存结果尚未确认')",'unknown creation preserves fixed rule');await nav('消息');await nav('自动化');await expect("document.querySelector('[aria-label=匹配来源]')?.value==='wh.demo' && document.querySelector('[aria-label=匹配优先级]')?.value==='high'",'navigation retains all conditional values');await tap('重试保存同一规则');await expect("!document.querySelector('.content form')",'original conditional creation confirmed');
let state=await qa(),creates=state.requests.filter(r=>r.method==='POST'&&r.path==='/api/v1/automation/rules');if(creates.length!==2||JSON.stringify(creates[0].body)!==JSON.stringify(creates[1].body))throw new Error('Conditional retry identity changed');checks.push('condition creation reuses one identity and body');
const rule=state.rules.find(r=>r.name==='条件规则');if(rule.enabled||rule.arguments.keywordFilter!=='紧急')throw new Error('Creation did not preserve conditions');
await expect(`document.getElementById('edit-rule-${rule.ruleId}')?.disabled===false`,'new conditional rule remains disabled and editable');await js(`document.getElementById('edit-rule-${rule.ruleId}').click()`);await expect("document.querySelector('[aria-label=编辑包含关键词]')?.value==='紧急' && !document.querySelector('[aria-label=编辑提醒时间]')",'editor restores conditional definition');
await fillInput('[aria-label=编辑包含关键词]','修改');await qa({next_write:'drop_after'});await tap('保存修改','.rule-editor');await expect("document.querySelector('.rule-editor')?.innerText.includes('尚未确认')",'unknown edit freezes conditional version');await tap('核对当前规则','.rule-editor');await expect("!document.querySelector('.rule-editor')",'readback includes conditions before confirming edit');
state=await qa();const edits=state.requests.filter(r=>r.method==='PUT'&&!r.path.endsWith('/enabled'));if(edits.length!==1||edits[0].body.conditions.keyword!=='修改'||edits[0].body.expectedVersion!==1)throw new Error('Wrong conditional edit');checks.push('conditional edit uses observed version and no replay');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await js(`document.getElementById('edit-rule-${rule.ruleId}').click()`);await expect("document.documentElement.scrollWidth<=390",'390px conditional editor fits viewport');cliLog(await captureScreenshot());cliLog({passed:checks.length,checks});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS automation conditions',{keep:false}));
EOF
