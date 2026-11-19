#!/usr/bin/env bash
# Actual Vue console; only the HTTP boundary and failure outcomes are fixtures.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-message-compose"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/management_ui_fixture.py" --frontend "$nexus_dir/target/frontend" --port 18804 >"$output_dir/fixture.log" 2>&1 &
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
const base='http://127.0.0.1:18804', name='NEXUS message submission regression';
const existing=(await listTaskSpaces()).find(space=>space.name===name);
if(existing && existing.ownership!=='agent')throw new Error('Existing regression space needs user confirmation before control');
const task=await useOrCreateTaskSpace(name);
cliLog({taskSpaceId:task.id});
const results=[];
async function fixture(value) {
  return JSON.parse(await serverFetch(base+'/__qa/state',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(value)}));
}
async function state() {return JSON.parse(await serverFetch(base+'/__qa/state',{headers:{'X-QA-Key':'isolated-ui-fixture-control'}}));}
async function expect(expression,label) {
  for(let i=0;i<50;i++){if(await js(expression).catch(()=>false)){results.push(label);return;}await wait(0.1);}
  const observed=await state();cliLog({failed:label,requests:observed.messageSubmissions.length,saved:observed.manualMessages.length});
  throw new Error('UI assertion failed: '+label);
}
async function tap(text) {
  // 面板由按需加载的 chunk 渲染，按钮可能晚一拍出现；有限次重试，断言仍是"存在且可点"。
  for(let i=0;i<50;i++) {
    if(await js(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()===${JSON.stringify(text)});if(!b||b.disabled)return false;b.click();return true;})()`))return;
    await wait(0.1);
  }
  throw new Error('Button unavailable: '+text);
}
async function draft(title,content) {
  await tap('发送测试消息');await fillInput('.inline-form input',title);await fillInput('.inline-form textarea',content);
}
async function openSelect(selector){await expect(`(()=>{const s=document.querySelector('${selector} .n-base-selection');return !!s&&!s.classList.contains('n-base-selection--disabled')})()`,'select '+selector+' is enabled');if(!(await js(`document.querySelector('${selector} .n-base-selection')?.classList.contains('n-base-selection--active')`)))await js(`document.querySelector('${selector} .n-base-selection').click()`);await expect(`document.querySelector('${selector} .n-base-selection')?.classList.contains('n-base-selection--active')`,'select '+selector+' is open');}
async function chooseVisibleOption(label){const found=await js(`(()=>{const o=[...document.querySelectorAll('.n-base-select-option')].filter(o=>o.offsetParent!==null).filter(o=>o.innerText.trim()===${JSON.stringify(label)}).pop();if(!o)return false;o.click();return true;})()`);if(!found)throw new Error('option '+label+' is not visible');}
async function pickPriority(label){await openSelect('.inline-form .priority-select');await expect(`[...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.trim()===${JSON.stringify(label)}&&o.offsetParent!==null)`,'priority option '+label+' is offered');await chooseVisibleOption(label);await expect(`document.querySelector('.inline-form .priority-select')?.innerText.includes(${JSON.stringify(label)})`,'priority shows '+label);}
async function closeSelect(selector){await js(`(()=>{const s=document.querySelector('${selector} .n-base-selection');if(s?.classList.contains('n-base-selection--active'))s.click();})()`);await expect(`!document.querySelector('${selector} .n-base-selection')?.classList.contains('n-base-selection--active')`,'select '+selector+' is closed');}
async function pickDevice(label){
  await openSelect('.inline-form .device-select');
  if(await js("[...document.querySelectorAll('.n-base-select-option')].some(o=>o.offsetParent!==null&&o.innerText.includes('已停用设备'))"))throw new Error('Disabled device must not be offered');
  await expect(`[...document.querySelectorAll('.n-base-select-option')].some(o=>o.innerText.trim()===${JSON.stringify(label)}&&o.offsetParent!==null)`,'device '+label+' is offered');
  await chooseVisibleOption(label);
  await expect(`document.querySelector('.inline-form .device-select')?.innerText.includes(${JSON.stringify(label)})`,'device shows '+label);
  await closeSelect('.inline-form .device-select');
  await expect("document.querySelector('.inline-form .form-footer')?.innerText.includes('仅发送到 1 台设备')",'footer names the selected device scope');
}
async function send() {
  if(!await js("(()=>{const b=document.querySelector('.inline-form button.primary');if(!b||b.disabled)return false;b.click();return true;})()"))throw new Error('Send button unavailable');
}
async function login() {
  await expect("!!document.querySelector('.login-card')",'login is shown');
  await waitForLogin();await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");
  await expect("!!document.querySelector('.sidebar')",'authenticated console is shown');
}
async function acknowledged(label) {await expect("!document.querySelector('.inline-form') && document.body.innerText.includes('测试消息已保存')",label);}
await fixture({reset:true,messageReset:true,messageDevices:true});
await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});
const builtScript=(await serverFetch(base+'/index.html')).match(/src="([^"]+\.js)"/)?.[1];
if(!builtScript || !await js(`[...document.scripts].some(s=>new URL(s.src).pathname===${JSON.stringify(builtScript)})`))throw new Error('Page must load the current compiled console');
await login();
await draft('丢失响应后的同一消息','只应保存一次');await pickPriority('紧急');await pickDevice('第二台设备（dodo-ui-second）');await fixture({messageNext:'save_then_drop'});await send();
await expect("document.body.innerText.includes('结果尚未确认') && !!document.querySelector('.inline-form') && document.querySelector('.inline-form .device-select .n-base-selection')?.classList.contains('n-base-selection--disabled') && document.querySelector('.inline-form .device-select')?.innerText.includes('第二台设备')",'response loss preserves the form, payload and target devices');
await send();await acknowledged('explicit retry is acknowledged');
let observed=await state();
const targeted=['dodo-ui-second'];
if(observed.messageSubmissions.length!==2 || observed.manualMessages.length!==1 || observed.messageSubmissions[0].eventId!==observed.messageSubmissions[1].eventId || observed.messageSubmissions[0].priority!=='critical' || JSON.stringify(observed.messageSubmissions[0].devices)!==JSON.stringify(targeted) || JSON.stringify(observed.messageSubmissions[1].devices)!==JSON.stringify(targeted))
  throw new Error('One logical submission must retain its event ID, priority and target devices and produce one saved message; observed '+JSON.stringify({requests:observed.messageSubmissions.length,saved:observed.manualMessages.length,sameId:observed.messageSubmissions[0]?.eventId===observed.messageSubmissions[1]?.eventId,priority:observed.messageSubmissions[0]?.priority,devices:observed.messageSubmissions[0]?.devices}));
results.push('saved response loss retries the same event ID, priority and target devices and saves one message');
await expect("document.querySelector('#last-send-receipt')?.innerText.includes('刚发送消息的投递状态') && document.querySelector('#last-send-receipt').innerText.includes('共 2 条投递') && document.querySelector('#last-send-receipt').innerText.includes('用户已读 1') && document.querySelector('#last-send-receipt').innerText.includes('dodo-ui-fixture')",'acknowledged send shows per-device delivery status');
observed=await state();
if(!observed.receiptRequests.at(-1)?.includes('/messages/manual-1/deliveries'))throw new Error('Acknowledged send did not read the new message receipts: '+JSON.stringify(observed.receiptRequests));
results.push('receipt view reads the acknowledged message');
const receiptCount=observed.receiptRequests.length;
await tap('刷新投递状态');
for(let i=0;i<20;i++){if((await state()).receiptRequests.length>receiptCount)break;await wait(.1);}
if((await state()).receiptRequests.length<=receiptCount)throw new Error('Receipt refresh did not re-read the same message');
results.push('receipt refresh re-reads the same message');
await draft('下一条有意发送的消息','新的逻辑提交');await expect("document.querySelector('.inline-form .form-footer')?.innerText.includes('未选择设备时按当前路由发送')",'a new draft returns to the default target scope');await send();await acknowledged('next intentional submission succeeds');
observed=await state();
if(observed.manualMessages.length!==2 || observed.messageSubmissions[2].eventId===observed.messageSubmissions[1].eventId)throw new Error('New logical submission reused an acknowledged ID');
if(observed.messageSubmissions[2].priority!=='normal')throw new Error('A new test message must start from the normal priority; observed '+observed.messageSubmissions[2].priority);
if('devices' in observed.messageSubmissions[2])throw new Error('The default target scope must omit devices and keep the existing routing');
results.push('acknowledged submission releases its ID and resets the priority and target devices for the next message');
await draft('未保存就断线','导航后仍保留请求');await pickPriority('高');await pickDevice('第二台设备（dodo-ui-second）');await fixture({messageNext:'drop_before_save'});await send();
await expect("document.querySelector('.inline-form input')?.readOnly && document.querySelector('.inline-form textarea')?.readOnly && document.querySelector('.inline-form .priority-select .n-base-selection')?.classList.contains('n-base-selection--disabled') && document.querySelector('.inline-form .priority-select')?.innerText.includes('高') && document.querySelector('.inline-form .device-select .n-base-selection')?.classList.contains('n-base-selection--disabled') && document.querySelector('.inline-form .device-select')?.innerText.includes('第二台设备') && document.querySelector('.inline-form button.primary')?.innerText==='重试同一消息'",'unknown submission freezes its payload, priority, target devices and offers explicit retry');
await click('.sidebar nav button[data-tab=devices]');await expect("document.querySelector('h1')?.innerText==='设备'",'navigation remains available');await click('.sidebar nav button[data-tab=inbox]');
await expect("document.querySelector('.inline-form input')?.value==='未保存就断线' && document.querySelector('.inline-form textarea')?.value==='导航后仍保留请求' && document.querySelector('.inline-form input')?.readOnly && document.querySelector('.inline-form .priority-select')?.innerText.includes('高') && document.querySelector('.inline-form .device-select')?.innerText.includes('第二台设备')",'unknown payload, priority and target devices survive tab navigation');
await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
await expect("document.documentElement.scrollWidth<=390 && document.querySelector('.inline-form .scope-note[role=alert]')?.innerText.includes('发送结果尚未确认')",'unknown result fits 390 CSS px and is announced assertively');
await cdp('Emulation.clearDeviceMetricsOverride');
await js("(()=>{const i=document.querySelector('.inline-form input');i.value='不能改变已冻结的请求';i.dispatchEvent(new Event('input',{bubbles:true}));})()");
await send();await acknowledged('retry after no server save succeeds');
observed=await state();
const before=observed.messageSubmissions[3], after=observed.messageSubmissions[4];
if(JSON.stringify(before)!==JSON.stringify(after) || observed.manualMessages.length!==3 || JSON.stringify(before.devices)!==JSON.stringify(targeted))throw new Error('Unknown request payload or target devices changed or were not retried exactly');
results.push('retry reuses the frozen payload even if form input is changed programmatically');
await draft('明确拒绝的消息','允许修改后重新提交');await fixture({messageNext:'reject'});await send();
await expect("document.body.innerText.includes('请检查填写的内容') && !document.querySelector('.inline-form input')?.readOnly && !document.querySelector('.inline-form button.primary')?.disabled",'definitive rejection keeps an editable draft');
await fillInput('.inline-form input','修改后的消息');await send();await acknowledged('corrected rejected draft succeeds');
observed=await state();
if(observed.messageSubmissions[5].eventId===observed.messageSubmissions[6].eventId || observed.manualMessages.length!==4 || observed.messageSubmissions[6].title!=='修改后的消息')throw new Error('Rejected submission did not release its ID');
results.push('definitive rejection uses a fresh ID for corrected input');
await draft('服务端返回 503','服务端可能已经保存');await fixture({messageNext:'save_then_500'});await send();
await expect("document.querySelector('.inline-form input')?.readOnly && document.body.innerText.includes('发送结果尚未确认')",'server error is treated as uncertain');
await send();await acknowledged('retry after server error succeeds');
observed=await state();
if(observed.messageSubmissions[7].eventId!==observed.messageSubmissions[8].eventId || observed.manualMessages.length!==5)throw new Error('Server error caused a duplicate');
results.push('server error keeps the original submission ID');
await draft('无效确认正文','不能误报成功');await fixture({messageNext:'invalid_ack'});await send();
await expect("document.querySelector('.inline-form input')?.readOnly && document.body.innerText.includes('发送结果尚未确认')",'invalid acknowledgment is treated as uncertain');
await send();await acknowledged('retry resolves invalid acknowledgment');
observed=await state();
if(observed.messageSubmissions[9].eventId!==observed.messageSubmissions[10].eventId || observed.manualMessages.length!==6)throw new Error('Invalid acknowledgment caused a duplicate');
results.push('invalid acknowledgment does not release the submission ID');
await draft('退出时清除的内容','不保留到新会话');await fixture({messageNext:'drop_before_save'});await send();
await expect("document.querySelector('.inline-form input')?.readOnly",'logout case first has an unknown submission');
await tap('退出登录');await login();
await expect("!document.querySelector('.inline-form') && !document.body.innerText.includes('退出时清除的内容')",'logout clears the pending form and private content');
await draft('新登录的消息','新的会话');await send();await acknowledged('new login may submit a new message');
observed=await state();
if(observed.messageSubmissions[11].eventId===observed.messageSubmissions[12].eventId || observed.manualMessages.length!==7)throw new Error('Logout kept an old unknown request');
results.push('new login does not replay the earlier uncertain submission');
await draft('认证失效的消息','自动退出也需清除');await fixture({messageNext:'unauthorized'});await send();
await expect("!!document.querySelector('.login-card') && document.querySelector('.token-label input').value===''",'401 logs out and clears the token');await login();
await expect("!document.querySelector('.inline-form') && !document.body.innerText.includes('认证失效的消息')",'401 logout clears the pending payload');
await expect("[...Object.values(localStorage),...Object.values(sessionStorage)].every(value=>!value.includes('ui-regression-fixture-token-at-least-32-characters') && !value.includes('不保留到新会话') && !value.includes('认证失效的消息'))",'submission data and token are not placed in browser storage');
cliLog({passed:results.length,checks:results,scope:'actual Vue bundle and explicit HTTP outcomes; no database, broker or device'});
EOF
# Complete only after the preceding browser checks have succeeded.
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS message submission regression',{keep:false}));
EOF
