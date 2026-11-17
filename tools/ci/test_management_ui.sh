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
async function waitForLogin(){for(let i=0;i<120;i++){if(await js("!!document.querySelector('.token-label input:not(:disabled)') && !document.querySelector('.login-card button.primary')?.disabled").catch(()=>false))return;await wait(.1);}throw new Error('Login form did not settle after session recovery');}
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
  for(let i=0;i<50;i++) {if(await js(expression).catch(()=>false)){results.push(label);return;} await wait(0.1);}
  throw new Error('UI assertion failed: '+label);
}
async function expectState(predicate,label) {
  for(let i=0;i<50;i++) {const snapshot=await state();if(predicate(snapshot)){results.push(label);return;} await wait(0.1);}
  throw new Error('State assertion failed: '+label);
}
async function tap(text) {
  // 面板由按需加载的 chunk 渲染，按钮可能晚一拍出现；有限次重试，断言仍是"存在且可点"。
  for(let i=0;i<50;i++) {
    const found=await js(`(()=>{const button=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!button||button.disabled)return false;button.scrollIntoView({block:'center'});button.click();return true;})()`);
    if(found)return;
    await wait(0.1);
  }
  throw new Error('Button unavailable: '+text);
}
async function range(index,value) {
  await js(`(async()=>{const input=document.querySelectorAll('.management-form [role=slider]')[${index}];input.focus();const current=Number(input.getAttribute('aria-valuenow'));const key=current<${value}?'ArrowRight':'ArrowLeft';for(let i=0;i<Math.abs(current-${value});i++){input.dispatchEvent(new KeyboardEvent('keydown',{key,bubbles:true}));await Promise.resolve();}})()`);
}
async function hover(ratio) {
  await js(`(()=>{const svg=document.querySelector('.telemetry-chart');if(!svg)return false;const rect=svg.getBoundingClientRect();svg.dispatchEvent(new MouseEvent('mousemove',{clientX:rect.left+rect.width*${ratio},clientY:rect.top+rect.height*0.5,bubbles:true}));return true;})()`);
}
await fixture({reset:true});
await openOrReuseTab(base,{wait:true,timeout:15});
await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');

await expect("!!document.querySelector('.login-card')",'login screen');
await waitForLogin();await fillInput('.token-label input','incorrect-fixture-token');
await click('.login-card button.primary');
await expect("document.body.innerText.includes('管理令牌无效')",'invalid token is rejected');
await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');
await click('.login-card button.primary');
await expect("!!document.querySelector('.sidebar')",'authenticated app');
await expect("document.body.innerText.includes('内容已按保留期限清除')",'purged message list explains removal');
await click('.message-row');
await expect("document.querySelector('.message-detail')?.innerText.includes('清除正文和分析结果') && !document.querySelector('.analysis-section')",'purged message hides analysis');
await click('.sidebar nav button[data-tab=devices]');
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
await expect("document.querySelector('.device-card')?.innerText.includes('HomeLab-2.4G') && document.querySelector('.device-card')?.innerText.includes('192.168.1.20') && document.querySelector('.device-card')?.innerText.includes('-52 dBm')",'device card shows the latest Wi-Fi network, address and signal');
await tap('设置与状态');
await expect("document.querySelector('#device-management-panel')?.innerText.includes('尚未收到设备状态')",'empty device state');
await tap('状态历史');
await expect("document.querySelector('.telemetry-history .console-state--empty')?.innerText.includes('没有采样点') && !document.querySelector('.telemetry-chart')",'history empty state is explicit');
await fixture({mode:'fresh'});await tap('刷新状态');
await expect("document.querySelectorAll('.management-form [role=slider]').length===2",'fresh readback is editable');
await expect("document.querySelector('.telemetry-history')?.innerText.includes('状态历史') && document.querySelector('.telemetry-history')?.innerText.includes('来自设备已上报的遥测')",'history section explains its source');
await expect("!!document.querySelector('.telemetry-chart .series-primary') && !!document.querySelector('.telemetry-chart .series-secondary') && document.querySelector('.chart-legend')?.innerText.includes('可用内存') && document.querySelector('.chart-legend')?.innerText.includes('启动以来最小')",'history chart renders both series and legend');
await expectState(snapshot=>snapshot.telemetryRequests.some(path=>path.includes('/devices/dodo-ui-fixture/telemetry') && path.includes('bucket=5m')),'default range queries a 5m bucket');
await expect("document.body.innerText.includes('实时快照') && !document.querySelector('.telemetry-history')?.innerText.includes('实时')",'live snapshot is labelled, history is not');
await hover(0.55);
await expect("document.querySelector('.chart-tooltip')?.textContent.includes('KB')",'chart hover reads a real sample');
await expect("document.querySelector('.history-metrics')?.innerText.includes('内存') && document.querySelector('.history-metrics')?.innerText.includes('音量') && document.querySelector('.history-metrics')?.innerText.includes('未读') && document.querySelector('.history-metrics')?.innerText.includes('音频') && document.querySelector('.history-metrics')?.innerText.includes('网络')",'metric switcher exposes all history metrics');
await expect("document.querySelector('.management-metrics')?.innerText.includes('HomeLab-2.4G') && document.querySelector('.management-metrics')?.innerText.includes('192.168.1.20') && document.querySelector('.management-metrics')?.innerText.includes('信道 6') && document.querySelector('.management-metrics')?.innerText.includes('-52 dBm')",'snapshot shows the device Wi-Fi network, address, channel and signal');
await expect("document.querySelector('.management-metrics')?.innerText.includes('上次断线') && document.querySelector('.management-metrics')?.innerText.includes('201 找不到目标 AP')",'snapshot names the last disconnect reason');
await tap('音量');
await expect("document.querySelector('.chart-legend')?.innerText.includes('音量') && document.querySelector('.chart-legend')?.innerText.includes('%') && !document.querySelector('.telemetry-chart .series-secondary')",'volume metric draws a single percent series');
await hover(0.55);
await expect("document.querySelector('.chart-tooltip')?.textContent.includes('%')",'volume tooltip uses percent');
await tap('未读');
await expect("document.querySelector('.chart-legend')?.innerText.includes('未读消息') && document.querySelector('.chart-legend')?.innerText.includes('条')",'unread metric is available');
await hover(0.55);
await expect("document.querySelector('.chart-tooltip')?.textContent.includes('条')",'unread tooltip uses message count');
await tap('音频');
await expect("document.querySelector('.chart-legend')?.innerText.includes('音频欠载') && document.querySelector('.chart-legend')?.innerText.includes('音频异常') && document.querySelectorAll('.telemetry-chart .dot-fault').length>0",'audio metric marks faulted samples');
await js("(()=>{const svg=document.querySelector('.telemetry-chart');const dot=document.querySelector('.telemetry-chart .dot-fault');const rect=svg.getBoundingClientRect();const box=svg.viewBox.baseVal;const cx=parseFloat(dot.getAttribute('cx')),cy=parseFloat(dot.getAttribute('cy'));svg.dispatchEvent(new MouseEvent('mousemove',{clientX:rect.left+cx/box.width*rect.width,clientY:rect.top+cy/box.height*rect.height,bubbles:true}));return true;})()");
await expect("document.querySelector('.chart-tooltip')?.textContent.includes('音频异常')",'audio fault is explained in tooltip');
await tap('网络');
await expect("document.querySelector('.chart-legend')?.innerText.includes('Wi-Fi 信号') && document.querySelector('.chart-legend')?.innerText.includes('dBm') && !!document.querySelector('.telemetry-chart .series-primary')",'network metric plots Wi-Fi signal');
await hover(0.55);
await expect("document.querySelector('.chart-tooltip')?.textContent.includes('dBm')",'network tooltip uses dBm');
await tap('内存');
await expect("document.querySelector('.chart-legend')?.innerText.includes('可用内存') && !!document.querySelector('.telemetry-chart .series-secondary')",'heap metric restores both series');
await js("(()=>{window.__csv=null;window.__csvName='';window.__csvText='';const original=URL.createObjectURL;URL.createObjectURL=blob=>{window.__csv=blob;return original.call(URL,blob);};HTMLAnchorElement.prototype.click=function(){window.__csvName=this.download;};return true;})()");
await tap('导出 CSV');
await expect("window.__csvName.startsWith('telemetry-dodo-ui-fixture-6h-') && window.__csvName.endsWith('.csv') && window.__csv?.size>500",'history export downloads a CSV');
await js("(()=>{window.__csv.text().then(text=>{window.__csvText=text;});return true;})()");
await expect("window.__csvText.includes('采样时间') && window.__csvText.includes('音频欠载') && window.__csvText.includes('Wi-Fi IP') && window.__csvText.includes('192.168.1.2') && window.__csvText.includes('SSID') && window.__csvText.includes('HomeLab-2.4G') && window.__csvText.split('\\r\\n').length>=70",'exported CSV has a header and every bucket row');
await js("(()=>{delete HTMLAnchorElement.prototype.click;return true;})()");
await js("(()=>{const svg=document.querySelector('.telemetry-chart');const rect=svg.getBoundingClientRect();const at=ratio=>[rect.left+rect.width*ratio,rect.top+rect.height*0.5];const [x1,y1]=at(0.35),[x2,y2]=at(0.6);svg.dispatchEvent(new MouseEvent('mousedown',{button:0,clientX:x1,clientY:y1,bubbles:true}));svg.dispatchEvent(new MouseEvent('mousemove',{clientX:x2,clientY:y2,bubbles:true}));window.dispatchEvent(new MouseEvent('mouseup',{button:0,clientX:x2,clientY:y2,bubbles:true}));return true;})()");
await expect("!!document.querySelector('.chart-reset')",'drag selection zooms the history chart');
await js("(()=>{const svg=document.querySelector('.telemetry-chart');const rect=svg.getBoundingClientRect();svg.dispatchEvent(new MouseEvent('mousemove',{clientX:rect.left+rect.width*0.5,clientY:rect.top+rect.height*0.5,bubbles:true}));return true;})()");
await expect("!!document.querySelector('.chart-tooltip')",'tooltip still works after zoom');
await tap('重置缩放');
await expect("!document.querySelector('.chart-reset')",'zoom resets to the full range');
await tap('24 小时');
await expectState(snapshot=>snapshot.telemetryRequests.some(path=>path.includes('bucket=15m')),'range switch queries a 15m bucket');
await expect("document.querySelector('.telemetry-history')?.innerText.includes('24 小时 · 15 分钟桶')",'history states active range and bucket');
await range(0,60);await fixture({advance:true});await tap('刷新状态');
await expect("document.body.innerText.includes('你的编辑仍保留') && document.querySelector('.management-form [role=slider]').getAttribute('aria-valuenow')==='60' && document.querySelector('.management-form [role=slider]').getAttribute('aria-disabled')==='true'",'dirty draft survives changed revision');
await tap('加载设备新设置');
await expect("document.querySelector('.management-form [role=slider]').getAttribute('aria-valuenow')==='35' && document.querySelector('.management-form [role=slider]').getAttribute('aria-disabled')==='false'",'explicit reload adopts new revision');
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
await expect("document.body.innerText.includes('设备存储不可写') && document.querySelector('.management-form [role=slider]').getAttribute('aria-disabled')==='true'",'readonly storage disables editing');
await fixture({mode:'stale'});await tap('刷新状态');
await expect("document.body.innerText.includes('快照已过期') && document.querySelector('.management-form [role=slider]').getAttribute('aria-disabled')==='true'",'stale readback disables editing');
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
