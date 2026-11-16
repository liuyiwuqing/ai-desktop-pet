#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
nexus_dir="${NEXUS_CORE_DIR:-${repo_root}/../nexus-core}"
output_dir="${repo_root}/firmware/build-ui-content-export"
mkdir -p "$output_dir"
npm --prefix "$nexus_dir/frontend" run check
npm --prefix "$nexus_dir/frontend" run build
python3 "$repo_root/tools/ci/content_export_ui_fixture.py" --frontend "$nexus_dir/target/frontend" >"$output_dir/fixture.log" 2>&1 &
fixture_pid=$!
trap 'kill "$fixture_pid" 2>/dev/null || true; wait "$fixture_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
 if ! kill -0 "$fixture_pid" 2>/dev/null; then cat "$output_dir/fixture.log"; exit 1; fi
 if rg -q '^UI boundary fixture ready$' "$output_dir/fixture.log"; then break; fi
 sleep .1
done
ego-browser nodejs <<'EOF'
const fs=await import('node:fs'),os=await import('node:os'),path=await import('node:path');
const name='NEXUS content export',base='http://127.0.0.1:18820';const prior=(await listTaskSpaces()).find(s=>s.name===name);if(prior&&prior.ownership!=='agent')throw new Error('Space requires confirmation');await useOrCreateTaskSpace(name);await openOrReuseTab(base,{wait:true,timeout:15});await cdp('Network.setCacheDisabled',{cacheDisabled:true});let checks=[];
const download=fs.mkdtempSync(path.join(os.tmpdir(),'nexus-content-download-'));await cdp('Page.setDownloadBehavior',{behavior:'allow',downloadPath:download});
async function qa(body={}){return serverFetch(base+'/__qa/query',{method:'POST',headers:{'Content-Type':'application/json','X-QA-Key':'isolated-ui-fixture-control'},body:JSON.stringify(body)});}
async function expect(e,label){for(let i=0;i<100;i++){if(await js(e)){checks.push(label);return;}await wait(.1);}cliLog(await snapshotText());throw new Error(label);}
async function tap(label){if(label==='导出正文…')await js("document.querySelector('.export-actions').open=true");await expect(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.textContent.trim()===${JSON.stringify(label)});if(!b||b.disabled)return false;b.click();return true;})()`,'click '+label);}
await gotoAndWait(base+'/?qa='+Date.now(),{timeout:15});await qa({reset:true});await fillInput('.token-label input','ui-regression-fixture-token-at-least-32-characters');await js("document.querySelector('.login-card button.primary').click()");await expect("!!document.querySelector('.sidebar')",'login');await tap('导出正文…');await expect("document.querySelector('.list-filters').textContent.includes('最近七天未清理')",'scope is explicit');await expect("[...document.querySelectorAll('button')].find(b=>b.textContent==='确认导出正文').disabled",'explicit confirmation required');await js("document.querySelector('.list-filters input[type=checkbox]').click()");await tap('确认导出正文');
for(let i=0;i<100&&!fs.readdirSync(download).includes('messages-content.csv');i++)await wait(.1);if(!fs.readdirSync(download).includes('messages-content.csv'))throw new Error('CSV was not downloaded');const csv=fs.readFileSync(path.join(download,'messages-content.csv'),'utf8');if(!csv.includes('正文')||!csv.startsWith('\ufeff'))throw new Error('Incorrect CSV');checks.push('real browser download contains expected UTF-8 body');
await fillInput('[aria-label=来源标识]','manual');await expect("[...document.querySelectorAll('button')].find(b=>b.textContent==='导出正文…').disabled",'unapplied draft blocks body export');await tap('应用筛选');await expect("![...document.querySelectorAll('button')].find(b=>b.textContent==='导出正文…').disabled",'applied filter restores export');await qa({exportMode:'large'});await js("document.querySelector('.list-filters input[type=checkbox]').click()");await tap('确认导出正文');await expect("document.querySelector('.content > .feedback.error')?.textContent.includes('1000')",'oversize error explains bound');
await qa({exportMode:'success',exportDelay:1});await js("document.querySelector('.list-filters input[type=checkbox]').click()");await tap('确认导出正文');await tap('退出登录');await wait(1.3);if(fs.readdirSync(download).filter(v=>v.endsWith('.csv')).length!==1)throw new Error('Late export created a file');checks.push('logout cancels delayed body download');await expect("!!document.querySelector('.login-card')",'logout clears session');cliLog({passed:checks.length,checks,downloadDir:download});
EOF
ego-browser nodejs <<'EOF'
cliLog(await completeTaskSpace('NEXUS content export',{keep:false}));
EOF
