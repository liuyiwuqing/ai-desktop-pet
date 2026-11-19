#!/usr/bin/env bash
# Actual Vue/strict CSP, loopback RAM only. No real service or device writes.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-console-states"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/console_states_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
  if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
  if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
  sleep .1
done
ego-browser nodejs <<'EOF'
async function waitForLogin(){for(let i=0;i<120;i++){if(await js("!!document.querySelector('.token-label input:not(:disabled)') && !document.querySelector('.login-card button.primary')?.disabled").catch(()=>false))return;await wait(.1);}throw new Error('Login form did not settle after session recovery');}
const base='http://127.0.0.1:18826',name='NEXUS console states';
const existing=(await listTaskSpaces()).find(s=>s.name===name);
if(existing&&existing.ownership!=='agent')throw new Error('Existing space needs user confirmation');
await useOrCreateTaskSpace(existing?.id??name);
await openOrReuseTab(base,{wait:true,timeout:15});
await cdp('Page.bringToFront');
await cdp('Emulation.setDeviceMetricsOverride',{width:1226,height:976,deviceScaleFactor:1,mobile:false});
await cdp('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'reduce'}]});
await cdp('Network.setCacheDisabled',{cacheDisabled:true});
await cdp('Page.addScriptToEvaluateOnNewDocument',{source:"window.qaViolations=[];document.addEventListener('securitypolicyviolation',e=>window.qaViolations.push(e.effectiveDirective+':'+e.blockedURI));"});
await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const results=[],fs=await import('node:fs'),shots='/tmp/nexus-console-states';fs.mkdirSync(shots,{recursive:true});
async function qa(value){return JSON.parse(await serverFetch(base+'/__qa/presentation',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));}
async function expect(expression,label){for(let i=0;i<80;i++){if(await js(expression).catch(()=>false)){results.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function nav(tab){await js(`document.querySelector('.app-layout > .sidebar [data-tab="${tab}"]').click()`);await expect(`document.querySelector('.app-layout > .sidebar [data-tab="${tab}"]').getAttribute('aria-current')==='page'&&!!document.querySelector('.content h1')`,'navigate '+tab);}
async function shot(name){await cdp('Page.bringToFront');const result=await cdp('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(shots+'/'+name+'.png',Buffer.from(result.data,'base64'));}
await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await click('.login-card button.primary');
await expect("!!document.querySelector('.app-layout')",'authenticated actual console');
await js("document.querySelector('.topbar button[aria-label=切换到浅色主题]')?.click()");
await expect("document.documentElement.dataset.theme==='light'",'state screenshots start in light mode');
await cdp('Emulation.setDeviceMetricsOverride',{width:1226,height:976,deviceScaleFactor:1,mobile:false});
const cases=[
 ['inbox','messages','.content > section','暂无消息'],
 ['deliveries','deliveries','.content > section','还没有投递记录'],
 ['devices','devices','.content > section','还没有设备'],
 ['connectors','connectors','.content > section > section','暂无 Halo 接入'],
 ['routing','routing','.content > section','暂无路由规则'],
 ['templates','templates','.content > section','暂无模板'],
 ['automation','automation/rules','.content > section','暂无自动化规则'],
 ['tools','tools','.content > section','暂无工具请求'],
 ['access','access/tokens','.content > section','暂无角色令牌'],
 ['memory','memory','.content > section','长期记忆未启用'],
 ['pet','pet','.content > section','CORE 专注档案'],
 ['overview','overview','.content > section','设备在线'],
];
for(const [tab,resource,scope,empty] of cases){
 await qa({path:'/api/v1/'+resource,mode:'success',delay:0});
 await nav(tab==='inbox'?'overview':'inbox');await nav(tab);
 await expect(`document.querySelector(${JSON.stringify(scope)})?.innerText.includes(${JSON.stringify(empty)})`,'successful '+tab+' state retains its content');
 const refresh=tab==='inbox'||tab==='deliveries'||tab==='devices'||tab==='connectors'?'.topbar button[aria-label="刷新"]':scope+' > .section-heading button';
 const kick=`(()=>{const b=document.querySelector(${JSON.stringify(refresh)});if(!b||b.disabled)return false;b.click();return true})()`;
 await qa({mode:'error',delay:1});
 await expect(kick,'refresh '+tab+' starts a real read');
 await expect(`!!document.querySelector(${JSON.stringify(scope+' .loading-state, '+scope+'[aria-busy=true], '+scope+' [aria-busy=true]')})`,'loading '+tab+' is exposed');
 await expect(`!!document.querySelector(${JSON.stringify(scope+' .console-state--error, .content > .console-state--error')})&&!document.querySelector(${JSON.stringify(scope+' .console-state--loading')})`,'failure '+tab+' replaces loading');
 await expect(`(()=>{const a=[...document.querySelectorAll(${JSON.stringify(scope+' .console-state--error, .content > .console-state--error')})];return a.length===1&&a[0].getAttribute('role')==='alert'&&a[0].innerText.includes('HTTP 503')})()`,'failure '+tab+' has one actual error reason');
 if(tab==='inbox'){await expect("!document.querySelector('.message-row,.empty-state')&&document.querySelector('.content > section .section-heading p')?.getAttribute('aria-hidden')==='true'",'message error has no false empty or duplicate subtitle');await expect("!document.querySelector('.content > section .loading-state')",'failed polling keeps the error without a duplicate loading panel');await shot('error-desktop');}
 if(tab==='tools')await expect("!document.querySelector('.task-form,.record-list .record-card')",'failed capabilities do not expose stale tool actions');
 await qa({mode:'success',delay:0});await expect(kick,'retry '+tab+' keeps existing refresh entry');
 await expect(`!document.querySelector(${JSON.stringify(scope+' .console-state--error, .content > .console-state--error')})&&document.querySelector(${JSON.stringify(scope)})?.innerText.includes(${JSON.stringify(empty)})`,'retry '+tab+' recovers the original content');
}
await qa({path:'/api/v1/messages',mode:'success',delay:0});await nav('inbox');
await expect("document.querySelector('.empty-state h2')?.innerText==='暂无消息'",'shared empty title');await shot('empty-desktop');
await nav('deliveries');
await expect("document.querySelector('.console-state-actions button')?.innerText==='查看消息'",'empty deliveries offers the next step');
await js("document.querySelector('.console-state-actions button').click()");
await expect("document.querySelector('.app-layout > .sidebar [data-tab=inbox]').getAttribute('aria-current')==='page' && document.querySelector('.content h1')?.innerText==='消息'",'empty deliveries action opens messages');
const filterTop=await js("document.querySelector('.list-filters').getBoundingClientRect().top");
await qa({delay:2});await expect("(()=>{const b=document.querySelector('.topbar button[aria-label=刷新]');if(!b||b.disabled)return false;b.click();return true})()",'visible refresh becomes available');
await expect("!!document.querySelector('.loading-state .n-skeleton')&&!document.querySelector('.empty-state')",'first-data loading has themed skeletons without a false empty state');
await expect(`Math.abs(document.querySelector('.list-filters').getBoundingClientRect().top-${filterTop})<1`,'loading does not shift the filter actions');await shot('loading-desktop');
await expect("!!document.querySelector('.empty-state')",'slow empty read completes');await qa({delay:0});
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:false});await wait(.3);await shot('empty-mobile');
await qa({mode:'error'});await expect("(()=>{const b=document.querySelector('.topbar button[aria-label=刷新]');if(!b||b.disabled)return false;b.click();return true})()",'visible refresh becomes available');
await expect("!!document.querySelector('.global-feedback.error')",'phone read failure remains visible');
await expect("document.documentElement.scrollWidth<=390&&document.querySelector('.console-state-actions button')?.getBoundingClientRect().right<=390",'phone error keeps text and retry within viewport');await shot('error-mobile');
await js("document.querySelector('.topbar button[aria-label=切换到深色主题]').click()");await expect("document.documentElement.dataset.theme==='dark'",'dark states use the existing theme');await shot('error-mobile-dark');
await expect("[...document.querySelectorAll('.spinning')].every(e=>parseFloat(getComputedStyle(e).animationDuration)<=.01)",'reduced motion disables state spinner animation');
await expect("!window.qaViolations.length",'shared state styles respect CSP');
cliLog({passed:results.length,checks:results,screenshots:shots,scope:'actual Vue, loopback RAM HTTP; no real service or device writes'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS console states',{keep:false}));
EOF
cp /tmp/nexus-console-states/*.png "$output_dir/"
