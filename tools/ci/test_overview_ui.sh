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
await expect("document.querySelector('.overview-panel')?.innerText.includes('2026-11-13 · UTC') && document.querySelector('[data-metric=online] dd')?.childNodes[0]?.textContent.trim()==='2' && document.querySelector('[data-metric=offline] dd')?.childNodes[0]?.textContent.trim()==='3'",'UTC day and device counts are explicit');
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7' && document.querySelector('[data-metric=backlog] dd')?.childNodes[0]?.textContent.trim()==='6' && document.querySelector('.overview-panel')?.innerText.includes('停用 1 台')",'message, backlog and overlapping disabled counts are distinct');
// 告警阈值：安静快照不造告警，但要把服务端阈值说出来；样本不足时条形不染色。
await expect("!document.querySelector('.overview-alerts') && document.querySelector('.overview-panel')?.innerText.includes('告警阈值：今日失败率 ≥ 10.0%') && document.querySelector('.overview-panel')?.innerText.includes('积压 ≥ 100 条')",'a quiet snapshot states the server thresholds without inventing alerts');
await expect("!!document.querySelector('.health-bar') && !document.querySelector('.health-bar').classList.contains('health-bar--warn')",'12 deliveries of sample stay below the judgement sample');
await fixture({mode:'alerts'});await tap('刷新概览');
await expect("document.querySelectorAll('.overview-alerts .overview-alert').length===3 && document.querySelector('.overview-alerts [data-alert=devices_online]')?.innerText.includes('低于告警阈值 1 台') && document.querySelector('.overview-alerts [data-alert=delivery_backlog]')?.innerText.includes('120 条') && document.querySelector('.overview-alerts [data-alert=delivery_failure_rate]')?.innerText.includes('22.5%')",'each breached metric becomes its own alert with the threshold it crossed');
await expect("document.querySelector('.health-bar')?.classList.contains('health-bar--warn')",'a failure rate above the threshold colours the bar');
// 告警钻取：目标由服务端给出，页面把筛选带过去，而不是只换页签。
await expect("document.querySelectorAll('.overview-alerts [data-alert-action]').length===3 && document.querySelector('[data-alert-action=delivery_backlog]')?.innerText==='查看等待投递' && document.querySelector('[data-alert-action=devices_online]')?.innerText==='查看设备'",'each actionable alert offers the filter the server points it at');
const drillBase=(await fixture({})).deliveryRequests.length;
await tap('查看等待投递');
await expect("document.querySelector('h1')?.innerText==='投递'",'the backlog alert opens the delivery page');
await expect("document.querySelector('.table-shell tbody')?.innerText.includes('等待投递样本') && !document.querySelector('.table-shell tbody')?.innerText.includes('失败投递样本')",'the drill lands on a filtered list instead of the whole table');
const drillRequests=(await fixture({})).deliveryRequests.slice(drillBase);
if(!drillRequests.length||!drillRequests.every(path=>path.includes('status=pending')))throw new Error('drill did not carry status=pending: '+JSON.stringify(drillRequests));
results.push('the filtered read really carried status=pending');
await navigate('概览');
await expect("document.querySelector('[data-alert-action=delivery_failure_rate]')",'returning to overview shows the alerts again');
await tap('查看失败投递');
await expect("document.querySelector('h1')?.innerText==='投递' && document.querySelector('.table-shell tbody')?.innerText.includes('失败投递样本') && !document.querySelector('.table-shell tbody')?.innerText.includes('等待投递样本')",'the failure-rate alert filters to failed deliveries');
await navigate('概览');
await tap('查看设备');
await expect("document.querySelector('h1')?.innerText==='设备'",'the device alert opens the device page');
await navigate('概览');
await expect("document.querySelectorAll('.overview-alerts [data-alert-action]').length===3",'drilling leaves the overview intact on return');
await fixture({mode:'alerts',thresholds:{failureRate:0.3}});await tap('刷新概览');
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
await fixture({mode:'mqtt_down'});await tap('刷新概览');
await expect("document.querySelector('.overview-panel [role=status]')?.innerText.includes('MQTT 连接不可用') && document.querySelector('[data-metric=online] dd')?.childNodes[0]?.textContent.trim()==='2' && !document.querySelector('[data-alert=mqtt_connection] [data-alert-action]')",'Broker failure never rewrites the online count and offers no filter it cannot honour');
await fixture({mode:'error'});await tap('刷新概览');
await expect("!!document.querySelector('.overview-panel [role=alert]') && !document.querySelector('.overview-panel dl')",'failed read hides stale counts instead of showing empty or healthy');
await fixture({mode:'empty'});await tap('刷新概览');
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='0' && document.querySelector('[data-metric=failure-rate] dd')?.childNodes[0]?.textContent.trim()==='—' && document.querySelector('[data-metric=online] dd')?.childNodes[0]?.textContent.trim()==='0'",'empty snapshot has zero counts and no fabricated zero-percent failure rate');
await fixture({mode:'success',delay:1});await tap('刷新概览');
await expect("document.querySelector('.overview-panel [aria-busy=true]')?.innerText==='正在读取概览…' && document.querySelector('.overview-panel button').disabled && !document.querySelector('.overview-panel dl')",'loading and repeat clicks are explicit');
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7'",'delayed snapshot finishes');await fixture({delay:0});
const before=(await fixture({})).requests;
await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7'",'polling starts from current snapshot');
for(let i=0;i<65;i++){if((await fixture({})).requests>before)break;await wait(0.1);}
if((await fixture({})).requests<=before)throw new Error('Visible overview did not poll');results.push('visible overview polls');
await fixture({mode:'alerts'});await tap('刷新概览');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && [...document.querySelectorAll('.overview-panel dl>div')].every(e=>e.getBoundingClientRect().right<=390) && [...document.querySelectorAll('.overview-alerts .overview-alert')].length===3 && [...document.querySelectorAll('.overview-alerts .overview-alert, .overview-alerts [data-alert-action]')].every(e=>e.getBoundingClientRect().right<=390) && document.querySelector('.overview-trend-chart')?.getBoundingClientRect().right<=390",'overview, its alert actions and its trend fit 390 CSS pixels');
cliLog({mobileScreenshot:await captureScreenshot()});
await cdp('Emulation.clearDeviceMetricsOverride');
await fixture({mode:'success'});await tap('刷新概览');
await expect("!document.querySelector('.overview-alerts')",'alerts clear without a leftover warning at the default viewport');
await fixture({mode:'empty',delay:1});await tap('刷新概览');await navigate('消息');
await wait(1.2);await expect("document.querySelector('h1')?.innerText==='消息' && !document.querySelector('.overview-panel')",'late response cannot restore an unmounted overview');
await fixture({mode:'success',delay:0});await navigate('概览');await expect("document.querySelector('[data-metric=messages] dd')?.childNodes[0]?.textContent.trim()==='7'",'returning to overview reads a fresh snapshot');
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
