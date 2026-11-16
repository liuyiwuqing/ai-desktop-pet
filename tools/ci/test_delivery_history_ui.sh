#!/usr/bin/env bash
# Actual Vue and RAM HTTP fixture; no real CORE/database/Broker/device.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-delivery-history"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/delivery_history_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const base='http://127.0.0.1:18807',name='NEXUS delivery history regression';
const existing=(await listTaskSpaces()).find(s=>s.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing space needs user confirmation');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value){return JSON.parse(await serverFetch(base+'/__qa/deliveries',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<60;i++){if(await js(expression)){results.push(label);return;}await wait(0.1);}cliLog(await snapshotText());throw new Error('UI assertion failed: '+label);}
async function tap(text){if(!await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))throw new Error('Button unavailable: '+text);}
async function navigate(label){await js(`(()=>{const b=[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)}));if(!b)throw new Error('Navigation missing');b.click();})()`);}
async function history(index=0){await js(`(()=>{const row=document.querySelectorAll('tbody tr')[${index}];const b=[...row.querySelectorAll('button')].find(b=>b.innerText.trim()==='时间线');if(!b)throw new Error('History action missing');b.click();})()`);await expect("!!document.querySelector('.delivery-history-panel .management-status .badge')",'history snapshot loaded');}
async function filter(value){await js(`(()=>{const s=document.querySelector('select[aria-label=投递状态]');s.value=${JSON.stringify(value)};s.dispatchEvent(new Event('change',{bubbles:true}));})()`);await expect("!document.querySelector('select[aria-label=投递状态]').disabled",'delivery filter is ready');}
async function reset(){await fixture({reset:true});if(await js("!!document.querySelector('.delivery-history-panel')"))await tap('收起时间线');await filter('');await tap('刷新');await expect("document.querySelectorAll('tbody tr').length===4 && !document.querySelector('.list-filters select').disabled",'fixture starts with four deliveries');}
await fixture({reset:true});await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');

await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("!!document.querySelector('.sidebar')",'login succeeds');await navigate('投递');
await expect("document.querySelectorAll('tbody tr').length===4 && [...document.querySelectorAll('tbody button')].some(b=>b.innerText.trim()==='时间线')",'each delivery exposes a timeline action');
await history();
await expect("document.querySelectorAll('.delivery-timeline li').length===1 && document.querySelector('.delivery-timeline')?.innerText.includes('建立投递') && document.activeElement?.classList.contains('delivery-history-panel')",'creation is recorded and history receives keyboard focus');
await tap('收起时间线');await expect("document.activeElement?.id==='delivery-history-delivery-history-0'",'closing history restores the originating action focus');
await history(1);
await expect("document.querySelector('.delivery-history-panel')?.innerText.includes('此前历史未知') && ![...document.querySelectorAll('.delivery-history-panel button')].some(b=>b.innerText.trim()==='取消投递')",'baseline is honest and a retried/legacy pending row cannot be cancelled');
await tap('收起时间线');await history(3);
await expect("document.querySelector('.delivery-history-panel .management-status .badge')?.innerText==='设备已展示' && ![...document.querySelectorAll('.delivery-history-panel button')].some(b=>b.innerText.trim()==='取消投递')",'displayed facts have no cancel action');
await tap('收起时间线');await filter('pending');await history();await tap('取消投递');
await expect("document.querySelector('.delivery-cancel-confirm')?.innerText.includes('不删除消息，也不影响其他设备')",'cancellation explains its per-delivery consequence');
if((await fixture({})).requests.some(r=>r.method==='POST'))throw new Error('Opening confirmation sent a cancel');results.push('opening confirmation does not mutate');
await tap('暂不取消');await expect("!document.querySelector('.delivery-cancel-confirm')",'confirmation can be dismissed');await tap('取消投递');await tap('确认取消');
await expect("document.querySelector('.delivery-history-panel .management-status .badge')?.innerText==='已取消' && document.querySelectorAll('.delivery-timeline li').length===2 && document.querySelectorAll('tbody tr').length===1 && document.querySelector('.list-filters select').value==='pending'",'cancellation is acknowledged, history appended and filtered list refreshed');
const first=await fixture({});if(first.requests.filter(r=>r.method==='POST').length!==1 || first.rows.slice(1).some(r=>r.status==='cancelled'))throw new Error('Cancel affected another delivery or repeated');results.push('only one selected delivery is cancelled');
await tap('收起时间线');await expect("document.activeElement?.getAttribute('aria-label')==='投递状态'",'closing a filtered-out delivery returns focus to filter');
await filter('cancelled');await expect("document.querySelectorAll('tbody tr').length===1 && document.querySelector('tbody .badge')?.innerText==='已取消'",'cancelled status has its own filtered result');
await reset();await history();await fixture({cancel_delay:1});await tap('取消投递');await tap('确认取消');
await expect("[...document.querySelectorAll('.delivery-cancel-confirm button')].some(b=>b.innerText==='正在取消…' && b.disabled) && document.querySelector('.delivery-history-panel button').disabled",'in-flight cancellation blocks duplicate submission');
await js("document.querySelector('.delivery-cancel-confirm button').click()");
await expect("document.querySelector('.delivery-history-panel .management-status .badge')?.innerText==='已取消'",'delayed cancellation finishes');
if((await fixture({})).requests.filter(r=>r.method==='POST').length!==1)throw new Error('Duplicate click resubmitted cancel');results.push('repeat click creates one cancellation');
await reset();await history();await fixture({next_cancel:'drop_after'});await tap('取消投递');await tap('确认取消');
await expect("document.querySelector('.delivery-history-panel')?.innerText.includes('已核对：投递已取消') && !document.querySelector('.delivery-cancel-confirm')",'lost response after cancellation is resolved by read-only lookup');
if((await fixture({})).requests.filter(r=>r.method==='POST').length!==1)throw new Error('Read-only lookup replayed cancel');results.push('saved response loss never replays automatically');
await reset();await history();await fixture({next_cancel:'drop_before'});await tap('取消投递');await tap('确认取消');
await expect("document.querySelector('.delivery-history-panel')?.innerText.includes('取消结果尚未确认') && [...document.querySelectorAll('.delivery-history-panel button')].some(b=>b.innerText.trim()==='重试取消同一投递')",'lost request keeps the original delivery identity and explicit retry');
await tap('刷新时间线');await expect("document.querySelector('.delivery-history-panel')?.innerText.includes('取消结果尚未确认') && !document.querySelector('.delivery-history-panel [aria-busy=true]')",'a pending lookup does not claim the missing cancel succeeded');
await wait(5.2);
if((await fixture({})).requests.filter(r=>r.method==='POST').length!==1)throw new Error('Refreshing replayed cancel');
await tap('重试取消同一投递');await expect("document.querySelector('.delivery-history-panel .management-status .badge')?.innerText==='已取消'",'explicit retry resolves cancellation');
const retry=(await fixture({})).requests.filter(r=>r.method==='POST');if(retry.length!==2 || retry[0].path!==retry[1].path)throw new Error('Cancel retry changed delivery identity');results.push('retry uses the same delivery URL');
await reset();await history();await fixture({next_cancel:'conflict'});await tap('取消投递');await tap('确认取消');
await expect("document.querySelector('.delivery-history-panel')?.innerText.includes('不能取消') && !document.querySelector('.delivery-cancel-confirm') && document.querySelectorAll('.delivery-timeline li').length===2",'claim conflict refreshes state and removes cancellation');
await reset();await fixture({many:true});await history();
await expect("document.querySelectorAll('.delivery-timeline li').length===30 && document.querySelector('.delivery-history-panel .pagination > span')?.innerText==='1 / 2'",'history is bounded and paginated');
await js("[...document.querySelectorAll('.delivery-history-panel .pagination button')].find(b=>b.innerText==='下一页').click()");
await expect("document.querySelectorAll('.delivery-timeline li').length===1 && document.querySelector('.delivery-history-panel .pagination > span')?.innerText==='2 / 2'",'second history page contains the remaining event');
await fixture({history_mode:'error'});await tap('刷新时间线');
await expect("!!document.querySelector('.delivery-history-panel [role=alert]') && !document.querySelector('.delivery-timeline')",'history read failure hides stale events');
await fixture({history_mode:'empty'});await tap('刷新时间线');await expect("document.querySelector('.delivery-history-panel')?.innerText.includes('尚无可读取的变化记录') && !document.querySelector('.delivery-history-panel [role=alert]') && document.querySelector('.delivery-history-panel .pagination > span')?.innerText==='1 / 1'",'empty history is distinct from failure');
await fixture({history_mode:'success',history_delay:1});await tap('刷新时间线');
await expect("document.querySelector('.delivery-history-panel [aria-busy=true]')?.innerText==='正在读取时间线…' && document.querySelector('.delivery-history-panel button').disabled",'history loading prevents duplicate reads');
await expect("document.querySelectorAll('.delivery-timeline li').length===30",'history refresh recovers');await fixture({history_delay:0});
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && document.querySelector('.delivery-history-panel').getBoundingClientRect().right<=390",'history fits 390 CSS pixels');await js("document.querySelector('.delivery-history-panel').scrollIntoView({block:'start'})");await wait(0.1);cliLog({mobileScreenshot:await captureScreenshot()});await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({history_delay:1});await tap('刷新时间线');await tap('收起时间线');await wait(1.2);
await expect("!document.querySelector('.delivery-history-panel')",'late history response cannot reopen a closed panel');
await fixture({history_delay:0});await history();await fixture({history_delay:1});await tap('刷新时间线');await tap('退出登录');await wait(1.2);
await expect("!!document.querySelector('.login-card') && !document.querySelector('.delivery-history-panel')",'logout discards delayed delivery data');
await fixture({reset:true});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("!!document.querySelector('.sidebar') && document.querySelectorAll('tbody tr').length===4",'session may return before cancellation logout');
await history();await fixture({cancel_delay:1});await tap('取消投递');await tap('确认取消');
await expect("[...document.querySelectorAll('.delivery-cancel-confirm button')].some(b=>b.innerText==='正在取消…')",'cancellation is in flight before logout');
await tap('退出登录');await wait(1.2);
await expect("!!document.querySelector('.login-card') && !document.querySelector('.delivery-history-panel') && document.querySelector('.token-label input').value===''",'late cancellation cannot restore session data or notice');
if((await fixture({})).requests.filter(r=>r.method==='POST').length!==1)throw new Error('Logout replayed cancellation');results.push('logout never replays an in-flight cancel');
await fixture({history_delay:0,history_mode:'unauthorized'});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'new session may log in');
await expect("document.querySelectorAll('tbody tr').length===4",'new session delivery list is ready');await js("[...document.querySelectorAll('tbody tr')[0].querySelectorAll('button')].find(b=>b.innerText==='时间线').click()");
await expect("!!document.querySelector('.login-card') && document.querySelector('.token-label input').value===''",'history 401 ends the session');
cliLog({passed:results.length,checks:results,scope:'actual Vue and RAM-only HTTP; not database/Broker/device'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS delivery history regression',{keep:false}));
EOF
