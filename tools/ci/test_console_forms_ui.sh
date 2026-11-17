#!/usr/bin/env bash
# Actual Vue + strict CSP + RAM HTTP. No database, broker, real message or device.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="$repo_root/firmware/build-ui-console-forms"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/management_ui_fixture.py" --frontend "$nexus_dir/target/frontend" --port 18824 >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
  if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
  if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
  sleep .1
done
rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"
{
cat "$repo_root/tools/ci/naive_ui_helpers.js"
cat <<'EOF'
const fs=await import('node:fs'),path=await import('node:path'),os=await import('node:os');
const name='NEXUS compact console forms',base='http://127.0.0.1:18824';
const prior=(await listTaskSpaces()).find(s=>s.name===name);if(prior&&prior.ownership!=='agent')throw new Error('Space requires confirmation');
await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await cdp('Runtime.enable');
await cdp('Page.addScriptToEvaluateOnNewDocument',{source:"window.formCsp=[];document.addEventListener('securitypolicyviolation',e=>formCsp.push(e.effectiveDirective));window.formErrors=[];window.addEventListener('error',e=>formErrors.push(e.message));window.addEventListener('unhandledrejection',e=>formErrors.push(String(e.reason)));window.formWrites=[];const rawFetch=window.fetch;window.fetch=(url,options)=>{if(options?.method&&options.method!=='GET')formWrites.push(String(url));return rawFetch(url,options)};"});
await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const checks=[];
async function expect(expression,label){for(let i=0;i<100;i++){if(await js(expression).catch(()=>false)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());cliLog(await js("({width:innerWidth,scroll:document.documentElement.scrollWidth,focus:document.activeElement?.tagName,dates:[...document.querySelectorAll('.n-date-panel')].map(e=>e.getBoundingClientRect().toJSON())})"));throw new Error(label);}
async function state(){return JSON.parse(await serverFetch(base+'/__qa/state',{headers:{'X-QA-Key':'isolated-ui-fixture-control'}}));}
async function qa(body){return JSON.parse(await serverFetch(base+'/__qa/state',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)}));}
async function tap(text){await expect(`(()=>{const b=[...document.querySelectorAll('button')].find(e=>e.textContent.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+text);}
async function nav(id){await js(`document.querySelector('.app-layout > .sidebar [data-tab=${id}]').click()`);}
async function viewport(width,height){await cdp('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<761});await wait(.2);}
const shots='/tmp/nexus-console-forms';fs.mkdirSync(shots,{recursive:true});
async function shot(name,width,height){await viewport(width,height);await js('window.scrollTo(0,0)');const p=path.join(shots,name+'.png'),r=await cdp('Page.captureScreenshot',{format:'png'});fs.writeFileSync(p,Buffer.from(r.data,'base64'));return p;}
await expect("document.title==='NEXUS · 桌面伙伴管理台' && !!document.querySelector('.login-card.n-form') && !!document.querySelector('.login-card [role=checkbox]')",'login uses Naive form and checkbox');
await tap('进入消息中心');await expect("document.querySelector('.login-card .n-form-item-feedback')?.textContent.includes('请填写') && !formWrites.length",'empty token shows field validation before authentication');
await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await tap('进入消息中心');await expect("!!document.querySelector('.list-filters.n-form') && !!document.querySelector('.inbox-layout.short-list')",'short inbox and Naive search form render');
if(await js("document.documentElement.dataset.theme==='dark'"))await click('[aria-label=切换到浅色主题]');
await viewport(1440,900);await expect("document.querySelector('.filter-expand').getAttribute('aria-expanded')==='false' && document.querySelector('.advanced-filters').getBoundingClientRect().height===0",'message receipt window starts collapsed');await shot('message-common-desktop',1440,900);await click('.filter-expand');await expect("document.querySelector('.filter-expand').getAttribute('aria-expanded')==='true' && document.querySelector('.advanced-filters').getBoundingClientRect().height>0",'more filters reveal receipt dates');await expect("getComputedStyle(document.querySelector('.content')).paddingLeft==='24px' && document.querySelector('.inbox-layout').offsetHeight===340",'desktop gutter and short inbox are compact');
await ui.choose('.n-select[aria-label=消息状态]','未读');await expect("document.querySelector('.n-select[aria-label=消息状态]').textContent.trim()==='未读'",'status changes through actual select menu');
await expect("document.querySelector('.list-filters .n-date-picker input')?.disabled===false",'calendar field is ready');await js("(()=>{const input=document.querySelector('.list-filters .n-date-picker input');input.focus();input.click();})()");await expect("!!document.querySelector('.n-date-panel')",'date field opens Naive calendar');await js("document.querySelector('.list-filters .n-date-picker input').focus()");await pressKey('Escape');await expect("!document.querySelector('.n-date-panel')",'Escape closes the calendar');
await expect("[...document.querySelectorAll('style[cssr-id]')].every(e=>!!e.nonce) && !formCsp.length",'calendar and virtual menu styles respect the page nonce');
await nav('devices');await tap('登记设备');await fillInput('.inline-form input[aria-label="设备 ID"]','bad!');await fillInput('.inline-form input[aria-label=名称]','校验测试');await tap('登记');await expect("document.querySelector('.inline-form .n-form-item-feedback')?.textContent.includes('格式') && !formWrites.some(p=>p.endsWith('/devices'))",'invalid device ID never reaches registration API');await tap('收起');
await qa({mode:'fresh'});await tap('设置与状态');await expect("document.querySelectorAll('.management-form [role=slider]').length===2",'settings use Naive sliders');
await js("(()=>{const s=document.querySelector('.management-form [role=slider]');s.focus();s.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}));[...document.querySelectorAll('.management-form [role=checkbox]')].find(e=>e.textContent.includes('定时免打扰')).click();})()");
await expect("document.querySelector('.management-form [role=slider]').getAttribute('aria-valuenow')==='26' && document.querySelectorAll('.management-form .n-time-picker').length===2",'keyboard changes volume and schedule checkbox reveals time pickers');
await fillInput('.management-form .n-time-picker input','22:15');await js("document.querySelector('.management-form .n-time-picker input').dispatchEvent(new Event('input',{bubbles:true}));document.querySelector('.management-form .n-time-picker input').blur()");await tap('保存到设备');
await expect("document.querySelector('.management-record')?.textContent.includes('等待下发')",'settings submission is recorded');
const config=(await state()).submissions.at(-1);if(config?.config.volume!==26||config.config.dnd.startMinute!==1335||config.config.dnd.endMinute!==480||config.config.timezone!=='Asia/Shanghai')throw new Error('Naive settings changed the device contract: '+JSON.stringify(config));checks.push('volume and local DND times preserve the submitted contract');
await shot('settings-desktop',1440,900);await js("document.querySelector('[aria-label=切换到深色主题]').click()");await expect("document.documentElement.dataset.theme==='dark'",'Naive fields follow dark theme');await shot('settings-dark',1440,900);
await tap('固件升级');await qa({otaMode:'fresh'});await tap('刷新状态');await ui.choose('#remote-ota-panel .n-select','0.4.1 · release-ui');await tap('登记版本');
const bad=path.join(os.tmpdir(),'nexus-form-invalid-'+Date.now()+'.json');fs.writeFileSync(bad,'not-json');await uploadFile('#remote-ota-panel input[type=file]',bad);await expect("document.querySelector('#remote-ota-panel .n-upload') && document.querySelector('#remote-ota-panel').textContent.includes('无法解析 manifest JSON')",'Naive upload retains manifest parse feedback');fs.unlinkSync(bad);
await viewport(390,844);await expect("document.documentElement.scrollWidth<=390",'device fields fit 390px');
await js("document.querySelector('.n-drawer-mask')?.click()");await click('[aria-label=打开导航]');await js("document.querySelector('[role=dialog] [data-tab=inbox]').click()");await expect("document.querySelector('.content h1')?.textContent==='消息' && !document.querySelector('[role=dialog]')",'mobile navigation returns to message filters');await expect("document.activeElement===document.querySelector('.content h1')",'mobile navigation restores heading focus');await expect("document.querySelector('.filter-expand').getAttribute('aria-expanded')==='false'",'return visit keeps only common filters visible');await shot('message-common-mobile',390,844);await click('.filter-expand');
await expect("document.querySelector('.list-filters .n-date-picker input')?.disabled===false",'calendar field is ready');await js("(()=>{const input=document.querySelector('.list-filters .n-date-picker input');input.focus();input.click();})()");await expect("(()=>{const r=document.querySelector('.n-date-panel')?.getBoundingClientRect();return !!r&&r.left>=0&&r.right<=390})()",'mobile calendar stays inside viewport');await js("document.querySelector('.list-filters .n-date-picker input').focus()");await pressKey('Escape');await expect("!document.querySelector('.n-date-panel')",'Escape closes the calendar');
await ui.choose('.n-select[aria-label=消息状态]','全部');await expect("document.documentElement.scrollWidth<=390",'mobile select remains usable');await expect("(()=>{const h=document.querySelector('.short-list .message-list').offsetHeight;return h>0&&h<240})()",'mobile short inbox shrinks to its content');await shot('filters-mobile',390,844);
await expect("!formCsp.length && !formErrors.length && !document.querySelector('vite-error-overlay')",'all tested fields and popups have no CSP or runtime errors');
const errors=(await drainEvents()).filter(e=>e.method==='Runtime.consoleAPICalled'&&e.params.type==='error');if(errors.length)throw new Error('Browser console errors: '+errors.map(e=>e.params.args.map(a=>a.description||a.value).join(' ')).join('\n'));
cliLog({passed:checks.length,checks,screenshots:shots,scope:'actual Vue, RAM HTTP and real CSP; no real service or device writes'});
EOF
} | ego-browser nodejs
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS compact console forms',{keep:false}));
EOF
cp /tmp/nexus-console-forms/*.png "$output_dir/"
