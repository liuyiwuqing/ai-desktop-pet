#!/usr/bin/env bash
# Actual Vue, strict page CSP and loopback RAM fixtures; no real services or devices.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-console-theme"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/overview_ui_fixture.py" --frontend "$nexus_dir/target/frontend" --port 18823 >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
  if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
  if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
  sleep .1
done
ego-browser nodejs <<'EOF'
const base='http://127.0.0.1:18823',name='NEXUS console theme';
const prior=(await listTaskSpaces()).find(s=>s.name===name);
if(prior&&prior.ownership!=='agent')throw new Error('Existing space needs user confirmation');
await useOrCreateTaskSpace(prior?.id??name);
await openOrReuseTab(base,{wait:true,timeout:15});
await cdp('Network.setCacheDisabled',{cacheDisabled:true});
await cdp('Page.addScriptToEvaluateOnNewDocument',{source:"window.qaViolations=[];document.addEventListener('securitypolicyviolation',e=>window.qaViolations.push(e.effectiveDirective+':'+e.blockedURI));"});
await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const fs=await import('node:fs'),path=await import('node:path');
const screenshots='/tmp/nexus-console-theme';fs.mkdirSync(screenshots,{recursive:true});
const checks=[];
async function expect(expression,label){for(let i=0;i<80;i++){if(await js(expression).catch(()=>false)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function shot(name,width,height=1000,mobile=false){await cdp('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile});await wait(.3);const result=await cdp('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(path.join(screenshots,name+'.png'),Buffer.from(result.data,'base64'));}
await expect("!!document.querySelector('.login-card .n-input input[required]')",'login keeps native required validation');
await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await click('.login-card button.primary');
await expect("!!document.querySelector('.app-layout')",'authenticated console loads');
await expect("document.querySelectorAll('.app-layout > .sidebar [data-tab]').length===12 && document.querySelectorAll('.app-layout > .sidebar .nav-group').length===3",'all twelve pages remain in three groups');
await expect("[...document.querySelectorAll('.app-layout > .sidebar nav button')].every(b=>!!b.querySelector('svg'))",'navigation uses one SVG icon family');
await expect("[...document.querySelectorAll('style[cssr-id]')].every(s=>s.nonce===document.querySelector('meta[name=nexus-style-nonce]').content && !!s.sheet)",'Naive styles are authorized by this document nonce');
/* 十二页标题指纹：App.vue 的 focusContent 靠 .content h1 找落点，标题退化或文案漂移会让焦点静默落回 body。 */
const pageHeadings=[['overview','运行概览'],['inbox','消息'],['devices','设备'],['pet','一起成长'],['connectors','接入'],['deliveries','投递'],['routing','路由'],['templates','模板'],['automation','自动化'],['memory','长期记忆'],['tools','Mac 工具'],['access','权限与审计']];
async function settle(expression){for(let i=0;i<80;i++){if(await js(expression).catch(()=>false))return true;await wait(.1);}return false;}
/* 导航按钮会随角色数据重渲染，按当前 DOM 里的活动节点点击，避免点到已脱离文档的旧节点。 */
async function goTab(id){const ok=await settle(`(()=>{const b=[...document.querySelectorAll('.app-layout > .sidebar nav button')].find(b=>b.dataset.tab===${JSON.stringify(id)});if(!b||b.disabled)return false;b.click();return true;})()`);if(!ok)throw new Error('page '+id+' navigation entry is not clickable');}
for(const [id,heading] of pageHeadings){await goTab(id);const ok=await settle(`(()=>{const h=document.querySelector('.content h1');return !!h&&h.innerText.trim()===${JSON.stringify(heading)}&&document.activeElement===h})()`);if(!ok){const seen=await js("document.querySelector('.content h1')?.innerText??'(no h1)'");const focus=await js("document.activeElement===document.body?'body':(document.activeElement?.tagName??'(none)')");throw new Error(`page ${id} heading/focus: h1=${JSON.stringify(seen)} active=${focus}`);}checks.push('page '+id+' h1 is '+heading+' and holds focus');}
/* 后面的导出断言依赖消息页的 .export-actions，回到登录后的原点，避免改变现有步骤的起点。 */
await goTab('inbox');await settle("document.querySelector('.content h1')?.innerText==='消息' && !!document.querySelector('.export-actions') && !document.querySelector('.loading-state') && !!document.querySelector('.message-list .message-row, .empty-state')");await js("document.activeElement instanceof HTMLElement && document.activeElement.blur()");
await expect("document.querySelector('.export-actions')?.open===false",'secondary export actions start collapsed');
await click('.export-actions summary');await expect("document.querySelector('.export-menu button').getBoundingClientRect().width>0",'export actions open through their visible disclosure');
await pressKey('Escape');await expect("!document.querySelector('.export-actions').open && document.activeElement.tagName==='SUMMARY'",'Escape closes exports and returns focus');
await shot('messages-desktop',1440);
await click('.sidebar [data-tab=devices]');await expect("document.querySelector('h1')?.textContent==='设备' && !!document.querySelector('.device-card .n-tag')",'device card keeps identity and online state');
await shot('devices-desktop',1440);
await shot('devices-mobile',390,844,true);
await expect("document.documentElement.scrollWidth<=390 && getComputedStyle(document.querySelector('.app-layout > .sidebar')).display==='none'",'mobile removes the scrolling navigation strip');
await click('[aria-label=打开导航]');await expect("!!document.querySelector('[role=dialog]')",'mobile opens a real drawer dialog');
await expect("document.querySelectorAll('[role=dialog] [data-tab]').length===12",'all navigation entries are available in the drawer');
await shot('navigation-mobile',390,844,true);
await pressKey('Escape');await expect("!document.querySelector('[role=dialog]') && document.activeElement.getAttribute('aria-label')==='打开导航'",'Escape closes drawer and restores menu focus');
await expect("!document.querySelector('.n-drawer-mask')",'drawer overlay finishes closing before another click');
await click('[aria-label=打开导航]');await expect("(()=>{const r=document.querySelector('[role=dialog] [data-tab=inbox]')?.getBoundingClientRect();return !!r&&r.left>=0&&r.right<=280})()",'drawer navigation is visible after its entrance transition');await click('[role=dialog] [data-tab=inbox]');await expect("!document.querySelector('[role=dialog]') && document.activeElement.tagName==='H1'",'drawer navigation closes and moves focus into the page');
await click('.export-actions summary');await expect("(()=>{const r=document.querySelector('.export-menu').getBoundingClientRect();return r.left>=0&&r.right<=390})()",'mobile export menu stays inside the viewport');
await click('h1');await expect("!document.querySelector('.export-actions').open",'outside pointer closes exports');
await shot('messages-mobile',390,844,true);
await cdp('Emulation.setDeviceMetricsOverride',{width:1440,height:1000,deviceScaleFactor:1,mobile:false});await wait(.2);
await click('.app-layout > .sidebar [data-tab=overview]');await expect("!!document.querySelector('[data-metric=online]')",'overview reads actual fixture snapshot');
await shot('overview-desktop',1440);
await expect("window.qaViolations.length===0",'login, navigation, drawer and overview run without CSP violations');
const current=(await serverFetch(base+'/index.html')).match(/nexus-style-nonce" content="([^"]+)/)[1];
if(current===await js("document.querySelector('meta[name=nexus-style-nonce]').content"))throw new Error('Nonce was reused across requests');checks.push('each HTML response has an independent nonce');
cliLog({passed:checks.length,checks,screenshots,scope:'actual Vue and page CSP with RAM-only HTTP; no CORE/database/Broker/device'});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS console theme',{keep:false}));
EOF
mkdir -p "$output_dir/screenshots"
cp /tmp/nexus-console-theme/*.png "$output_dir/screenshots/"
