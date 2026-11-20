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
async function waitForLogin(){for(let i=0;i<120;i++){if(await js("!!document.querySelector('.token-label input:not(:disabled)') && !document.querySelector('.login-card button.primary')?.disabled").catch(()=>false))return;await wait(.1);}throw new Error('Login form did not settle after session recovery');}
const base='http://127.0.0.1:18806',name='NEXUS overview regression';
const existing=(await listTaskSpaces()).find(s=>s.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing space needs user confirmation');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value){return JSON.parse(await serverFetch(base+'/__qa/overview',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<60;i++){if(await js(expression).catch(()=>false)){results.push(label);return;}await wait(0.1);}cliLog(await snapshotText());throw new Error('UI assertion failed: '+label);}
async function tap(text){if(!await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))throw new Error('Button unavailable: '+text);}
async function navigate(label){await js(`(()=>{const b=[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)}));if(!b)throw new Error('Navigation missing');b.click();})()`);}
async function login(){await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login succeeds');}
await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');
await login();
await expect("[...document.querySelectorAll('.sidebar nav button')].some(b=>b.textContent.includes('概览'))",'overview navigation exists');
await navigate('概览');
await expect("document.querySelector('h1')?.innerText==='运行概览' && document.querySelector('.overview-panel')?.innerText.includes('25.0%')",'overview loads its own authenticated HTTP snapshot');
await expect("document.querySelector('.overview-panel')?.innerText.includes('2026-11-13 · Asia/Shanghai 当日起算') && document.querySelector('[data-stat=online]')?.textContent.trim().startsWith('2') && document.querySelector('[data-stat=offline]')?.textContent.trim().startsWith('3')",'the local day and configured zone are explicit rather than a sliced UTC date');
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7' && document.querySelector('[data-metric=backlog] dd')?.childNodes[0]?.textContent.trim()==='6' && document.querySelector('.overview-panel')?.innerText.includes('停用 1 台')",'message, backlog and overlapping disabled counts are distinct');
// 告警阈值：安静快照不造告警，但要把服务端阈值说出来；样本不足时条形不染色。
await expect("!document.querySelector('.overview-alerts') && document.querySelector('.overview-panel')?.innerText.includes('告警阈值：今日失败率 ≥ 10.0%') && document.querySelector('.overview-panel')?.innerText.includes('积压 ≥ 100 条')",'a quiet snapshot states the server thresholds without inventing alerts');
await expect("!!document.querySelector('.health-bar') && !document.querySelector('.health-bar').classList.contains('health-bar--warn')",'12 deliveries of sample stay below the judgement sample');
// 今日失败原因：名字要看得懂，计数折行要正好等于今日失败数。
await expect("document.querySelectorAll('.overview-reason-list li').length===2 && document.querySelector('[data-reason=ack_timeout]')?.innerText.includes('设备确认超时') && document.querySelector('[data-reason=ack_timeout]')?.innerText.includes('2 条') && [...document.querySelectorAll('.overview-reason-count')].reduce((sum,node)=>sum+parseInt(node.innerText,10),0)===3",'failure reasons are named and their counts add up to today\'s failures');
// 计数钻取：在线数字复用设备页的状态筛选，不新增一个只读列表。
await expect("!!document.querySelector('[data-stat=online]')",'the online count is clickable');
await js("document.querySelector('[data-stat=online]').click()");
await expect("document.querySelector('h1')?.innerText==='设备'",'the online count opens the device page');
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-card')?.innerText.includes('回归测试设备') && document.querySelector('.device-state-filter')?.innerText.includes('在线') && document.querySelector('.content .section-heading p')?.innerText.includes('显示 1 / 共 1 台')",'the online count lands on the online devices with the state filter named');
const statReads=(await fixture({})).deviceRequests.filter(path=>path.includes('state=online'));
if(!statReads.length||!statReads.every(path=>path.includes('page=1')&&path.includes('size=24')))throw new Error('online stat drill did not carry state=online: '+JSON.stringify(statReads));
results.push('the online count drill really carries state=online');
await navigate('概览');
await expect("(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()==='刷新概览');return !!b&&!b.disabled;})()",'the overview refresh is ready after the stat drill');
await fixture({mode:'alerts'});await tap('刷新概览');
await expect("document.querySelectorAll('.overview-alerts .overview-alert').length===3 && document.querySelector('.overview-alerts [data-alert=devices_online]')?.innerText.includes('低于告警阈值 1 台') && document.querySelector('.overview-alerts [data-alert=delivery_backlog]')?.innerText.includes('120 条') && document.querySelector('.overview-alerts [data-alert=delivery_failure_rate]')?.innerText.includes('22.5%')",'each breached metric becomes its own alert with the threshold it crossed');
await expect("document.querySelector('.health-bar')?.classList.contains('health-bar--warn')",'a failure rate above the threshold colours the bar');
// 离线计数同样钻取；停用但不在线的设备不应混进离线列表。
await expect("!!document.querySelector('[data-stat=offline]')",'the offline count is clickable');
await js("document.querySelector('[data-stat=offline]').click()");
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-card')?.innerText.includes('离线样机') && !document.querySelector('.device-card')?.innerText.includes('停用样机') && document.querySelector('.device-state-filter')?.innerText.includes('离线')",'the offline count lands on only enabled offline devices');
const statOfflineReads=(await fixture({})).deviceRequests.filter(path=>path.includes('state=offline'));
if(!statOfflineReads.length||!statOfflineReads.some(path=>path.includes('page=1')))throw new Error('offline stat drill did not carry state=offline: '+JSON.stringify(statOfflineReads));
results.push('the offline count drill really carries state=offline');
await navigate('概览');
// 告警钻取：目标由服务端给出，页面把筛选带过去，而不是只换页签。
await expect("document.querySelectorAll('.overview-alerts [data-alert-action]').length===3 && document.querySelector('[data-alert-action=delivery_backlog]')?.innerText==='查看等待投递' && document.querySelector('[data-alert-action=devices_online]')?.innerText==='查看设备'",'each actionable alert offers the filter the server points it at');
// 切页签会重挂面板，按钮要等快照回来才存在；单发 click 会撞上正在加载的窗口。
async function clickWhenReady(selector,label){await expect(`(()=>{const b=document.querySelector(${JSON.stringify(selector)});return !!b&&!b.disabled;})()`,label);await js(`document.querySelector(${JSON.stringify(selector)}).click()`);}
const drillBase=(await fixture({})).deliveryRequests.length;
await clickWhenReady('[data-alert-action=delivery_backlog]','the backlog alert is clickable');
await expect("document.querySelector('h1')?.innerText==='投递'",'the backlog alert opens the delivery page');
await expect("document.querySelector('.table-shell tbody')?.innerText.includes('等待投递样本') && !document.querySelector('.table-shell tbody')?.innerText.includes('失败投递样本')",'the drill lands on a filtered list instead of the whole table');
await expect("document.querySelector('.table-shell tbody')?.innerText.includes('回归样机') && document.querySelector('.table-shell tbody')?.innerText.includes('dodo-ui-fixture')",'delivery rows show the device name and keep the id');
const drillRequests=(await fixture({})).deliveryRequests.slice(drillBase);
if(!drillRequests.length||!drillRequests.every(path=>path.includes('status=pending')))throw new Error('drill did not carry status=pending: '+JSON.stringify(drillRequests));
results.push('the filtered read really carried status=pending');
await navigate('概览');
await clickWhenReady('[data-alert-action=delivery_failure_rate]','returning to overview offers the failure alert again');
await expect("document.querySelector('h1')?.innerText==='投递' && document.querySelector('.table-shell tbody')?.innerText.includes('失败投递样本') && !document.querySelector('.table-shell tbody')?.innerText.includes('等待投递样本')",'the failure-rate alert filters to failed deliveries');
await navigate('概览');
await clickWhenReady('[data-alert-action=devices_online]','the device alert is clickable');
await expect("document.querySelector('h1')?.innerText==='设备'",'the device alert opens the device page');
// 设备页按「启用且离线」过滤：停用样机虽同样不在线，但不属于这次告警。
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-card')?.innerText.includes('离线样机') && !document.querySelector('.device-card')?.innerText.includes('停用样机') && document.querySelector('.device-card')?.innerText.includes('待接收') && document.querySelector('.device-card')?.innerText.includes('2 条') && document.querySelector('.device-card')?.innerText.includes('固件') && document.querySelector('.device-card')?.innerText.includes('0.4.0-dev')",'the device drill shows only enabled offline devices with their waiting deliveries and firmware');
await expect("document.querySelector('.device-state-filter')?.innerText.includes('离线') && document.querySelector('.content .section-heading p')?.innerText.includes('显示 1 / 共 2 台')",'the device page states the state it filtered to and how many it hid');
// 卡片「待接收 2 条」可钻取：按该设备 + pending/sent 过滤，行数与卡片计数同一口径。
const waitingBase=(await fixture({})).deliveryRequests.length;
await clickWhenReady('.device-waiting','the waiting count is clickable');
await expect("document.querySelector('h1')?.innerText==='投递'",'the waiting count opens the delivery page');
await expect("document.querySelector('.table-shell tbody')?.innerText.includes('离线样机待发送') && document.querySelector('.table-shell tbody')?.innerText.includes('离线样机已发送') && !document.querySelector('.table-shell tbody')?.innerText.includes('失败投递样本') && !document.querySelector('.table-shell tbody')?.innerText.includes('等待投递样本')",'the drill lists only this device\'s pending and sent deliveries');
await expect("document.querySelector('.content .section-heading p')?.innerText.includes('2 条投递') && document.querySelector('.n-select[aria-label=投递状态]')?.innerText.includes('待接收')",'the delivery page names the waiting filter instead of a whole-table status');
const waitingReads=(await fixture({})).deliveryRequests.slice(waitingBase);
if(!waitingReads.length||!waitingReads.every(path=>path.includes('waiting=true')&&path.includes('deviceId=dodo-ui-offline')&&!path.includes('status=')))throw new Error('waiting drill did not read pending and sent for that device: '+JSON.stringify(waitingReads));
results.push('the waiting count drill really reads pending and sent for that device');
await navigate('设备');
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-state-filter')?.innerText.includes('离线') && document.querySelector('.device-card')?.innerText.includes('离线样机')",'returning from the waiting drill keeps the device filter');
async function openSelect(selector){await expect(`(()=>{const s=document.querySelector('${selector} .n-base-selection');return !!s&&!s.classList.contains('n-base-selection--disabled')})()`,'select '+selector+' is enabled');if(!(await js(`document.querySelector('${selector} .n-base-selection')?.classList.contains('n-base-selection--active')`)))await js(`document.querySelector('${selector} .n-base-selection').click()`);await expect(`document.querySelector('${selector} .n-base-selection')?.classList.contains('n-base-selection--active')`,'select '+selector+' is open');}
async function chooseVisibleOption(label){const found=await js(`(()=>{const o=[...document.querySelectorAll('.n-base-select-option')].filter(o=>o.offsetParent!==null).filter(o=>o.innerText.trim()===${JSON.stringify(label)}).pop();if(!o)return false;o.click();return true;})()`);if(!found)throw new Error('option '+label+' is not visible');}
async function pickState(label){for(let attempt=0;attempt<3;attempt++){await openSelect('.device-state-filter');await expect(`[...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.trim()===${JSON.stringify(label)}&&o.offsetParent!==null)`,'state option '+label+' is offered');await chooseVisibleOption(label);try{await expect(`document.querySelector('.device-state-filter')?.innerText.includes(${JSON.stringify(label)})`,'state filter shows '+label);return;}catch(e){if(attempt===2)throw e;await wait(.3);}}}
await pickState('全部');
await expect("document.querySelectorAll('.device-card').length===2 && document.querySelector('.device-state-filter')?.innerText.includes('全部')",'switching the state back to all restores every device');
// 设备分页：26 台样本由服务端分页，页头写全表总数；外壳下拉仍读未过滤的完整清单。
await fixture({mode:'many'});
await navigate('概览');await navigate('设备');
await expect("document.querySelectorAll('.device-card').length===24 && document.querySelector('.content .section-heading p')?.innerText.includes('26 台设备') && document.querySelector('.content .section-heading p')?.innerText.includes('22 台在线') && document.querySelector('.device-pagination')?.innerText.includes('1 / 2') && [...document.querySelectorAll('.device-card')].every(card=>!card.innerText.includes('待接收') && ![...card.querySelectorAll('.device-seen')].some(line=>line.innerText.startsWith('固件')))",'the device page reads one server page, states the global total and hides zero waiting and unknown firmware');
await tap('下一页');
await expect("document.querySelectorAll('.device-card').length===2 && document.querySelector('.device-pagination')?.innerText.includes('2 / 2') && document.querySelector('.device-grid')?.innerText.includes('分页设备 24') && document.querySelector('.device-grid')?.innerText.includes('分页设备 25')",'the next page asks the server for the remaining devices');
await pickState('离线');
await expect("document.querySelectorAll('.device-card').length===2 && document.querySelector('.content .section-heading p')?.innerText.includes('显示 2 / 共 26 台') && !document.querySelector('.device-pagination') && document.querySelector('.device-grid')?.innerText.includes('分页设备 22') && !document.querySelector('.device-grid')?.innerText.includes('分页设备 24')",'a state filter is applied by the server and resets to the first page');
await tap('刷新');
await expect("document.querySelectorAll('.device-card').length===2 && document.querySelector('.device-state-filter')?.innerText.includes('离线')",'the filtered device page survives a full refresh');
const deviceReads=await fixture({});
const filteredReads=deviceReads.deviceRequests.filter(path=>path.includes('state=offline'));
if(!filteredReads.length||!filteredReads.every(path=>path.includes('page=1')&&path.includes('size=24')))throw new Error('device filter did not carry state/page/size: '+JSON.stringify(deviceReads.deviceRequests));
if(!deviceReads.deviceRequests.some(path=>!path.includes('state=')&&!path.includes('page=')&&!path.includes('size=')))throw new Error('the shell device picker lost its unfiltered read: '+JSON.stringify(deviceReads.deviceRequests));
results.push('device reads carry state/page/size while the shell keeps the unfiltered list');
await pickState('全部');
// 设备搜索/排序/页大小：均由服务端处理，条件变化后回到第一页。
async function pickFilter(selector,label){await openSelect(selector);await expect(`[...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.trim()===${JSON.stringify(label)}&&o.offsetParent!==null)`,'option '+label+' is offered');await chooseVisibleOption(label);}
await fillInput('.device-search input','25');
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-grid')?.innerText.includes('分页设备 25') && document.querySelector('.content .section-heading p')?.innerText.includes('显示 1 / 共 26 台')",'a device search is applied by the server');
await fillInput('.device-search input','');
await expect("document.querySelectorAll('.device-card').length===24 && document.querySelector('.content .section-heading p')?.innerText.includes('26 台设备')",'clearing the search restores the unbounded page');
await pickFilter('.device-sort-filter','名称：Z→A');
await expect("document.querySelectorAll('.device-card').length===24 && document.querySelector('.device-card')?.innerText.includes('分页设备 25') && !document.querySelector('.device-grid')?.innerText.includes('分页设备 01')",'sorting is applied by the server');
await pickFilter('.device-size-filter','每页 12 台');
await expect("document.querySelectorAll('.device-card').length===12 && document.querySelector('.device-pagination')?.innerText.includes('1 / 3')",'the chosen page size is applied by the server');
await pickFilter('.device-sort-filter','信号：强→弱');
await expect("document.querySelectorAll('.device-card').length===12 && document.querySelector('.device-card')?.innerText.includes('分页设备 01')",'signal sorting is applied by the server');
const queryReads=await fixture({});
if(!queryReads.deviceRequests.some(path=>/[?&]q=25(&|$)/.test(path)))throw new Error('device search did not reach the server: '+JSON.stringify(queryReads.deviceRequests));
if(!queryReads.deviceRequests.some(path=>path.includes('sort=name')&&path.includes('order=desc')))throw new Error('device sort did not reach the server: '+JSON.stringify(queryReads.deviceRequests));
if(!queryReads.deviceRequests.some(path=>path.includes('sort=signal')&&path.includes('order=desc')))throw new Error('signal sort did not reach the server: '+JSON.stringify(queryReads.deviceRequests));
if(!queryReads.deviceRequests.some(path=>path.includes('size=12')&&path.includes('page=1')))throw new Error('device page size did not reach the server: '+JSON.stringify(queryReads.deviceRequests));
results.push('device search, name/signal sort and page size are server-side and reset to page 1');
// 恢复名称倒序，后续删除样本仍落在第一页。
await pickFilter('.device-sort-filter','名称：Z→A');
await expect("document.querySelector('.device-card')?.innerText.includes('分页设备 25')",'name sorting restores the first page');
// 顶栏设备选择器：输入按名称/ID 走服务端搜索，选中后进入单设备视图；搜索本身不改设备页。
async function pickViewInput(value){await openSelect('.device-view');await fillInput('.device-view input',value);}
async function pickViewOption(label){await expect(`[...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.trim()===${JSON.stringify(label)}&&o.offsetParent!==null)`,'device view option '+label+' is offered');await chooseVisibleOption(label);}
const pickerBase=(await fixture({})).deviceRequests.length;
await pickViewInput('25');
await expect("[...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.trim()==='分页设备 25') && ![...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.includes('分页设备 01'))",'the topbar search offers only matching devices');
await expect("document.querySelectorAll('.device-card').length===12",'typing in the topbar leaves the device page untouched');
const pickerReads=(await fixture({})).deviceRequests.slice(pickerBase);
if(!pickerReads.some(path=>/[?&]q=25(&|$)/.test(path)&&path.includes('size=50')&&path.includes('sort=name')&&path.includes('order=asc')))throw new Error('topbar search did not reach the server: '+JSON.stringify(pickerReads));
results.push('the topbar device search is a debounced server read');
await pickViewOption('分页设备 25');
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-grid')?.innerText.includes('分页设备 25') && document.querySelector('.content .section-heading p')?.innerText.includes('1 台设备') && document.querySelector('.n-select[aria-label=设备视图]')?.innerText.includes('分页设备 25')",'choosing a search result shows that device');
// 空结果要明确说出来，而不是留下一个空下拉。
await pickViewInput('不存在的设备');
await expect("[...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.trim()==='没有匹配的设备'&&o.classList.contains('n-base-select-option--disabled'))",'an empty search result is named');
await js("document.querySelector('.device-view input').blur()");
await expect("document.querySelectorAll('.device-card').length===1",'closing the search keeps the chosen device view');
await js("document.querySelector('.device-view .n-base-selection').click()");
await pickViewOption('全部设备');
await expect("document.querySelectorAll('.device-card').length===12 && document.querySelector('.n-select[aria-label=设备视图]')?.innerText.includes('全部设备')",'the global view is restorable');
// 超过默认 100 台：搜索能选到第 120 台，整表刷新不能把选择清空。
await fixture({mode:'crowd'});await tap('刷新');
await expect("document.querySelectorAll('.device-card').length===12 && document.querySelector('.content .section-heading p')?.innerText.includes('120 台设备')",'the crowd fixture loads 120 devices through one default page');
await pickViewInput('119');
await pickViewOption('海量设备 119');
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-grid')?.innerText.includes('海量设备 119')",'a device beyond the default 100 is selectable');
const crowdRefreshBase=(await fixture({})).deviceRequests.length;
await tap('刷新');
await expect("document.querySelectorAll('.device-card').length===1 && document.querySelector('.device-grid')?.innerText.includes('海量设备 119')",'a full refresh keeps a device outside the default list selected');
const crowdRefresh=(await fixture({})).deviceRequests.slice(crowdRefreshBase);
if(!crowdRefresh.some(path=>!path.includes('q=')))throw new Error('full refresh did not reread the default device list: '+JSON.stringify(crowdRefresh));
results.push('the selected search-only device survives the unfiltered list refresh');
// 设备删除：只有管理员、需输入完整名称；有历史的设备 409 保留，删除当前选择后回到全部设备。
const deleteBase=(await fixture({})).deviceDeletes.length;
await expect("!!document.querySelector('#device-delete-dodo-crowd-119') && !document.querySelector('#device-delete-dodo-crowd-119').disabled",'an unused device offers deletion to the administrator');
await js("document.querySelector('#device-delete-dodo-crowd-119').click()");
await expect("document.querySelector('.device-delete-hint')?.innerText.includes('有历史请停用') && [...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='确认删除')?.disabled",'deletion explains its history boundary and waits for the typed name');
await fillInput('#devicespanel-field-4','海量设备 118');
await expect("[...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='确认删除')?.disabled",'a wrong device name cannot confirm deletion');
await fillInput('#devicespanel-field-4','海量设备 119');
await expect("![...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='确认删除')?.disabled",'the exact device name unlocks confirmation');
await tap('确认删除');
await expect("!document.querySelector('#device-delete-dodo-crowd-119') && document.querySelector('.n-select[aria-label=设备视图]')?.innerText.includes('全部设备') && document.querySelector('.content .section-heading p')?.innerText.includes('119 台设备')",'deleting the selected device clears the shell view and the card');
const deleteRequests=(await fixture({})).deviceDeletes.slice(deleteBase);
if(deleteRequests.length!==1||!deleteRequests[0].endsWith('/api/v1/devices/dodo-crowd-119'))throw new Error('device deletion did not reach the server exactly once: '+JSON.stringify(deleteRequests));
results.push('device deletion is one explicit DELETE for the confirmed device');
await js("document.querySelector('#device-delete-dodo-crowd-118').click()");
await fillInput('#devicespanel-field-4','海量设备 118');
await tap('确认删除');
await expect("document.querySelector('.content > .feedback.error')?.innerText.includes('只能停用') && !!document.querySelector('#device-delete-dodo-crowd-118')",'a device with history is refused and stays in the list');
await tap('取消');
await expect("!document.querySelector('.device-delete-hint') && !!document.querySelector('#device-delete-dodo-crowd-118')",'cancelling deletion leaves the device untouched');
await fixture({mode:'alerts'});await navigate('概览');await clickWhenReady('.overview-panel .section-heading button','the overview refresh control returns after the device checks');
await navigate('概览');
await expect("document.querySelectorAll('.overview-alerts [data-alert-action]').length===3",'drilling leaves the overview intact on return');
// 二级钻取：从失败原因再收敛到"只有这类失败"。
const reasonBase=(await fixture({})).deliveryRequests.length;
await clickWhenReady('[data-reason=ack_timeout] button','the reason row is clickable');
await expect("document.querySelector('h1')?.innerText==='投递'",'a failure reason opens the delivery page too');
await expect("document.querySelector('.table-shell tbody')?.innerText.includes('失败投递样本') && !document.querySelector('.table-shell tbody')?.innerText.includes('Broker 失败样本')",'the reason drill narrows to that reason instead of every failure');
const reasonRequests=(await fixture({})).deliveryRequests.slice(reasonBase);
if(!reasonRequests.length||!reasonRequests.some(path=>path.includes('status=failed')&&path.includes('lastError=ack_timeout')))throw new Error('reason drill did not carry status+lastError: '+JSON.stringify(reasonRequests));
results.push('the reason read carried both status=failed and lastError');
await expect("document.querySelector('[aria-label=最近错误]')?.innerText.includes('设备确认超时')",'the delivery page shows the reason filter it applied');
const clearBase=(await fixture({})).deliveryRequests.length;
await expect("(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()==='清空筛选');return !!b&&!b.disabled;})()",'the clear control is available after a drill');
await tap('清空筛选');
await expect("document.querySelector('[aria-label=最近错误]')?.innerText.trim()==='全部原因' && document.querySelector('.table-shell tbody')?.innerText.includes('等待投递样本')",'clearing the filter resets the reason control and restores the unfiltered list');
const cleared=(await fixture({})).deliveryRequests.slice(clearBase);
if(!cleared.length||cleared.some(path=>path.includes('lastError=')||path.includes('status=')))throw new Error('clearing left a filter behind: '+JSON.stringify(cleared));
results.push('clearing removes both status and lastError from the read');
await navigate('概览');
await fixture({mode:'alerts',thresholds:{failureRate:0.3}});await clickWhenReady('.overview-panel .section-heading button','the overview refresh control is ready after returning');
await expect("document.querySelectorAll('.overview-alerts .overview-alert').length===2 && !document.querySelector('.overview-alerts [data-alert=delivery_failure_rate]') && document.querySelector('.overview-panel')?.innerText.includes('≥ 30.0%')",'the console follows the server threshold instead of a local constant');
await expect("!!document.querySelector('.health-bar') && !document.querySelector('.health-bar').classList.contains('health-bar--warn')",'raising the threshold stops colouring the same 22.5% rate');
await fixture({mode:'success',thresholds:{failureRate:0.1}});await tap('刷新概览');
await expect("!document.querySelector('.overview-alerts') && document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7'",'the alert list disappears once the breach is cleared');
// 运行趋势：24 桶、最后一桶未结束、失败单独成图；范围切换与空/失败/恢复。
await expect("document.querySelector('.overview-trend-summary')?.innerText.includes('窗口内收件') && document.querySelectorAll('.overview-trend-chart--messages .overview-trend-bar').length===24",'trend draws one bar per hour in the default 24-hour range');
await expect("document.querySelectorAll('.overview-trend-chart--messages .overview-trend-bar.is-partial').length===1 && document.querySelector('.overview-trend-chart--messages rect.is-partial')?.dataset.index==='23'",'only the current bucket is marked partial');
await expect("document.querySelectorAll('.overview-trend-chart--failures .overview-trend-bar:not(.is-empty)').length===1 && [...document.querySelectorAll('.overview-trend-figure figcaption')].some(node=>node.innerText.includes('失败事件'))",'failure events keep their own series and caption');
await tap('7 天');
await expect("document.querySelectorAll('.overview-trend-chart--messages .overview-trend-bar').length===28 && document.querySelector('.overview-trend')?.innerText.includes('每桶 6 小时')",'7-day range switches to 6-hour buckets');
await fixture({trendMode:'empty'});await tap('刷新趋势');
await expect("document.querySelector('.overview-trend .console-state')?.innerText.includes('没有保留') && !document.querySelector('.overview-trend-chart')",'empty trend is explicit instead of a zero chart');
await fixture({trendMode:'error'});await tap('刷新趋势');
await expect("document.querySelector('.overview-trend [role=alert]') && !document.querySelector('.overview-trend-chart')",'trend failure hides the stale chart');
await fixture({trendMode:'success'});await tap('刷新趋势');
await expect("!!document.querySelector('.overview-trend-chart') && !document.querySelector('.overview-trend [role=alert]')",'trend recovers after a failed read');
// 趋势按原因折行：折行覆盖整个窗口，点选只收窄失败列，清空后恢复。
await expect("document.querySelectorAll('.overview-trend-reasons li').length===2 && document.querySelector('[data-trend-reason=ack_timeout]')?.innerText.includes('设备确认超时') && document.querySelector('[data-trend-reason=ack_timeout]')?.innerText.includes('1') && document.querySelector('.overview-trend-reasons')?.innerText.includes('合计 2 次')",'trend lists the window failure reasons with counts');
const trendBase=(await fixture({})).trendRequests.length;
await js("document.querySelector('[data-trend-reason=ack_timeout]').click()");
await expect("document.querySelector('[data-trend-reason=ack_timeout]')?.getAttribute('aria-pressed')==='true' && [...document.querySelectorAll('.overview-trend-figure figcaption')].some(node=>node.innerText.includes('设备确认超时')) && document.querySelector('.overview-trend-summary')?.innerText.includes('仅设备确认超时；窗口共 2 次')",'selecting a reason narrows only the failure series and keeps the window total');
const trendRead=(await fixture({})).trendRequests.slice(trendBase);
if(!trendRead.some(path=>path.includes('reason=ack_timeout')))throw new Error('the reason was not sent: '+JSON.stringify(trendRead));
results.push('the trend request carries reason=ack_timeout');
await expect("document.querySelectorAll('.overview-trend-chart--failures .overview-trend-bar:not(.is-empty)').length===1 && document.querySelectorAll('.overview-trend-reasons li').length===2",'filtered chart keeps the full reason list for switching');
await tap('全部原因');
await expect("document.querySelector('[data-trend-reason=ack_timeout]')?.getAttribute('aria-pressed')==='false' && document.querySelector('.overview-trend-summary')?.innerText.includes('失败事件 2 次') && !document.querySelector('.overview-trend-summary')?.innerText.includes('窗口共')",'clearing the reason restores the unfiltered series');
const trendClear=(await fixture({})).trendRequests.slice(trendBase);
if(trendClear.at(-1).includes('reason='))throw new Error('clearing left a reason behind: '+trendClear.at(-1));
results.push('clearing removes reason from the trend read');
await fixture({mode:'mqtt_down'});await tap('刷新概览');
await expect("document.querySelector('.overview-panel [role=status]')?.innerText.includes('MQTT 连接不可用') && document.querySelector('[data-stat=online]')?.textContent.trim().startsWith('2') && !document.querySelector('[data-alert=mqtt_connection] [data-alert-action]')",'Broker failure never rewrites the online count and offers no filter it cannot honour');
await fixture({mode:'error'});await tap('刷新概览');
await expect("!!document.querySelector('.overview-panel [role=alert]') && !document.querySelector('.overview-panel dl')",'failed read hides stale counts instead of showing empty or healthy');
await fixture({mode:'empty'});await tap('刷新概览');
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='0' && document.querySelector('[data-metric=failure-rate] dd')?.childNodes[0]?.textContent.trim()==='—' && document.querySelector('[data-stat=online]')?.textContent.trim().startsWith('0')",'empty snapshot has zero counts and no fabricated zero-percent failure rate');
await fixture({mode:'success',delay:1});await tap('刷新概览');
await expect("document.querySelector('.overview-panel [aria-busy=true]')?.innerText==='正在读取概览…' && document.querySelector('.overview-panel button').disabled && !document.querySelector('.overview-panel dl')",'loading and repeat clicks are explicit');
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7'",'delayed snapshot finishes');await fixture({delay:0});
const before=(await fixture({})).requests;
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7'",'polling starts from current snapshot');
for(let i=0;i<65;i++){if((await fixture({})).requests>before)break;await wait(0.1);}
if((await fixture({})).requests<=before)throw new Error('Visible overview did not poll');results.push('visible overview polls');
await fixture({mode:'alerts'});await tap('刷新概览');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && [...document.querySelectorAll('.overview-panel dl>div')].every(e=>e.getBoundingClientRect().right<=390) && [...document.querySelectorAll('.overview-alerts .overview-alert')].length===3 && [...document.querySelectorAll('.overview-alerts .overview-alert, .overview-alerts [data-alert-action], .overview-reason-list li, .overview-reason-list button, .overview-trend-reasons li, .overview-trend-reasons button')].every(e=>e.getBoundingClientRect().right<=390) && document.querySelectorAll('.overview-reason-list li').length===2 && document.querySelector('.overview-trend-chart')?.getBoundingClientRect().right<=390",'overview, its alert actions, reason rows and trend fit 390 CSS pixels');
cliLog({mobileScreenshot:await captureScreenshot()});
await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({mode:'success'});await tap('刷新概览');
await expect("!document.querySelector('.overview-alerts')",'alerts clear without a leftover warning at the default viewport');
await fixture({mode:'empty',delay:1});await clickWhenReady('.overview-panel .section-heading button','the overview refresh control is ready for the unmount check');await navigate('消息');
await wait(1.2);await expect("document.querySelector('h1')?.innerText==='消息' && !document.querySelector('.overview-panel')",'late response cannot restore an unmounted overview');
await fixture({mode:'success',delay:0});await navigate('概览');await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7'",'returning to overview reads a fresh snapshot');
// 阈值配置：读写同一份服务端状态，版本与变更审计随响应返回，保存后概览立即采用。
await tap('调整阈值');
await expect("!!document.querySelector('#threshold-failure-rate') && document.querySelector('.threshold-editor')?.innerText.includes('当前 v1')",'the admin threshold form opens with the current revision');
await fillInput('#threshold-failure-rate','30');
await fillInput('#threshold-failure-sample','5');
await tap('保存阈值');
await expect("document.querySelector('.threshold-editor')?.innerText.includes('当前 v2') && document.querySelector('.threshold-audit')?.innerText.includes('已显示 1 / 共 1 条') && document.querySelectorAll('.threshold-audit li').length===1",'saving bumps the revision and records one audit entry with its true total');
await expect("document.querySelector('.overview-panel')?.innerText.includes('≥ 30.0%') && document.querySelector('.overview-panel')?.innerText.includes('投递 ≥ 5 条')",'the overview caption uses the saved thresholds');
const thresholdState=await fixture({});
if(thresholdState.thresholdVersion!==2||thresholdState.thresholdUpdates.length!==1||thresholdState.thresholdUpdates[0].thresholds.failureRate!==0.3)throw new Error('threshold save did not reach the server: '+JSON.stringify(thresholdState));
results.push('the threshold save carried version and values to the server');
// 审计分页：首屏最新 20 条，按最旧 sequence 向前取，追加后不重复并隐藏按钮。
await fixture({thresholdHistory:25});await tap('取消');await tap('调整阈值');
await expect("document.querySelector('.threshold-editor')?.innerText.includes('已显示 20 / 共 25 条') && document.querySelectorAll('.threshold-audit li').length===20 && [...document.querySelectorAll('.threshold-audit button')].some(b=>b.innerText.includes('加载更早变更'))",'the audit list caps at 20 and offers older entries');
await tap('加载更早变更');
await expect("document.querySelectorAll('.threshold-audit li').length===25 && document.querySelector('.threshold-audit')?.innerText.includes('已显示 25 / 共 25 条') && ![...document.querySelectorAll('.threshold-audit button')].some(b=>b.innerText.includes('加载更早变更'))",'loading older audit rows appends without duplicates');
const auditCalls=(await fixture({})).auditRequests;
if(!auditCalls.length||!auditCalls.at(-1).includes('before=6'))throw new Error('audit pagination did not use the oldest loaded cursor: '+JSON.stringify(auditCalls));
results.push('audit pagination requested before=6');
await tap('取消');
await fixture({mode:'empty',delay:1});await tap('刷新概览');await tap('退出登录');await wait(1.2);
await expect("!!document.querySelector('.login-card') && !document.querySelector('.overview-panel') && document.querySelector('.token-label input').value===''",'logout discards delayed snapshot and token');
const beforeAuth=(await fixture({mode:'unauthorized',delay:0})).requests;
await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("!!document.querySelector('.login-card') && !document.querySelector('.overview-panel') && document.querySelector('.token-label input').value===''",'overview 401 ends the session');
if((await fixture({})).requests<=beforeAuth)throw new Error('401 check never reached overview HTTP');
cliLog({passed:results.length,checks:results,scope:'actual Vue and RAM-only HTTP; no database/Broker/device'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS overview regression',{keep:false}));
EOF
