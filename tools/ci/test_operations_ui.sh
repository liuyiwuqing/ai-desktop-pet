#!/usr/bin/env bash
# Actual Vue and RAM-only HTTP boundaries; no real Bridge/CORE/database/device.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-operations"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/operations_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const base='http://127.0.0.1:18808',name='NEXUS operations regression';
const existing=(await listTaskSpaces()).find(s=>s.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing space needs user confirmation');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<60;i++){if(await js(expression).catch(()=>false)){results.push(label);return;}await wait(0.1);}cliLog(await snapshotText());cliLog({fixture:await fixture({})});throw new Error('UI assertion failed: '+label);}
async function tap(text,scope='.content > section'){if(!await js(`(()=>{const b=[...document.querySelectorAll(${JSON.stringify(scope)}+' button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))throw new Error('Button unavailable: '+text);}
async function navigate(label){await js(`(()=>{const b=[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)}));if(!b)throw new Error('Navigation missing');b.click();})()`);}
await fixture({reset:true});await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');

await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("!!document.querySelector('.sidebar')",'login succeeds');await navigate('工具');
await expect("[...document.querySelectorAll('.content > section button')].some(b=>b.innerText.trim()==='查询' && !b.disabled)",'tool catalog is ready');
await fixture({next_write:'drop_after'});await tap('查询');
await expect("document.querySelector('.content > .feedback.error')?.innerText.includes('尚未确认')",'saved tool request response loss is visible');
await js("document.querySelector('.content > section button.primary').click()");
for(let i=0;i<60;i++){if((await fixture({})).requests.filter(r=>r.method==='POST').length===2)break;await wait(0.1);}
const retry=await fixture({});
if(retry.jobs.length!==1 || retry.requests.filter(r=>r.method==='POST')[0].body.requestKey!==retry.requests.filter(r=>r.method==='POST')[1].body.requestKey)throw new Error('Unknown tool creation retry created a second logical request');
results.push('unknown creation retry retains one tool request identity');
await expect("document.querySelectorAll('.record-card').length===1",'saved tool request appears after retry');
async function ready(){await expect("!document.querySelector('.content > section [aria-busy=true]')",'panel read has settled');}
async function choose(selector,value){await js(`(()=>{const s=document.querySelector(${JSON.stringify(selector)});s.value=${JSON.stringify(value)};s.dispatchEvent(new Event('change',{bubbles:true}));})()`);}
async function tools(){await navigate('消息');await navigate('工具');await ready();}
async function automation(){await navigate('消息');await navigate('自动化');await ready();}
async function prepareRule(name,kind='comment'){
  if(!await js("!!document.querySelector('.inline-form')"))await tap('创建规则');
  await fillInput('.inline-form input',name);
  if(kind==='daily'){
    await choose('.inline-form select','daily');
    await js("(()=>{const input=document.querySelector('.inline-form input[type=time]');input.value='09:30';input.dispatchEvent(new Event('input',{bubbles:true}));})()");
    await fillInput('.inline-form .form-columns.spaced input:not([type=time])','Asia/Shanghai');
    await fillInput('.inline-form > label input','回归提醒');await fillInput('.inline-form textarea','隔离页面检查');
  }
}
await choose('.inline-form select','app.open');await choose('.inline-form label:nth-child(2) select','codex');
await tap('创建打开请求');await expect("document.querySelector('.record-card .badge')?.innerText==='等待你确认'",'opening an application first creates a confirmation request');
const opened=await fixture({});if(opened.requests.filter(r=>r.path.endsWith('/confirm')).length)throw new Error('Preparing auto-confirmed opening');results.push('prepare never confirms application opening');
await fixture({write_delay:1});await tap('确认打开 codex');
await expect("[...document.querySelectorAll('.record-card button')].filter(b=>b.innerText.includes('确认打开')).every(b=>b.disabled)",'confirmation disables repeated action');
await js("document.querySelector('.record-card button.primary')?.click()");
await expect("document.querySelector('.record-card .badge')?.innerText==='等待执行'",'confirmation returns the queued state');
const confirmed=await fixture({});if(confirmed.requests.filter(r=>r.path.endsWith('/confirm')).length!==1)throw new Error('Confirmation repeated');results.push('one confirmation posts once');
await fixture({write_delay:0});await tap('取消');await expect("document.querySelector('.record-card .badge')?.innerText==='已取消'",'queued tool job can be cancelled');
const unknown=await fixture({});unknown.jobs[0].state='unknown';unknown.jobs[0].lastError='bridge_result_unknown';await fixture({jobs:unknown.jobs});await tap('刷新');await expect("document.querySelector('.record-card .badge')?.innerText==='结果未知' && !document.querySelector('.record-card button.primary') && document.querySelector('.record-card .scope-note')?.innerText.includes('先核对原请求')",'unknown execution exposes reconciliation without a new confirmation');await tap('核对原请求结果');await expect("document.querySelector('.record-card .badge')?.innerText==='已获得结果' && document.querySelector('.tool-result')?.innerText.includes('已请求系统打开')",'reconciliation reads the original tool result');
const reconciled=await fixture({});if(reconciled.requests.filter(r=>r.method==='POST' && r.path.endsWith('/confirm')).length!==1 || reconciled.requests.filter(r=>r.method==='POST' && r.path.endsWith('/reconcile')).length!==1)throw new Error('Reconciliation re-executed confirmation');results.push('reconciliation never creates or confirms another job');
await tap('查看记录');await expect("!!document.querySelector('.audit-list li')",'tool audit is read separately');await tap('收起记录');
await fixture({audit_mode:'error',audit_delay:1});await tap('查看记录');
await expect("document.querySelector('.record-card [role=status]')?.innerText==='正在读取记录…'",'audit loading is distinct from empty records');await tap('收起记录');await wait(1.2);
await expect("!document.querySelector('.record-card [role=alert]') && !document.querySelector('.content > section > [role=alert]')",'late closed audit errors cannot contaminate the tool page');
await fixture({audit_delay:0});await tap('查看记录');await expect("!!document.querySelector('.record-card [role=alert]') && document.querySelectorAll('.record-card').length===2",'audit errors are scoped to the selected request');
await tap('收起记录');await fixture({audit_mode:'success',read_mode:'error',read_delay:1});await tap('刷新');
await expect("!!document.querySelector('.content > section [aria-busy=true]') && !document.querySelector('.record-card') && !document.querySelector('.empty-state')",'manual tool read clears stale actions without showing false empty state');
await expect("!!document.querySelector('.content > section > [role=alert]') && document.querySelector('.section-heading p')?.innerText==='Bridge 状态未知' && !document.querySelector('.inline-form') && !document.querySelector('.empty-state')",'tool read failure cannot offer stale capabilities or jobs');
await fixture({read_mode:'success',read_delay:0});await tap('刷新');await expect("document.querySelectorAll('.record-card').length===2",'tool read recovers');
await fixture({bridge:'off'});await tap('刷新');await expect("document.querySelector('.section-heading p')?.innerText==='Bridge 未启用' && document.querySelector('.inline-form button.primary')?.disabled",'disabled Bridge blocks new tool requests');
await fixture({bridge:'unavailable'});await tap('刷新');await expect("document.querySelector('.section-heading p')?.innerText==='Bridge 暂不可用' && document.querySelector('.inline-form button.primary')?.disabled",'unavailable Bridge blocks new tool requests');
await fixture({reset:true});await tools();await fixture({next_write:'drop_before'});await tap('查询');
await expect("document.querySelector('.scope-note[role=status]')?.innerText.includes('结果尚未确认') && document.querySelector('.inline-form select')?.disabled",'lost tool request freezes tool and arguments');
await navigate('消息');await navigate('工具');await ready();await expect("document.querySelector('.inline-form button.primary')?.innerText==='重试同一请求'",'pending tool identity survives navigation');
await wait(5.2);if((await fixture({})).requests.filter(r=>r.method==='POST').length!==1)throw new Error('Tool polling replayed creation');results.push('tool polling never replays unknown creation');
await fixture({next_write:'deny'});await tap('重试同一请求');await expect("document.querySelector('.inline-form button.primary')?.innerText==='重试同一请求' && !document.querySelector('.inline-form button.primary').disabled && document.querySelector('.inline-form select')?.disabled",'a rejection after an unknown tool result cannot discard its identity');
await tap('重试同一请求');await expect("document.querySelectorAll('.record-card').length===1 && !document.querySelector('.inline-form select')?.disabled",'explicit tool retry recovers from pre-save loss');
const toolRetry=await fixture({});if(new Set(toolRetry.requests.filter(r=>r.method==='POST').map(r=>JSON.stringify(r.body))).size!==1)throw new Error('Tool retry changed parameters');results.push('navigation retry preserves exact tool body');
for(const mode of ['deny','error','invalid']){
  await fixture({reset:true});await tools();await fixture({next_write:mode});await tap('查询');
  if(mode==='deny'){
    await expect("!!document.querySelector('.content > .feedback.error') && !document.querySelector('.inline-form select')?.disabled",'explicit tool rejection releases editing');
    await tap('查询');await expect("document.querySelectorAll('.record-card').length===1",'new tool creation succeeds after a rejected request');
    const state=await fixture({});const posts=state.requests.filter(r=>r.method==='POST');if(posts[0].body.requestKey===posts[1].body.requestKey)throw new Error('Rejected tool key reused');results.push('rejected tool creation starts a new identity');
  }else{
    await expect("document.querySelector('.inline-form button.primary')?.innerText==='重试同一请求'",mode==='error'?'tool 503 keeps identity':'invalid tool acknowledgment keeps identity');
    await tap('重试同一请求');await expect("document.querySelectorAll('.record-card').length===1",'tool retry acknowledges exactly one job');
  }
}
await fixture({reset:true,read_delay:1});await navigate('自动化');
await expect("document.querySelector('.content > section [aria-busy=true]')?.innerText==='正在读取自动化状态…' && !document.querySelector('.empty-state')",'automation initial load is distinct from empty state');await ready();
await expect("document.querySelector('.empty-state h2')?.innerText==='暂无自动化规则'",'automation empty state appears after successful read');await fixture({read_delay:0});
await prepareRule('响应丢失的规则','daily');await fixture({next_write:'drop_after'});await tap('保存规则');
await expect("document.querySelector('.scope-note[role=status]')?.innerText.includes('结果尚未确认') && [...document.querySelectorAll('.inline-form input,.inline-form select,.inline-form textarea')].every(e=>e.disabled)",'unknown rule creation freezes the submitted definition');
await navigate('工具');await navigate('自动化');await ready();
await expect("document.querySelector('.inline-form input')?.value==='响应丢失的规则' && document.querySelector('.inline-form button.primary')?.innerText==='重试保存同一规则'",'pending daily rule restores after navigation');
await tap('重试保存同一规则');await expect("document.querySelectorAll('.record-card').length===1 && !document.querySelector('.inline-form')",'rule replay acknowledges one saved rule');
const ruleRetry=await fixture({});const rulePosts=ruleRetry.requests.filter(r=>r.path==='/api/v1/automation/rules' && r.method==='POST');
if(ruleRetry.rules.length!==1 || rulePosts.length!==2 || JSON.stringify(rulePosts[0].body)!==JSON.stringify(rulePosts[1].body) || !rulePosts[0].body.requestKey)throw new Error('Rule retry changed key or definition');results.push('rule replay preserves exact key and daily definition');
await fixture({read_mode:'error',read_delay:1});await tap('刷新');
await expect("!!document.querySelector('.content > section [aria-busy=true]') && !document.querySelector('.record-card') && !document.querySelector('.empty-state')",'manual automation read clears stale actions');
await expect("!!document.querySelector('.content > section > [role=alert]') && !document.querySelector('.record-card input') && !document.querySelector('.empty-state')",'automation failure is not an empty rules list');
await fixture({read_mode:'success',read_delay:0});await tap('刷新');await expect("document.querySelectorAll('.record-card').length===1",'automation read recovers');
for(const mode of ['drop_before','deny','error','invalid']){
  await fixture({reset:true});await automation();await prepareRule('规则 '+mode);await fixture({next_write:mode});await tap('保存规则');
  if(mode==='deny'){
    await expect("!!document.querySelector('.content > .feedback.error') && !document.querySelector('.inline-form input')?.disabled",'explicit rule rejection releases the draft');
    await wait(5.3);await expect("document.querySelector('.content > .feedback.error')?.innerText.includes('请检查填写的内容')",'background refresh preserves a mutation rejection until user action');
    await fillInput('.inline-form input','拒绝后修改');await tap('保存规则');
    await expect("document.querySelectorAll('.record-card').length===1",'edited rule can be created after rejection');
    const state=await fixture({});const posts=state.requests.filter(r=>r.method==='POST');if(posts[0].body.requestKey===posts[1].body.requestKey)throw new Error('Rejected rule key reused');results.push('rejected rule creation starts a new identity');
  }else{
    await expect("document.querySelector('.inline-form button.primary')?.innerText==='重试保存同一规则'",'unknown rule result retains a retry identity');
    if(mode==='drop_before'){
      await tap('刷新');await ready();await wait(5.2);if((await fixture({})).requests.filter(r=>r.method==='POST').length!==1)throw new Error('Automation polling replayed creation');results.push('automation refresh and polling never replay creation');
    }
    if(mode==='drop_before'){await fixture({next_write:'deny'});await tap('重试保存同一规则');await expect("document.querySelector('.inline-form input')?.disabled && document.querySelector('.inline-form button.primary')?.innerText==='重试保存同一规则' && !document.querySelector('.inline-form button.primary').disabled",'a rejection after an unknown rule result cannot release the definition');}
    await tap('重试保存同一规则');await expect("document.querySelectorAll('.record-card').length===1 && !document.querySelector('.inline-form')",'explicit rule retry acknowledges one rule');
    const state=await fixture({});const posts=state.requests.filter(r=>r.method==='POST');if(posts.length!==(mode==='drop_before'?3:2) || new Set(posts.map(r=>JSON.stringify(r.body))).size!==1)throw new Error('Rule retry body changed');results.push('rule retry uses the original body after '+mode);
  }
}
await fixture({reset:true});await automation();await prepareRule('慢速规则');await fixture({write_delay:1});await tap('保存规则');
await expect("document.querySelector('.inline-form button.primary')?.disabled && document.querySelector('.inline-form input')?.disabled",'in-flight rule creation prevents duplicate input');await js("document.querySelector('.inline-form button.primary').click()");
await expect("document.querySelectorAll('.record-card').length===1",'slow rule creation finishes');if((await fixture({})).requests.filter(r=>r.method==='POST').length!==1)throw new Error('Duplicate rule creation');results.push('duplicate click posts one rule');
await fixture({write_delay:0});
const state=await fixture({});const rule={...state.rules[0],enabled:true,version:2};
const disabled={...rule,ruleId:'00000000-0000-0000-0000-000000000001',name:'停用规则',enabled:false};const comment={...rule,ruleId:'00000000-0000-0000-0000-000000000002',name:'评论规则',trigger:'message.received',action:'analyze_public_comment',enabled:false};
const run=(id,ruleId,version,state)=>({runId:id,ruleId,ruleVersion:version,name:'隔离规则任务',state,attempts:3,lastError:'execution_interrupted',createdAt:'2026-11-13T10:00:00Z',result:null});
await fixture({rules:[rule,disabled,comment],ai:false,runs:[run('00000000-0000-0000-0000-000000000011',rule.ruleId,2,'failed'),run('00000000-0000-0000-0000-000000000012',disabled.ruleId,2,'failed'),run('00000000-0000-0000-0000-000000000013',rule.ruleId,1,'failed'),run('00000000-0000-0000-0000-000000000014',rule.ruleId,2,'executing')]});await tap('刷新');await ready();
await expect("document.querySelectorAll('.automation-history tbody button').length===1",'retry only appears for enabled matching-version failed runs');
await expect("document.querySelectorAll('.record-card input[type=checkbox]')[2]?.disabled && !document.querySelectorAll('.record-card input[type=checkbox]')[0]?.disabled",'missing AI blocks comment enablement but permits disabling active rules');
await tap('重试');await expect("!document.querySelector('.automation-history tbody button') && document.querySelector('.automation-history tbody .badge')?.innerText==='等待处理'",'run retry returns to the automation queue');
await js("document.querySelector('.record-card input[type=checkbox]').click()");await ready();await expect("!document.querySelector('.record-card input[type=checkbox]')?.checked",'enabled rule can be explicitly disabled');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390",'automation fits a 390 CSS pixel viewport');await js("document.querySelector('.content > section').scrollIntoView({block:'start'})");cliLog({mobileScreenshot:await captureScreenshot()});await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({read_delay:1});await tap('刷新');await tap('退出登录','.sidebar');await wait(1.2);
await expect("!!document.querySelector('.login-card') && !document.querySelector('.record-card')",'logout discards delayed automation reads');
await fixture({reset:true});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'session can reconnect for late creation regression');await automation();await prepareRule('迟到规则');await fixture({next_write:'drop_after',write_delay:1});await tap('保存规则');await expect("document.querySelector('.inline-form input')?.disabled",'rule creation is in flight before logout');await tap('退出登录','.sidebar');await wait(1.2);await expect("!!document.querySelector('.login-card') && document.querySelector('.token-label input').value===''",'late rule creation cannot restore the logged-out session');await fixture({write_delay:0});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar') && !document.querySelector('.inline-form')",'logout discards unknown rule draft without replay');await tools();
await fixture({next_write:'drop_after',write_delay:1});await tap('查询');await expect("document.querySelector('.inline-form button.primary')?.disabled",'tool creation is in flight before logout');await tap('退出登录','.sidebar');await wait(1.2);
await expect("!!document.querySelector('.login-card') && document.querySelector('.token-label input').value==='' && !document.querySelector('.content')",'late tool completion cannot restore credentials or session');
await fixture({write_delay:0});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'session reconnects after discarded tool request');await tools();
await expect("document.querySelector('.inline-form button.primary')?.innerText==='查询' && !document.querySelector('.scope-note[role=status]')",'logout removes unknown tool identity without replay');
await fixture({read_mode:'unauthorized'});await tap('刷新');await expect("!!document.querySelector('.login-card') && !document.querySelector('.record-card')",'tool 401 clears the session');
cliLog({passed:results.length,results});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS operations regression',{keep:false}));
EOF
