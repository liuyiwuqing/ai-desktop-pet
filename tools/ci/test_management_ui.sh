#!/usr/bin/env bash
# Actual Vue bundle in Ego; HTTP is an isolated boundary fixture, not the real CORE.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-management"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/management_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const base='http://127.0.0.1:18799';
const name='NEXUS management regression';
const listed=await listTaskSpaces();
const existing=listed.find(space=>space.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing regression space needs user confirmation before control');
const task=await useOrCreateTaskSpace(name);
cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value) {
  return JSON.parse(await serverFetch(base+'/__qa/state',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));
}
async function state() {return JSON.parse(await serverFetch(base+'/__qa/state',{headers:{'X-QA-Key':'isolated-ui-fixture-control'}}));}
async function expect(expression,label) {
  for(let i=0;i<50;i++) {if(await js(expression)){results.push(label);return;} await wait(0.1);}
  throw new Error('UI assertion failed: '+label);
}
async function tap(text) {
  const found=await js(`(()=>{const button=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!button||button.disabled)return false;button.scrollIntoView({block:'center'});button.click();return true;})()`);
  if(!found)throw new Error('Button unavailable: '+text);
}
async function range(index,value) {
  await js(`(()=>{const input=document.querySelectorAll('.management-form input[type=range]')[${index}];input.value=${JSON.stringify(String(value))};input.dispatchEvent(new Event('input',{bubbles:true}));return input.value;})()`);
}
await fixture({reset:true});
await openOrReuseTab(base,{wait:true,timeout:15});
await gotoAndWait(base,{timeout:15});
await expect("!!document.querySelector('.login-card')",'login screen');
await fillInput('.token-label input','incorrect-fixture-token');
await click('.login-card button.primary');
await expect("document.body.innerText.includes('管理令牌无效')",'invalid token is rejected');
await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');
await click('.login-card button.primary');
await expect("!!document.querySelector('.sidebar')",'authenticated app');
await expect("document.body.innerText.includes('内容已按保留期限清除')",'purged message list explains removal');
await click('.message-row');
await expect("document.querySelector('.message-detail')?.innerText.includes('清除正文和分析结果') && !document.querySelector('.analysis-section')",'purged message hides analysis');
await click('.sidebar nav button:nth-child(3)');
await tap('修改名称');
await fillInput('.device-name-form input','书房伙伴');
await fixture({deviceName:'另一窗口的名称'});await tap('刷新');
await expect("document.querySelector('.device-name-form input').value==='书房伙伴' && document.querySelector('.device-name-form').innerText.includes('你的编辑已保留')",'rename draft survives concurrent change');
await tap('加载当前名称');
await expect("document.querySelector('.device-name-form input').value==='另一窗口的名称'",'rename explicitly loads current name');
await fillInput('.device-name-form input','书房伙伴');await fixture({renameReadDelay:1});await tap('保存名称');
await expect("document.querySelector('.device-name-heading h2')?.innerText==='书房伙伴' && document.querySelector('.device-name-form input')?.disabled && !document.querySelector('.device-name-form')?.innerText.includes('你的编辑已保留')",'own rename readback never shows a conflict while saving');
await expect("!document.querySelector('.device-name-form') && document.querySelector('.device-name-heading h2').innerText==='书房伙伴'",'rename saves and closes editor');
let renameState=await state();
if(renameState.renameSubmissions.length!==1 || renameState.renameSubmissions[0].expectedName!=='另一窗口的名称')throw new Error('Rename used stale expected name');
results.push('rename submits current original name');
await tap('修改名称');await fillInput('.device-name-form input','客厅伙伴');
await fixture({dropRenameNext:true});await tap('保存名称');
await expect("document.body.innerText.includes('操作结果尚未确认') && document.querySelector('.device-name-form input').value==='客厅伙伴'",'unknown rename preserves draft and explains uncertainty');
await tap('刷新');
await expect("document.querySelector('.device-name-heading h2').innerText==='客厅伙伴' && document.querySelector('.device-name-form').innerText.includes('你的编辑已保留')",'rename readback resolves unknown result without resending');
renameState=await state();if(renameState.renameSubmissions.length!==2)throw new Error('Rename was unexpectedly replayed');
results.push('unknown rename never resends automatically');
await tap('加载当前名称');await tap('取消');
await tap('设置与状态');
await expect("document.querySelector('#device-management-panel')?.innerText.includes('尚未收到设备状态')",'empty device state');
await fixture({mode:'fresh'});await tap('刷新状态');
await expect("document.querySelectorAll('.management-form input[type=range]').length===2",'fresh readback is editable');
await range(0,60);await fixture({advance:true});await tap('刷新状态');
await expect("document.body.innerText.includes('你的编辑仍保留') && document.querySelector('.management-form input[type=range]').value==='60' && document.querySelector('.management-form fieldset').disabled",'dirty draft survives changed revision');
await tap('加载设备新设置');
await expect("document.querySelector('.management-form input[type=range]').value==='35' && !document.querySelector('.management-form fieldset').disabled",'explicit reload adopts new revision');
await range(1,0);
await expect("document.body.innerText.includes('亮度为 0 将熄屏')",'brightness zero consequence is visible');
await tap('保存到设备');
await expect("document.body.innerText.includes('等待下发')",'submission shows pending');
let observed=await state();
if(observed.submissions.length!==1 || observed.submissions[0].expectedRevision!==2 || observed.submissions[0].config.brightness!==0)throw new Error('Wrong submitted revision/config');
results.push('submitted draft and revision match');
await fixture({apply:true});await tap('刷新状态');
await expect("document.body.innerText.includes('设备已保存')",'device confirmation is distinct from submission');
await tap('查看操作记录');
await expect("document.querySelector('.audit-list')?.innerText.includes('管理员提交')",'audit history is readable');
await range(0,61);await fixture({dropNext:true});await tap('保存到设备');
await expect("document.body.innerText.includes('提交结果尚未确认') && [...document.querySelectorAll('button')].some(b=>b.innerText.trim()==='重试同一请求')",'unknown HTTP outcome retains retry');
await tap('重试同一请求');
await expect("document.querySelectorAll('.management-record').length===2 && !document.body.innerText.includes('提交结果尚未确认')",'retry resolves after acknowledgment');
observed=await state();
if(observed.submissions.length!==3 || observed.submissions[1].requestId!==observed.submissions[2].requestId)throw new Error('Retry changed the request ID');
results.push('unknown outcome retries identical request ID');
await fixture({mode:'readonly'});await tap('刷新状态');
await expect("document.body.innerText.includes('设备存储不可写') && document.querySelector('.management-form fieldset').disabled",'readonly storage disables editing');
await fixture({mode:'stale'});await tap('刷新状态');
await expect("document.body.innerText.includes('状态已过期') && document.querySelector('.management-form fieldset').disabled",'stale readback disables editing');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("window.innerWidth===390 && document.documentElement.scrollWidth<=390",'390 CSS px has no horizontal overflow');
await cdp('Emulation.clearDeviceMetricsOverride');
await js("[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith('记忆')).click()");
await expect("!!document.querySelector('.memory-search input') && document.body.innerText.includes('待删除')",'memory query and deletion queue are visible');
await fillInput('.memory-search input','公开评论');await tap('检索');
await expect("document.querySelectorAll('.memory-results article').length===1 && document.querySelector('.memory-results').innerText.includes('<img src=x') && !document.querySelector('.memory-results img') && !document.querySelector('.memory-results a')",'memory uses plain text and rejects unsafe source links');
await fixture({memoryMode:'error'});await tap('检索');
await expect("document.body.innerText.includes('记忆检索失败') && !document.querySelector('.memory-results article') && document.querySelector('.memory-search input').value==='公开评论'",'search failure clears earlier results and preserves query');
await fixture({memoryMode:'partial'});await tap('检索');
await expect("document.body.innerText.includes('部分来源暂时无法核验') && document.querySelectorAll('.memory-results article').length===1",'partial verification is explained');
await fixture({memoryMode:'empty'});await tap('检索');
await expect("document.body.innerText.includes('没有可返回的公开评论') && !document.querySelector('.memory-results article')",'empty search has clear feedback');
await fixture({memoryMode:'delayed'});await tap('检索');
await expect("document.querySelector('.memory-search button').disabled && document.querySelector('.memory-search input').disabled",'search in progress prevents duplicate submission');
await expect("!document.querySelector('.memory-search button').disabled && document.querySelectorAll('.memory-results article').length===1",'search completion restores controls');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("window.innerWidth===390 && document.documentElement.scrollWidth<=390",'memory results fit 390 CSS px');
await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({memoryMode:'disabled'});await tap('刷新记忆状态');
await expect("document.body.innerText.includes('长期记忆未启用') && document.querySelector('.memory-search button').disabled && !document.querySelector('.memory-results article')",'disabled memory clears results and prevents queries');
await tap('退出登录');
await expect("!!document.querySelector('.login-card') && document.querySelector('.token-label input').value===''",'logout clears page token');
cliLog({passed:results.length,checks:results,scope:'actual Vue bundle; HTTP boundary fixture; no real database, broker, cloud or device'});
EOF
# Completion is deliberately a separate final heredoc, after all browser assertions succeed.
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS management regression',{keep:false}));
EOF
