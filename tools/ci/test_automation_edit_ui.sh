#!/usr/bin/env bash
# Actual Vue; RAM-only HTTP boundary. No real rule, message, CORE, DB or device.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-automation-edit"
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
const name='NEXUS automation editing',base='http://127.0.0.1:18813';
const existing=(await listTaskSpaces()).find(s=>s.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing space requires user confirmation');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const prior=(await listTabs()).filter(tab=>tab.url.startsWith(base));
if(prior.length){await switchTab(prior[0].targetId);for(const tab of prior.slice(1))await closeTab(tab.targetId);}
else await openOrReuseTab(base,{wait:true,timeout:15});
await cdp('Network.setCacheDisabled',{cacheDisabled:true});
const results=[],daily='11111111-1111-4111-8111-111111111111',active='22222222-2222-4222-8222-222222222222',comment='33333333-3333-4333-8333-333333333333',running='44444444-4444-4444-8444-444444444444';
async function fixture(value={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<80;i++){if(await js(expression)){results.push(label);return;}await wait(0.1);}cliLog(await snapshotText());throw new Error('UI assertion failed: '+label);}
async function tap(text,scope='.content > section'){for(let i=0;i<80;i++){if(await js(`(()=>{const b=[...document.querySelectorAll(${JSON.stringify(scope)}+' button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))return;await wait(0.1);}throw new Error('Button unavailable: '+text);}
async function navigate(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
async function ready(){for(let i=0;i<80;i++){if(await js("!!document.querySelector('.content > section') && !document.querySelector('.content > section [aria-busy=true]')"))return;await wait(0.1);}throw new Error('Panel did not settle');}
async function field(label,value){await js(`(()=>{const e=document.querySelector('[aria-label="'+${JSON.stringify(label)}+'"]');if(!e||e.disabled)throw new Error('Field not editable');e.value=${JSON.stringify(value)};e.dispatchEvent(new Event('input',{bubbles:true}));document.activeElement?.blur();})()`);}
function writes(state){return state.requests.filter(r=>r.method==='PUT'&&!r.path.endsWith('/enabled'));}
async function reset(){
 await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');
await fixture({reset:true});
 await cdp('Emulation.clearDeviceMetricsOverride');
 await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
 await expect("!!document.querySelector('.sidebar')",'session opens');await navigate('自动化');
 await expect("document.querySelectorAll('.record-card').length===4",'all fixture rules are read');await ready();
}
async function edit(id=daily){await expect(`document.getElementById('edit-rule-${id}')?.disabled===false`,'edit action becomes available');await js(`document.getElementById('edit-rule-${id}').click()`);await expect("!!document.querySelector('.rule-editor')",'rule editor opens');}
async function rename(value){await field('编辑规则名称',value);}
await reset();
await expect(`document.getElementById('edit-rule-${daily}')?.disabled===false && !document.getElementById('edit-rule-${comment}').disabled && document.getElementById('edit-rule-${active}').disabled && document.getElementById('edit-rule-${running}').disabled`,'enabled and executing rules cannot be edited');
await edit();await expect("document.activeElement?.getAttribute('aria-label')==='编辑规则名称'",'editor focuses the named input');
await expect("document.querySelector('[aria-label=编辑提醒时间]').value==='08:00' && document.querySelector('[aria-label=编辑提醒时区]').value==='Asia/Shanghai' && !document.querySelector('.rule-editor select')",'daily values are loaded and rule kind cannot change');
await rename('跨导航草稿');await field('编辑提醒时间','09:30');await field('编辑提醒时区','UTC');await field('编辑提醒标题','修改后标题');await field('编辑提醒内容','修改后内容');
await wait(5.3);await ready();await expect("document.querySelector('[aria-label=编辑规则名称]').value==='跨导航草稿' && document.querySelector('[aria-label=编辑提醒内容]').value==='修改后内容'",'polling preserves the edit draft');
await navigate('消息');await navigate('自动化');await ready();await expect("document.querySelector('[aria-label=编辑规则名称]').value==='跨导航草稿' && document.querySelector('[aria-label=编辑提醒时间]').value==='09:30'",'navigation preserves unsaved rule draft and base version');
if(writes(await fixture()).length)throw new Error('Reading replayed an edit');results.push('navigation and polling never submit edits');
await fixture({write_delay:1});await tap('保存修改','.rule-editor');await expect("document.querySelector('[aria-label=编辑规则名称]').disabled && document.querySelector('.rule-editor button.primary').disabled",'saving freezes draft and duplicate click');
await js("document.querySelector('.rule-editor button.primary').click()");await expect("!document.querySelector('.rule-editor') && document.querySelector('.record-card h2')?.innerText==='跨导航草稿'",'saved definition appears without enabling rule');await ready();
let state=await fixture(),saved=state.rules.find(r=>r.ruleId===daily);if(writes(state).length!==1||saved.enabled||saved.version!==2)throw new Error('Edit repeated or enabled a rule');
let body=writes(state)[0].body;if(body.expectedVersion!==1||body.title!=='修改后标题'||body.localTime!=='09:30'||'trigger'in body||'requestKey'in body)throw new Error('Wrong fixed edit identity');results.push('one fixed versioned PUT saves without type or identity changes');
await expect(`document.activeElement?.id==='edit-rule-${daily}'`,'successful save restores the rule action focus');
await fixture({write_delay:0});await js(`document.getElementById('edit-rule-${daily}').closest('.record-card').querySelector('input[type=checkbox]').click()`);await ready();
let toggle;for(let i=0;i<80;i++){state=await fixture();toggle=state.requests.find(r=>r.method==='PUT'&&r.path.endsWith('/enabled'));if(toggle)break;await wait(.1);}if(toggle?.body.expectedVersion!==2)throw new Error('Enable omitted observed version');results.push('enable sends the currently observed version');
await expect(`document.getElementById('edit-rule-${daily}').disabled`,'enabling removes edit permission');
await edit(comment);await expect("!document.querySelector('[aria-label=编辑提醒标题]')",'comment editing hides notification payload fields');await rename('评论新名称');await tap('保存修改','.rule-editor');await expect("!document.querySelector('.rule-editor') && [...document.querySelectorAll('.record-card h2')].some(h=>h.innerText==='评论新名称')",'comment name can be edited');
state=await fixture();if(state.rules.find(r=>r.ruleId===comment).trigger!=='message.received')throw new Error('Comment type changed');results.push('comment trigger and action stay fixed');
await reset();await edit();await rename('');await tap('保存修改','.rule-editor');await expect("!document.querySelector('[aria-label=编辑规则名称]').checkValidity()",'required name blocks empty submission');if(writes(await fixture()).length)throw new Error('Empty name wrote');results.push('invalid native form creates no request');
await rename('保存后断线');await fixture({next_write:'drop_after'});await tap('保存修改','.rule-editor');await expect("document.querySelector('.rule-editor [role=status]')?.innerText.includes('保存结果尚未确认') && document.querySelector('[aria-label=编辑规则名称]').disabled",'saved response loss freezes original definition');
await navigate('消息');await navigate('自动化');await ready();await expect("document.querySelector('.rule-editor button.primary')?.innerText==='重试原版本修改' && document.querySelector('[aria-label=编辑规则名称]').value==='保存后断线'",'unknown edit identity survives navigation');
await wait(5.3);if(writes(await fixture()).length!==1)throw new Error('Unknown edit replayed automatically');results.push('unknown edit is not replayed by polling');
await tap('重试原版本修改','.rule-editor');await expect("!document.querySelector('.rule-editor')",'explicit original edit retry confirms the saved result');state=await fixture();if(writes(state).length!==2||JSON.stringify(writes(state)[0].body)!==JSON.stringify(writes(state)[1].body)||state.rules[0].version!==2)throw new Error('Unknown edit changed version or body');results.push('response-loss retry keeps one resulting version');
await reset();await edit();await rename('保存前断线');await fixture({next_write:'drop_before'});await tap('保存修改','.rule-editor');await expect("document.querySelector('.rule-editor button.primary')?.innerText==='重试原版本修改' && !document.querySelector('.rule-editor button.primary').disabled",'pre-save loss retains original request');await fixture({next_write:'deny'});await tap('重试原版本修改','.rule-editor');await expect("document.querySelector('[aria-label=编辑规则名称]').disabled && !document.querySelector('.rule-editor button.primary').disabled",'rejection after an unknown result cannot discard identity');await tap('重试原版本修改','.rule-editor');await expect("!document.querySelector('.rule-editor')",'explicit retry recovers a pre-save loss');
state=await fixture();if(new Set(writes(state).map(r=>JSON.stringify(r.body))).size!==1||state.rules[0].version!==2)throw new Error('Pre-save identity changed');results.push('all retries preserve URL, version and definition');
await reset();await edit();await rename('被拒绝草稿');await fixture({next_write:'deny'});await tap('保存修改','.rule-editor');await expect("!!document.querySelector('.content > .feedback.error') && !document.querySelector('[aria-label=编辑规则名称]').disabled",'first explicit rejection releases draft correction');await rename('校正草稿');await tap('保存修改','.rule-editor');await expect("!document.querySelector('.rule-editor')",'corrected rejected draft can save');
await reset();await edit();await rename('我的旧版本草稿');state=await fixture();state.rules[0].name='另一客户端版本';state.rules[0].version=2;await fixture({rules:state.rules});await tap('保存修改','.rule-editor');await expect("document.querySelector('.rule-editor [role=alert]')?.innerText.includes('版本已变化') && document.querySelector('.rule-editor button.primary').disabled && document.querySelector('[aria-label=编辑规则名称]').value==='我的旧版本草稿'",'version conflict preserves draft and blocks stale resubmission');
await tap('核对当前规则','.rule-editor');await expect("document.querySelector('.rule-readback')?.innerText.includes('另一客户端版本') && document.querySelector('[aria-label=编辑规则名称]').value==='我的旧版本草稿'",'readback shows current server definition beside retained draft');await tap('放弃草稿并载入当前版本','.rule-editor');await expect("document.querySelector('[aria-label=编辑规则名称]').value==='另一客户端版本' && !document.querySelector('[aria-label=编辑规则名称]').disabled && !document.querySelector('.rule-readback')",'explicit load replaces draft and base revision');await rename('合并后修改');await tap('保存修改','.rule-editor');await expect("!document.querySelector('.rule-editor')",'edit can save after explicit conflict recovery');state=await fixture();if(writes(state).at(-1).body.expectedVersion!==2||state.rules[0].version!==3)throw new Error('Conflict recovery missed version');results.push('conflict recovery sends the loaded current version');
for(const mode of ['error','invalid']){
 await reset();await edit();await rename('异常确认-'+mode);await fixture({next_write:mode});await tap('保存修改','.rule-editor');await expect("document.querySelector('.rule-editor button.primary')?.innerText==='重试原版本修改' && !document.querySelector('.rule-editor button.primary').disabled",'failure retains edit identity: '+mode);await tap('重试原版本修改','.rule-editor');await expect("!document.querySelector('.rule-editor')",'failure recovery keeps the same edit: '+mode);
 state=await fixture();if(JSON.stringify(writes(state)[0].body)!==JSON.stringify(writes(state)[1].body)||state.rules[0].version!==2)throw new Error('Fault recovery repeated revision');results.push('fault recovery results in one revision: '+mode);
}
await reset();await edit();await rename('只读核对确认');await fixture({next_write:'drop_after'});await tap('保存修改','.rule-editor');await expect("document.querySelector('.rule-editor button.primary')?.innerText==='重试原版本修改' && !document.querySelector('.rule-editor button.primary').disabled",'readback recovery starts from unknown result');await tap('核对当前规则','.rule-editor');await expect("!document.querySelector('.rule-editor')",'matching readback confirms current definition');if(writes(await fixture()).length!==1)throw new Error('Readback repeated write');results.push('readback confirmation uses no second PUT');
await reset();await edit();await rename('读取失败保留');await fixture({detail_mode:'error'});await tap('核对当前规则','.rule-editor');await expect("!!document.querySelector('.content > .feedback.error') && document.querySelector('[aria-label=编辑规则名称]').value==='读取失败保留' && !document.querySelector('.rule-readback')",'failed readback preserves draft without fake server values');
await fixture({detail_mode:'invalid'});await tap('核对当前规则','.rule-editor');await expect("document.querySelector('.content > .feedback.error')?.innerText.includes('有效的规则定义') && !document.querySelector('.rule-readback') && document.querySelector('[aria-label=编辑规则名称]').value==='读取失败保留'",'invalid readback cannot replace the current draft');
await fixture({read_mode:'error',detail_mode:'success'});await tap('刷新');await expect("!!document.querySelector('.content > section > [role=alert]') && document.querySelectorAll('.record-card').length===0 && document.querySelector('.rule-editor button.primary').disabled",'list failure hides stale rule actions and disables saving');await fixture({read_mode:'success'});await tap('刷新');await expect("document.querySelectorAll('.record-card').length===4 && !document.querySelector('[aria-label=编辑规则名称]').disabled && document.querySelector('[aria-label=编辑规则名称]').value==='读取失败保留'",'list recovery preserves draft');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && [...document.querySelectorAll('.rule-editor input,.rule-editor textarea,.rule-editor button')].every(e=>e.getBoundingClientRect().right<=390)",'editor fields and actions fit 390 CSS pixels');cliLog({mobileScreenshot:await captureScreenshot()});await js("document.querySelector('.rule-editor .form-footer').scrollIntoView({block:'center'})");cliLog({mobileActionsScreenshot:await captureScreenshot()});await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({detail_delay:1});await tap('核对当前规则','.rule-editor');await expect("document.querySelector('.rule-editor button.primary').disabled",'pending readback disables competing edits');await tap('退出登录','.sidebar');await wait(1.2);await expect("!!document.querySelector('.login-card') && !document.querySelector('.rule-editor') && !document.querySelector('.sidebar')",'late readback cannot restore logged out editor');
await reset();await edit();await rename('退出中的修改');await fixture({write_delay:1});await tap('保存修改','.rule-editor');await expect("document.querySelector('.rule-editor button.primary').disabled",'edit is in flight before logout');await tap('退出登录','.sidebar');await wait(1.2);await expect("!!document.querySelector('.login-card') && !document.querySelector('.sidebar')",'late saved result cannot restore the session');
await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'new session opens after late edit');await navigate('自动化');await ready();await expect("!document.querySelector('.rule-editor')",'new session has no previous edit identity');
await edit();await fixture({next_write:'unauthorized',write_delay:0});await rename('鉴权失败');await tap('保存修改','.rule-editor');await expect("!!document.querySelector('.login-card') && document.querySelector('.inline-error')?.innerText.includes('管理令牌无效')",'edit 401 clears the session');
if((await listTabs()).length!==1)throw new Error('Regression must reuse one tab');results.push('all reset cycles reuse one browser tab');
cliLog({passed:results.length,checks:results,scope:'actual Vue, RAM HTTP; no real rules/messages/CORE/DB/device'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS automation editing',{keep:false}));
EOF
