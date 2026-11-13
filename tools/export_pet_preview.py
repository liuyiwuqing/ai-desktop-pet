#!/usr/bin/env python3
"""Export the real LVGL host frames to a standalone animation viewer (stdlib only)."""
import argparse
import base64
import json
import struct
import zlib
from pathlib import Path

STATES = ('发呆', '开心', '兴奋', '低落', '困倦', '饥饿', '生病', '睡眠', '有点冷', '有点热', '有点干', '有点湿')


def png_from_ppm(path):
    magic, dimensions, maximum, rgb = path.read_bytes().split(b'\n', 3)
    if (magic, dimensions, maximum, len(rgb)) != (b'P6', b'240 320', b'255', 240 * 320 * 3):
        raise ValueError(f'Unexpected frame format: {path}')

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    # Crop exactly the firmware's avatar area; do not upscale or redraw the character.
    scanlines = b''.join(b'\0' + rgb[(y * 240 + 14) * 3:(y * 240 + 222) * 3] for y in range(62, 140))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 208, 78, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frames', type=Path, default=Path(__file__).resolve().parents[1] / 'firmware/build-ui-preview')
    args = parser.parse_args()
    frames = [[base64.b64encode(png_from_ppm(args.frames / f'pet-{state}-{frame:02}.ppm')).decode()
               for frame in range(42)] for state in range(len(STATES))]
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>NEXUS PET · 状态动画预览</title><style>
body{margin:0;background:#101923;color:#dde6ed;font:16px system-ui;display:grid;place-items:center;min-height:100vh}
main{width:min(900px,90vw)}h1{font-size:24px;font-weight:500}button{font:inherit;padding:8px 16px;margin:4px;border:1px solid #526574;border-radius:6px;background:none;color:inherit;cursor:pointer}
button[aria-pressed=true]{background:#91d4b7;color:#172d32;border-color:#91d4b7}img{display:block;width:100%;height:auto;image-rendering:pixelated;margin:48px 0}p{color:#a9bdcc;line-height:1.6}
</style><main><h1>NEXUS PET · 状态动画</h1><nav aria-label="成长状态">BUTTONS</nav>
<img id="pet" width="832" height="312" alt="发呆状态的宠物动画"><button id="pause">暂停</button>
<p>固件同一套 LVGL 图元输出，预览每秒 10 帧；设备调度上限为每秒 25 帧。消息与菜单遮挡时动画停帧。此预览不代表实屏刷新性能。</p></main>
<script>const frames=FRAMES,names=NAMES;let state=0,frame=0,playing=true;const pet=document.querySelector('#pet');
function draw(){pet.src='data:image/png;base64,'+frames[state][frame]}
document.querySelectorAll('[data-state]').forEach(b=>b.onclick=()=>{state=Number(b.dataset.state);frame=0;document.querySelectorAll('[data-state]').forEach(n=>n.setAttribute('aria-pressed',String(n===b)));pet.alt=names[state]+'状态的宠物动画';draw()});
document.querySelector('#pause').onclick=e=>{playing=!playing;e.target.textContent=playing?'暂停':'播放'};
setInterval(()=>{if(playing){frame=(frame+1)%42;draw()}},100);draw();</script></html>'''
    buttons = ''.join(f'<button data-state="{i}" aria-pressed="{str(i == 0).lower()}">{name}</button>' for i, name in enumerate(STATES))
    page = page.replace('BUTTONS', buttons).replace('FRAMES', json.dumps(frames)).replace('NAMES', json.dumps(STATES))
    output = args.frames / 'pet-animation.html'
    output.write_text(page)
    print(output)


if __name__ == '__main__':
    main()
