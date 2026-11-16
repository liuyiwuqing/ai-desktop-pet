#!/usr/bin/env bash
# Actual Vue console with an explicit RAM-only HTTP list fixture.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-list-filters"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/management_ui_fixture.py" --frontend "$nexus_dir/target/frontend" --port 18805 >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const base='http://127.0.0.1:18805', name='NEXUS list filter regression';
const existing=(await listTaskSpaces()).find(space=>space.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing regression space needs user confirmation before control');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value) {return JSON.parse(await serverFetch(base+'/__qa/state',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label) {
  for(let i=0;i<60;i++){if(await js(expression).catch(()=>false)){results.push(label);return;}await wait(0.1);}
  cliLog(await snapshotText());throw new Error('UI assertion failed: '+label);
}
async function state() {return JSON.parse(await serverFetch(base+'/__qa/state',{headers:{'X-QA-Key':'isolated-ui-fixture-control'}}));}
async function tap(text) {
  // 面板由按需加载的 chunk 渲染，按钮可能晚一拍出现；有限次重试，断言仍是"存在且可点"。
  for(let i=0;i<50;i++) {
    if(await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))return;
    await wait(0.1);
  }
  throw new Error('Button unavailable: '+text);
}
async function navigate(tab) {
  // Viewport emulation can leave cached mouse coordinates in the browser helper.
  // Use the public DOM button, without accessing component internals.
  await js(`document.querySelector('.sidebar nav button[data-tab=${tab}]').click()`);
}
async function choose(label,value) {
  const selector=`select[aria-label="${label}"]`;
  await expect(`!!document.querySelector(${JSON.stringify(selector)}) && !document.querySelector(${JSON.stringify(selector)}).disabled`,'filter is enabled: '+label+'/'+value);
  await js(`(()=>{const s=document.querySelector(${JSON.stringify(selector)});s.value=${JSON.stringify(value)};s.dispatchEvent(new Event('change',{bubbles:true}));})()`);
}
await fixture({reset:true,listReset:true});
await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');

await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("!!document.querySelector('.sidebar') && document.querySelectorAll('.message-row').length===30",'default message list is unchanged');
await expect("!!document.querySelector('select[aria-label=消息状态]')",'message state filter is available');
if((await state()).listRequests.some(path=>path.includes('readStatus=')||path.includes('status=')))throw new Error('Default lists must omit the optional filters');
results.push('default HTTP requests omit filters');
await choose('消息状态','unread');
await expect("document.querySelector('.section-heading p')?.innerText==='31 条消息' && document.querySelectorAll('.message-row').length===30 && document.querySelectorAll('.message-row:not(.unread)').length===0 && document.querySelector('.pagination > span')?.innerText==='1 / 2'",'unread filter shares rows, total and pagination');
await tap('下一页');
await expect("document.querySelectorAll('.message-row').length===1 && document.querySelector('.message-title')?.innerText==='未读消息 30' && document.querySelector('.pagination > span')?.innerText==='2 / 2'",'filtered message next page is accurate');
await choose('消息状态','read');
await expect("document.querySelector('.section-heading p')?.innerText==='2 条消息' && document.querySelectorAll('.message-row').length===2 && document.querySelectorAll('.message-row.unread').length===0 && document.querySelector('.pagination > span')?.innerText==='1 / 1'",'switching filter resets the message page');
await navigate('deliveries');
await expect("document.querySelector('h1')?.innerText==='投递' && document.querySelectorAll('tbody tr').length===30",'default delivery list is unchanged');
for(const [value,name,total] of [['pending','等待投递',1],['sent','等待设备确认',1],['received','设备已保存',1],['displayed','设备已展示',1],['read','用户已读',1],['expired','已结束',1],['failed','投递失败',31]]) {
  await choose('投递状态',value);
  await expect(`document.querySelector('.section-heading p')?.innerText.startsWith('${total} 条投递') && document.querySelectorAll('tbody tr').length===${Math.min(total,30)} && [...document.querySelectorAll('tbody .badge')].every(b=>b.innerText===${JSON.stringify(name)})`,'delivery filter: '+value);
}
await navigate('inbox');
await expect("document.querySelector('select[aria-label=消息状态]')?.value==='read' && document.querySelectorAll('.message-row').length===2",'message filter survives navigation');
await tap('刷新');
await expect("!document.querySelector('.list-filters select').disabled && document.querySelector('.list-filters select').value==='read' && document.querySelectorAll('.message-row').length===2",'manual refresh retains the message filter');
await choose('消息状态','unread');
await expect("document.querySelector('.pagination > span')?.innerText==='1 / 2'",'unread list is ready for last-page action');await tap('下一页');
await expect("document.querySelectorAll('.message-row').length===1 && document.querySelector('.pagination > span')?.innerText==='2 / 2'",'last message page contains one row');
await click('.message-row');await tap('标记已读');
await expect("document.querySelector('.section-heading p')?.innerText==='30 条消息' && document.querySelector('.pagination > span')?.innerText==='1 / 1' && document.querySelectorAll('.message-row').length===30 && document.querySelector('.list-filters select').value==='unread' && !document.body.innerText.includes('未读消息 30')",'marking the final filtered row read reloads the valid page');
await navigate('deliveries');
await expect("document.querySelector('select[aria-label=投递状态]')?.value==='failed' && document.querySelector('.pagination > span')?.innerText==='1 / 2'",'delivery filter survives navigation');await tap('下一页');
await expect("document.querySelectorAll('tbody tr').length===1 && document.querySelector('.pagination > span')?.innerText==='2 / 2'",'last delivery page contains one failed row');
await click('tbody button');
await expect("document.querySelector('.section-heading p')?.innerText.startsWith('30 条投递') && document.querySelector('.pagination > span')?.innerText==='1 / 1' && document.querySelectorAll('tbody tr').length===30 && document.querySelector('.list-filters select').value==='failed'",'retrying the final failed row reloads the valid page');
await choose('投递状态','pending');
await expect("document.querySelectorAll('tbody tr').length===2 && [...document.querySelectorAll('tbody .badge')].every(b=>b.innerText==='等待投递')",'retried delivery appears in pending results');
await fixture({listError:'deliveries'});await choose('投递状态','expired');
await expect("document.querySelector('h2')?.innerText==='投递读取失败' && document.querySelector('.list-filters select').value==='expired' && document.querySelectorAll('tbody tr').length===0 && !document.body.innerText.includes('没有符合条件的投递')",'failed delivery filter does not show stale rows or a false empty result');
await fixture({listError:''});await tap('重试');
await expect("document.querySelectorAll('tbody tr').length===1 && document.querySelector('.list-filters select').value==='expired' && !document.querySelector('.feedback.error')",'delivery read failure recovers with its filter');
await fixture({listDelay:1});await choose('投递状态','failed');
await expect("document.querySelector('.empty-state[role=status]')?.innerText==='正在加载投递…' && document.querySelector('.list-filters select').disabled && !document.body.innerText.includes('没有符合条件的投递')",'delivery loading is distinct from empty or failed');
await expect("document.querySelectorAll('tbody tr').length===30 && !document.querySelector('.list-filters select').disabled",'delayed delivery filter finishes');await fixture({listDelay:0});
await choose('投递状态','expired');await expect("document.querySelectorAll('tbody tr').length===1 && !document.querySelector('.list-filters select').disabled",'delivery filter restores the narrow result');
await navigate('inbox');await fixture({listError:'messages'});await choose('消息状态','read');
await expect("document.querySelector('h2')?.innerText==='消息读取失败' && document.querySelector('.list-filters select').value==='read' && document.querySelectorAll('.message-row').length===0 && !document.body.innerText.includes('没有符合条件的消息')",'failed message filter does not show stale rows or a false empty result');
await fixture({listError:''});await tap('重试');
await expect("document.querySelectorAll('.message-row').length===3 && document.querySelector('.list-filters select').value==='read' && !document.querySelector('.feedback.error')",'message read failure recovers with its filter');
await fixture({listDelay:1});await choose('消息状态','unread');
await expect("document.querySelector('.empty-state[role=status]')?.innerText==='正在加载消息…' && document.querySelector('.list-filters select').disabled && !document.body.innerText.includes('没有符合条件的消息')",'loading a different filter is distinct from empty or failed');
await expect("document.querySelectorAll('.message-row').length===30 && !document.querySelector('.list-filters select').disabled",'delayed message filter finishes');await fixture({listDelay:0});
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && document.querySelector('.list-filters select').getBoundingClientRect().right<=390",'message filter fits 390 CSS pixels');
await navigate('deliveries');
await expect("document.querySelector('h1')?.innerText==='投递' && document.documentElement.scrollWidth<=390 && document.querySelector('.list-filters select').getBoundingClientRect().right<=390 && document.querySelectorAll('tbody tr').length===1",'delivery filter fits 390 CSS pixels with a scrollable table');
await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({listEmpty:true});await tap('刷新');
await expect("document.querySelector('h2')?.innerText==='没有符合条件的投递' && document.querySelector('.list-filters select').value==='expired'",'empty filtered delivery result is explicit');
await choose('投递状态','');await expect("document.querySelector('h2')?.innerText==='还没有投递记录'",'unfiltered delivery empty state is preserved');
await navigate('inbox');
await expect("document.querySelector('h2')?.innerText==='没有符合条件的消息' && document.querySelector('.list-filters select').value==='unread'",'empty filtered message result is explicit');
await choose('消息状态','');await expect("document.querySelector('h2')?.innerText==='消息会从这里开始'",'unfiltered message empty state is preserved');
await fixture({listDelay:1});await choose('消息状态','read');
await expect("document.querySelector('.empty-state[role=status]')?.innerText==='正在加载消息…'",'a filtered request is in flight before logout');await tap('退出登录');
await expect("!!document.querySelector('.login-card')",'logout ends the filtered session');
await fixture({listDelay:0});await wait(1.2);
await expect("!!document.querySelector('.login-card') && !document.querySelector('.sidebar') && document.querySelector('.token-label input').value===''",'late filtered response cannot restore a logged-out session');
await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("document.querySelector('select[aria-label=消息状态]')?.value===''",'new login resets the message filter');
await navigate('deliveries');await expect("document.querySelector('select[aria-label=投递状态]')?.value===''",'new login resets the delivery filter');
const requests=(await state()).listRequests;
if(!requests.includes('/api/v1/messages?page=2&readStatus=unread') || !requests.includes('/api/v1/deliveries?page=2&status=failed'))throw new Error('Pagination lost its HTTP filter');
results.push('public pagination requests retain both filter parameters');
cliLog({passed:results.length,checks:results,scope:'actual Vue and RAM HTTP boundary; no database, broker or device'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS list filter regression',{keep:false}));
EOF
