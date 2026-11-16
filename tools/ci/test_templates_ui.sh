#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-templates"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/templates_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
 if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
 if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
 sleep .1
done
ego-browser nodejs <<'EOF'
const name='NEXUS template management',base='http://127.0.0.1:18818';const prior=(await listTaskSpaces()).find(s=>s.name===name);if(prior&&prior.ownership!=='agent')throw new Error('Space requires confirmation');await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});let checks=[];
async function qa(body={}){return JSON.parse(await serverFetch(base+'/__qa/operations',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)}));}
async function expect(e,label){for(let i=0;i<100;i++){if(await js(e).catch(()=>false)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label){await expect(`(()=>{const b=[...document.querySelectorAll('.content button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
async function nav(label){await js(`(()=>{[...document.querySelectorAll('.sidebar nav button')].find(b=>b.textContent.trim().endsWith(${JSON.stringify(label)})).click();})()`);}
await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});await qa({reset:true});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login');await nav('模板');await expect("document.querySelector('.content').textContent.includes('暂无模板')",'empty templates');await tap('创建模板');await fillInput('[aria-label=模板编辑] input','评论卡片');await nav('消息');await nav('模板');await expect("document.querySelector('[aria-label=模板编辑] input')?.value==='评论卡片'",'template draft preserved');await tap('预览当前草稿');await expect("document.querySelector('[aria-label=模板预览结果]')?.textContent.includes('预览标题')",'variables rendered');await expect("document.querySelector('.content').textContent.includes('无提示音、显示 8 秒')",'supported device limits visible');await qa({next_write:'drop_after'});await tap('保存模板');await expect("document.querySelector('[aria-label=模板编辑]')?.textContent.includes('未确认')",'save response loss retains submission');await tap('重试原模板保存');await expect("!document.querySelector('[aria-label=模板编辑]') && document.querySelector('.record-card h2')?.textContent==='评论卡片'",'retry saves once');let state=await qa();let writes=state.requests.filter(r=>r.method==='PUT'&&r.path.includes('/templates/'));if(writes.length!==2||JSON.stringify(writes[0])!==JSON.stringify(writes[1])||state.templates.length!==1)throw new Error('Template identity changed');checks.push('original id/version/body preserved');
await nav('路由');await tap('创建路由');await expect("[...document.querySelectorAll('[aria-label=路由编辑] select option')].some(o=>o.textContent==='评论卡片')",'routing can select templates');await nav('模板');await tap('编辑模板');await fillInput('[aria-label=模板编辑] input','编辑后模板');await qa({next_write:'error'});await tap('保存模板');await expect("document.querySelector('[aria-label=模板编辑]')?.textContent.includes('未确认')",'503 retains template');await tap('重试原模板保存');await expect("!document.querySelector('[aria-label=模板编辑]')",'503 recovers with same version');
await tap('删除模板');await expect("[...document.querySelectorAll('.content button')].find(b=>b.textContent==='确认删除模板').disabled",'deletion requires name');await js("(()=>{let e=[...document.querySelectorAll('label')].find(l=>l.textContent.includes('输入模板名')).querySelector('input');e.value='编辑后模板';e.dispatchEvent(new Event('input',{bubbles:true}));})()");await tap('确认删除模板');await expect("document.querySelector('.content').textContent.includes('暂无模板')",'template deleted');await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await tap('创建模板');await expect("document.documentElement.scrollWidth<=390",'390px template form fits');cliLog(await captureScreenshot());await fillInput('[aria-label=模板编辑] input','401 测试');await qa({next_write:'unauthorized'});await tap('保存模板');await expect("!!document.querySelector('.login-card')",'401 clears template draft');cliLog({passed:checks.length,checks});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS template management',{keep:false}));
EOF
