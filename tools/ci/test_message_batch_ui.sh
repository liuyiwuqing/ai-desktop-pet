#!/usr/bin/env bash
# Actual Vue console; the per-message delivery batch endpoints are the only fixtures.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-message-batch"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/management_ui_fixture.py" --frontend "$nexus_dir/target/frontend" --port 18827 >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
{
cat "$repo_root/tools/ci/naive_ui_helpers.js"
cat <<'EOF'
async function waitForLogin(){for(let i=0;i<120;i++){if(await js("!!document.querySelector('.token-label input:not(:disabled)') && !document.querySelector('.login-card button.primary')?.disabled").catch(()=>false))return;await wait(.1);}throw new Error('Login form did not settle after session recovery');}
const base='http://127.0.0.1:18827', name='NEXUS message delivery batch regression';
const existing=(await listTaskSpaces()).find(space=>space.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing regression space needs user confirmation before control');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value) {return JSON.parse(await serverFetch(base+'/__qa/state',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function state() {return JSON.parse(await serverFetch(base+'/__qa/state',{headers:{'X-QA-Key':'isolated-ui-fixture-control'}}));}
async function expect(expression,label) {
  for(let i=0;i<60;i++){if(await js(expression).catch(()=>false)){results.push(label);return;}await wait(0.1);}
  cliLog(await snapshotText());throw new Error('UI assertion failed: '+label);
}
async function tap(text) {
  // 面板由按需加载的 chunk 渲染，按钮可能晚一拍出现；有限次重试，断言仍是"存在且可点"。
  for(let i=0;i<60;i++) {
    if(await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))return;
    await wait(0.1);
  }
  throw new Error('Button unavailable: '+text);
}
const batchText="document.querySelector('.delivery-batch')?.innerText||''";
async function reopen() { await tap('收起投递批量操作'); await tap('投递批量操作…'); }
await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});await fixture({reset:true,listReset:true});
await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("document.querySelectorAll('.message-row').length===30",'message list is ready for the detail view');
await js("document.querySelectorAll('.message-row')[0].click()");
await expect("!!document.querySelector('.message-detail h2') && [...document.querySelectorAll('button')].some(b=>b.innerText.trim()==='投递批量操作…')",'message detail offers the delivery batch operation');

await tap('投递批量操作…');
await expect(`${batchText}.includes('可撤销 2 条') && ${batchText}.includes('可重试 1 条')`,'opening reads both plans without writing');
await expect(`${batchText}.includes('跳过 2 条：已经尝试过发送 1、已经开始投递 1')`,'cancel plan explains every skipped delivery by reason');
await expect(`${batchText}.includes('跳过 3 条') && ${batchText}.includes('不在失败状态 3')`,'retry plan explains its own skips');
const opened=(await state()).batchRequests;
if(opened.length!==2 || opened.some(request=>request.dryRun!==true || request.confirm!=='') || opened[0].action!=='cancel' || opened[1].action!=='retry')throw new Error('Opening must send exactly two dry runs without a confirm token');
results.push('opening sends two dry runs and no write');

await tap('确认撤销 2 条');
await expect(`${batchText}.includes('已撤销 2 条未开始的投递') && ${batchText}.includes('2 条未撤销，原因见下方核对')`,'cancel reports what it applied and what it did not');
await expect(`${batchText}.includes('没有可撤销的投递') && ![...document.querySelectorAll('.delivery-batch button')].some(b=>b.innerText.trim().startsWith('确认撤销'))`,'cancel plan is re-read after applying');
const written=(await state()).batchRequests.filter(request=>request.dryRun===false);
if(written.length!==1 || written[0].action!=='cancel' || written[0].confirm!=='CANCEL_MESSAGE_DELIVERIES')throw new Error('Applying must send the cancel confirm token exactly once');
results.push('cancel write carries its confirm token');

await tap('确认重试 1 条');
await expect(`${batchText}.includes('已把 1 条失败的投递重新加入队列') && ${batchText}.includes('没有可重试的投递')`,'retry applies and then shows no candidates');
const retried=(await state()).batchRequests.filter(request=>request.dryRun===false&&request.action==='retry');
if(retried.length!==1 || retried[0].confirm!=='RETRY_MESSAGE_DELIVERIES')throw new Error('Retry write must carry its own confirm token');
results.push('retry write carries its confirm token');

await fixture({batchMode:'empty'});await reopen();
await expect(`${batchText}.includes('共有 0 条投递记录') && ${batchText}.includes('没有可撤销的投递') && ${batchText}.includes('没有可重试的投递')`,'a message without deliveries is explicit instead of a zero plan');

await fixture({batchMode:'fail'});await reopen();
await expect("document.querySelector('.delivery-batch .status-note--danger')?.innerText.includes('HTTP 503') && !document.querySelector('.delivery-batch-plans')",'a failed read hides the plan instead of showing stale counts');

await fixture({batchMode:'normal'});await reopen();
await expect(`${batchText}.includes('可撤销 2 条')`,'batch recovers after a failed read');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && document.querySelector('.delivery-batch').getBoundingClientRect().right<=390 && [...document.querySelectorAll('.delivery-batch button')].every(b=>b.getBoundingClientRect().right<=390)",'delivery batch fits 390 CSS pixels');
await cdp('Emulation.clearDeviceMetricsOverride');
await tap('收起投递批量操作');
await expect(`!document.querySelector('.delivery-batch-plans') && ${batchText}.includes('投递批量操作…')`,'collapsing hides the plan again');
cliLog({passed:results.length,checks:results,scope:'actual Vue and RAM HTTP boundary; no database, broker or device'});
EOF
} | ego-browser nodejs
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS message delivery batch regression',{keep:false}));
EOF
