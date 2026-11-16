#!/usr/bin/env bash
# Actual Vue console with an explicit RAM-only HTTP list fixture.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-metadata-export"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/metadata_export_ui_fixture.py" --frontend "$nexus_dir/target/frontend" --port 18812 >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
    if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
    sleep 0.1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
ego-browser nodejs <<'EOF'
const fs=await import('node:fs'), os=await import('node:os'), path=await import('node:path');
const name='NEXUS metadata export regression', base='http://127.0.0.1:18812';
const existing=(await listTaskSpaces()).find(s=>s.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing regression space needs user confirmation before control');
const task=await useOrCreateTaskSpace(name);cliLog({taskSpaceId:task.id});
const results=[], downloadDir=fs.mkdtempSync(path.join(os.tmpdir(),'nexus-metadata-downloads-'));
const downloadDirs=[downloadDir];
async function freshDownloadDir(){const dir=fs.mkdtempSync(path.join(os.tmpdir(),'nexus-metadata-downloads-'));downloadDirs.push(dir);await cdp('Page.setDownloadBehavior',{behavior:'allow',downloadPath:dir});}
await cdp('Emulation.setTimezoneOverride',{timezoneId:'Asia/Shanghai'});
async function fixture(value){return JSON.parse(await serverFetch(base+'/__qa/query',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function state(){return JSON.parse(await serverFetch(base+'/__qa/state',{headers:{'X-QA-Key':'isolated-ui-fixture-control'}}));}
async function expect(expression,label){for(let i=0;i<70;i++){if(await js(expression).catch(()=>false)){results.push(label);return;}await wait(0.1);}cliLog(await snapshotText());throw new Error('UI assertion failed: '+label);}
async function ready(){for(let i=0;i<70;i++){if(await js("!!document.querySelector('.list-filters select') && !document.querySelector('.list-filters select').disabled"))return;await wait(0.1);}throw new Error('List did not settle');}
async function tap(text){if(text==='导出元数据')await js("document.querySelector('.export-actions').open=true");for(let i=0;i<70;i++){if(await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.textContent.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))return;await wait(0.1);}throw new Error('Button unavailable: '+text);}
async function navigate(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
async function input(label,value){await ready();await js(`(()=>{const e=document.querySelector('input[aria-label="'+${JSON.stringify(label)}+'"]');e.value=${JSON.stringify(value)};e.dispatchEvent(new Event('input',{bubbles:true}));})()`);}
async function choose(label,value){await ready();await js(`(()=>{const e=document.querySelector('select[aria-label="'+${JSON.stringify(label)}+'"]');e.value=${JSON.stringify(value)};e.dispatchEvent(new Event('change',{bubbles:true}));})()`);await ready();}
function files(){return downloadDirs.flatMap(dir=>fs.readdirSync(dir).filter(file=>file.endsWith('.csv')).map(file=>path.join(dir,file)));}
async function downloaded(count,label){for(let i=0;i<70;i++){if(files().length===count){results.push(label);return;}await wait(0.1);}throw new Error('Download missing: '+label);}
await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');

// A reused tab may poll the new fixture before navigation stops the old app.
await fixture({reset:true});await cdp('Emulation.clearDeviceMetricsOverride');
await cdp('Page.setDownloadBehavior',{behavior:'allow',downloadPath:downloadDir});
await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
await expect("!!document.querySelector('.sidebar') && document.querySelectorAll('.message-row').length===30",'default list still paginates');await ready();
await expect("!!document.querySelector('input[aria-label=来源标识]') && !!document.querySelector('input[aria-label=收件开始]')",'source and local receipt window are available');
if((await state()).queryRequests.some(url=>url.includes('source=')||url.includes('receivedFrom=')))throw new Error('Default query must omit optional facets: '+JSON.stringify((await state()).queryRequests));results.push('default queries omit new optional filters');
await choose('消息状态','unread');await input('来源标识','manual');await input('收件开始','2026-11-14T16:00');await input('收件截止','2026-11-14T17:00');
await expect("document.querySelector('.filter-feedback')?.innerText==='筛选尚未应用。' && [...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='导出元数据').disabled",'unapplied draft cannot export a different result');
await tap('应用筛选');await expect("document.querySelector('.section-heading p')?.innerText==='31 条消息' && document.querySelector('.pagination > span')?.innerText==='1 / 2' && !document.querySelector('.filter-feedback')",'source and receipt window combine with unread state');await ready();
let request=(await state()).queryRequests.filter(url=>url.startsWith('/api/v1/messages?')).at(-1), query=new URL(request,base).searchParams;
if(query.get('receivedFrom')!=='2026-11-14T08:00:00.000Z'||query.get('receivedBefore')!=='2026-11-14T09:00:00.000Z'||query.get('source')!=='manual'||query.get('readStatus')!=='unread')throw new Error('Wrong public UTC query');results.push('local inputs send explicit UTC bounds and exact source');
await tap('下一页');await expect("document.querySelectorAll('.message-row').length===1 && document.querySelector('.pagination > span')?.innerText==='2 / 2'",'filtered pagination retains every facet');
await tap('导出元数据');await downloaded(1,'browser saves real message CSV');await ready();
let csv=fs.readFileSync(path.join(downloadDir,'messages-metadata.csv'),'utf8');
if(csv.split('\r\n').length!==33||!csv.includes('metadata-0')||!csv.includes('metadata-30')||csv.includes('metadata-31')||csv.includes('不导出'))throw new Error('Wrong message CSV content');results.push('CSV includes the entire filtered result beyond current page and excludes body');
let exported=(await state()).exportRequests.at(-1),exportQuery=new URL(exported,base).searchParams;
if(exportQuery.get('source')!=='manual'||exportQuery.get('readStatus')!=='unread'||exportQuery.has('page')||exportQuery.get('receivedBefore')!=='2026-11-14T09:00:00.000Z')throw new Error('Export lost active filters');results.push('export reuses filters and omits pagination');
await navigate('投递');await expect("document.querySelector('h1')?.innerText==='投递'",'delivery list opens');await ready();
await choose('投递状态','failed');await input('设备 ID','device-a');await input('收件开始','2026-11-14T16:00');await input('收件截止','2026-11-14T17:00');await tap('应用筛选');
await expect("document.querySelector('.section-heading p')?.innerText.startsWith('31 条投递') && document.querySelectorAll('tbody tr').length===30",'device and message receipt window combine with delivery state');await ready();
await tap('导出元数据');await downloaded(2,'browser saves real delivery CSV');await ready();
csv=fs.readFileSync(path.join(downloadDir,'deliveries-metadata.csv'),'utf8');
if(csv.split('\r\n').length!==33||!csv.includes('event-30')||csv.includes('event-31')||csv.includes('title'))throw new Error('Wrong delivery CSV content');results.push('delivery metadata includes all filtered rows and no title');
await navigate('消息');await expect("document.querySelector('input[aria-label=来源标识]')?.value==='manual' && document.querySelector('input[aria-label=收件截止]')?.value==='2026-11-14T17:00' && document.querySelector('.pagination > span')?.innerText==='2 / 2'",'navigation preserves message search and page');
await tap('刷新');await ready();await expect("document.querySelector('input[aria-label=来源标识]')?.value==='manual' && document.querySelector('.pagination > span')?.innerText==='2 / 2'",'manual refresh preserves applied facets');
await input('收件开始','2026-11-14T18:00');let reads=(await state()).queryRequests.length;await tap('应用筛选');
await expect("document.querySelector('.filter-feedback[role=alert]')?.innerText==='收件开始必须早于截止时间。'",'inverted range has local feedback');
if((await state()).queryRequests.length!==reads)throw new Error('Invalid range issued HTTP');results.push('invalid range does not issue a list request');
await input('收件开始','2026-11-14T16:00');await input('来源标识','missing');await tap('应用筛选');await expect("document.querySelector('h2')?.innerText==='没有符合条件的消息' && [...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='导出元数据').disabled",'empty filtered messages disable export');await ready();
await fixture({queryError:'messages'});await input('来源标识','manual');await tap('应用筛选');await expect("document.querySelector('h2')?.innerText==='消息读取失败' && document.querySelectorAll('.message-row').length===0 && [...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='导出元数据').disabled",'read failure hides stale rows and disables export');
await fixture({queryError:''});await tap('重试');await ready();await expect("document.querySelector('.section-heading p')?.innerText==='31 条消息' && document.querySelector('input[aria-label=来源标识]')?.value==='manual'",'read recovery keeps the selected source and range');
await freshDownloadDir();
for(const [mode,text]of [['large','结果超过 5000 条'],['error','CORE 处理失败'],['invalid','未能导出元数据']]){
 await fixture({exportMode:mode});await tap('导出元数据');await expect(`document.querySelector('.feedback.error')?.innerText.includes(${JSON.stringify(text)}) && ![...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='导出元数据').disabled`,'export failure is actionable: '+mode);
 if(files().length!==2)throw new Error('Failed export created a file');results.push('failed export creates no download: '+mode);
}
await freshDownloadDir();await fixture({exportMode:'success',exportDelay:1});let before=(await state()).exportRequests.length;await tap('导出元数据');
await expect("[...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='正在导出…')?.disabled && document.querySelector('.list-filters input').disabled",'in-flight export freezes repeated action and facets');
await js("[...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='正在导出…').click()");await downloaded(3,'slow export finishes once');await ready();
if((await state()).exportRequests.length!==before+1)throw new Error('Repeated click sent another export');results.push('repeated export click sends once');
await freshDownloadDir();await fixture({exportDelay:1});await tap('导出元数据');await expect("!![...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='正在导出…')",'export is pending before navigation');await navigate('投递');await wait(1.2);
if(files().length!==3)throw new Error('Abandoned export downloaded after navigation');results.push('navigation discards a delayed export');
await expect("document.querySelector('input[aria-label=\"设备 ID\"]')?.value==='device-a' && document.querySelector('.list-filters select')?.value==='failed'",'delivery facets survive a return visit');await ready();
await fixture({exportDelay:0,queryDelay:1});await input('设备 ID','device-b');await tap('应用筛选');
await expect("document.querySelector('.empty-state[role=status]')?.innerText==='正在加载投递…' && document.querySelector('.list-filters input').disabled",'filter loading clears old delivery rows');
await expect("document.querySelector('.section-heading p')?.innerText.startsWith('1 条投递')",'delayed delivery filter finishes');await ready();await fixture({queryDelay:0});
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && [...document.querySelectorAll('.list-filters input,.list-filters select,.filter-actions button')].every(e=>e.getBoundingClientRect().right<=390)",'all delivery facets and actions fit 390 CSS pixels');
cliLog({mobileScreenshot:await captureScreenshot()});await navigate('消息');await expect("document.documentElement.scrollWidth<=390 && [...document.querySelectorAll('.list-filters input,.filter-actions button')].every(e=>e.getBoundingClientRect().right<=390)",'all message facets and actions fit 390 CSS pixels');
await cdp('Emulation.clearDeviceMetricsOverride');await tap('清空筛选');await ready();await expect("document.querySelector('.list-filters select').value==='' && [...document.querySelectorAll('.list-filters input')].every(e=>e.value==='') && document.querySelector('.section-heading p')?.innerText==='34 条消息'",'clear resets state, facets and page together');
await freshDownloadDir();await fixture({exportDelay:1});await tap('导出元数据');await expect("!![...document.querySelectorAll('button')].find(b=>b.textContent.trim()==='正在导出…')",'export is pending before logout');await tap('退出登录');await wait(1.2);
await expect("!!document.querySelector('.login-card') && !document.querySelector('.sidebar') && document.querySelector('.token-label input').value===''",'logout discards credentials and delayed export');
if(files().length!==3)throw new Error('Logged out export downloaded');results.push('late export cannot create a file after logout');
await fixture({exportDelay:0,exportMode:'unauthorized'});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await ready();
await expect("document.querySelector('.list-filters select').value==='' && [...document.querySelectorAll('.list-filters input')].every(e=>e.value==='')",'new session starts with no message filters');
await navigate('投递');await expect("document.querySelector('.list-filters select').value==='' && [...document.querySelectorAll('.list-filters input')].every(e=>e.value==='')",'new session starts with no delivery filters');
await tap('导出元数据');await expect("!!document.querySelector('.login-card') && document.querySelector('.inline-error')?.innerText.includes('管理令牌无效')",'export 401 ends the session');
cliLog({passed:results.length,checks:results,downloadDirs,downloadedFiles:files(),scope:'actual Vue and real browser CSV download; RAM HTTP boundary only'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS metadata export regression',{keep:false}));
EOF
