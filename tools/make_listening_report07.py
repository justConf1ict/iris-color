#!/usr/bin/env python3
"""Build a private, portable listening report from measured comparison files."""
from __future__ import annotations
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
import html
import json
from pathlib import Path
import shutil
import subprocess
import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]
SCENES = [("dry", "原始 Dry"), ("iris06", "Iris 0.6"), ("iris07-off", "0.7 · Body 关闭"),
          ("iris07-glass", "只加 Glass"), ("iris07-metal", "只加 Metal"),
          ("iris07-wood", "只加 Wood"), ("iris07-string", "只加 String"),
          ("preset-crystal-punch", "Crystal Punch"), ("preset-chrome-growl", "Chrome Growl"),
          ("preset-wooden-pluck", "Wooden Pluck"), ("preset-string-halo", "String Halo"),
          ("masterspark-modal", "MASTER SPARK · Modal"),
          ("masterspark-resochord", "MASTER SPARK · Resochord")]
INPUT_NAMES = {"user-bass-Dsharp-minor": "你的 D♯ minor bass",
               "synthetic-fm-growl": "原创 FM growl 测试",
               "synthetic-color-gun": "原创短促 color gun 测试",
               "synthetic-reese": "原创 Reese 测试"}

def data_uri(path, mime):
    return "data:"+mime+";base64,"+base64.b64encode(Path(path).read_bytes()).decode()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("lab", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    lab, out = args.lab.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = json.loads((lab/"comparison-metrics.json").read_text())
    render = json.loads((lab/"iris-render-receipt.json").read_text())
    encoded = lab/"encoded"
    encoded.mkdir(exist_ok=True)
    def encode(row):
        src = lab/row["matched_file"]
        mp3 = encoded/(src.stem+".mp3")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                        "-i", str(src), "-codec:a", "libmp3lame", "-b:a", "192k", str(mp3)], check=True)
        return row["input"], row["scene"], data_uri(mp3, "audio/mpeg")
    clips = {}
    with ThreadPoolExecutor(max_workers=4) as pool:
        for stem, scene, uri in pool.map(encode, report["clips"]):
            clips.setdefault(stem, {})[scene] = uri
    body = data_uri(ROOT/"docs/screenshots07/Body100.png", "image/png")
    controls = "".join('<option value="'+key+'">'+label+'</option>' for key,label in INPUT_NAMES.items() if key in clips)
    buttons = "".join('<button class="scene" data-scene="'+key+'">'+label+'</button>' for key,label in SCENES)
    payload = json.dumps({"audio": clips, "report": report, "labels": dict(SCENES), "inputs": INPUT_NAMES}, ensure_ascii=False).replace("</", "<\\/")
    doc = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Iris Color 0.7 · 材质共鸣试听</title>
<style>
:root{color-scheme:light;--paper:#f1f4eb;--ink:#233b36;--line:#cbd5c5;--violet:#7f6c9d;--muted:#64756b}
*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.75 system-ui,-apple-system,"Segoe UI","Microsoft YaHei",sans-serif}
main{max-width:1120px;margin:auto;padding:42px 32px 76px}h1{font-size:42px;line-height:1.25;margin:8px 0 20px;letter-spacing:-1px}h2{font-size:25px;line-height:1.4;margin:40px 0 16px}h3{font-size:18px;margin:24px 0 8px}p{max-width:84ch;margin:10px 0 18px}.eyebrow{font-size:13px;letter-spacing:.12em;text-transform:uppercase;color:var(--violet)}.lead{font-size:20px}.quiet{color:var(--muted);font-size:14px}.screenshot{width:100%;display:block;border:1px solid var(--line);border-radius:10px;margin:26px 0 8px}section{margin-top:32px}.listen{background:#fafbf5;border:1px solid var(--line);border-radius:12px;padding:24px}.toolbar{display:flex;gap:16px;align-items:center;flex-wrap:wrap}select,button{font:inherit;color:inherit;border:1px solid var(--line);background:#fafbf5;border-radius:6px;padding:9px 12px}button{cursor:pointer}button:hover{border-color:var(--violet)}button:focus-visible,select:focus-visible,a:focus-visible{outline:3px solid #9b84ba;outline-offset:3px}.scenes{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin:22px 0}.scene{text-align:left;font-size:14px}.scene[aria-pressed=true]{background:#e9e3ef;border-color:var(--violet);color:#513e72}.current{font-size:21px;color:#614a80}audio{width:100%;margin:12px 0}.facts{display:grid;grid-template-columns:repeat(3,1fr);gap:18px;margin:20px 0}.fact{padding-left:16px;border-left:2px solid var(--line)}.fact strong{display:block;font-size:27px;font-weight:500}.fact span{font-size:13px;color:var(--muted)}.tablewrap{overflow:auto}table{border-collapse:collapse;width:100%;font-size:14px}th{text-align:left;font-weight:600;background:#e8ede2}th,td{padding:11px 12px;border-bottom:1px solid var(--line)}td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}.note{border-left:3px solid var(--violet);padding:4px 18px;margin:24px 0;background:#ece8f0}.twocol{display:grid;grid-template-columns:1fr 1fr;gap:30px}a{color:#5b477b}.tag{font-size:12px;color:var(--muted)}footer{margin-top:44px;padding-top:18px;border-top:1px solid var(--line);font-size:13px;color:var(--muted)}@media(max-width:760px){main{padding:24px 18px}h1{font-size:32px}.scenes{grid-template-columns:1fr 1fr}.facts,.twocol{grid-template-columns:1fr}.listen{padding:18px}}
</style><main>
<div class="eyebrow">Iris Color / Version 0.7.0 / 2026-10-20</div>
<h1>给 bass 加一种会共鸣的材质。</h1>
<p class="lead">新的 Body 把材质、衰减和激励分开控制。这里可以直接比较真实渲染的 Iris 0.6、Iris 0.7 和你提供的 MASTER SPARK。</p>
<p>四种材质使用原创的模态共鸣结构：Glass、Metal、Wood 和 String。Body Amount 默认为 0，旧工程保持原有参数；四条测试素材上，Body 关闭的 0.7 与 0.6 输出逐采样一致。</p>
<img class="screenshot" src="__BODY__" alt="Iris Color 0.7 原生 Windows Body 页面，五个旋钮和四种材质选项">
<p class="quiet">实际原生 HWND 截图。Modeled partials 使用 DSP 的目标音高比和相对衰减，图形不是输入声音的实时频谱。截图中的 E minor pentatonic 用于展示；下方所有试听使用 D♯ minor。</p>
<section class="listen" aria-labelledby="listen-heading">
<h2 id="listen-heading" style="margin-top:0">同响度试听</h2>
<div class="toolbar"><label>素材 <select id="input">__OPTIONS__</select></label><label><input type="checkbox" id="keep" checked> 切换时保留位置</label><label><input type="checkbox" id="loop" checked> 循环</label></div>
<div class="scenes">__BUTTONS__</div>
<div id="current" class="current" aria-live="polite"></div>
<audio id="player" controls preload="metadata" loop></audio>
<p class="quiet">每段 6.4 秒原始长度 + 1.6 秒尾音。切换音色后按播放即可；浏览器可能要求首次手动播放。此页播放已渲染音频，不在浏览器内运行插件。</p>
<div class="facts"><div class="fact"><strong id="target"></strong><span>统一目标响度 · LUFS</span></div><div class="fact"><strong id="gain"></strong><span>此段固定增益 · dB</span></div><div class="fact"><strong id="peak"></strong><span>匹配后 4× 峰值估计 · dBFS</span></div></div>
<p id="scene-note" class="quiet"></p>
</section>
<h2>如何比较才有意义</h2>
<div class="twocol"><div><h3>先听单一改变</h3><p>从 Iris 0.6 切到「0.7 · Body 关闭」，再切「只加 Glass / Metal / Wood / String」。这一组固定原 Glass 预设，Body 使用 Amount 40%、Decay 360 ms、Damping 45%、Inharmonicity 25%、Strike 40%。四种材质只改 Material，其他参数一致。</p><p>然后试听四个新预设。它们还改变了 Clarity、Transient、Color 等参数，代表完整的音色起点。</p></div><div><h3>再听参考插件的思路</h3><p>MASTER SPARK 用真实 VST3 离线处理。Modal 是 D♯3、Glass shape / Glass material、Decay 0.65 s；Resochord 使用 D♯ minor 弦式和弦设置。两者为 Parallel、100% wet、0 dB Output、限幅关闭，并分别确认了 Blend 两端的处理路线。</p><p>参考音色是独立设计的场景，参数名称或数字不代表两款插件等价，也不构成产品音质排名。</p></div></div>
<div class="note"><strong>三个容易误判的地方</strong><p>更响不等于更好；尾音更长不等于材质更丰富；选择了根音也不代表非谐和泛音都落在音阶内。Body 调谐的是最低共鸣模式，Inharmonicity 会有意偏离整数泛音。</p></div>
<h2>当前素材的测量</h2>
<p class="quiet">「原始 LUFS」是处理结果在增益匹配前的响度；能量占比不受固定增益影响。这里只描述本次设置，不能直接推断主观好坏。</p>
<div class="tablewrap"><table><thead><tr><th>场景</th><th class="num">原始 LUFS</th><th class="num">20–120 Hz</th><th class="num">120–500 Hz</th><th class="num">500 Hz–2.5 kHz</th><th class="num">2.5–8 kHz</th><th class="num">尾音 RMS*</th></tr></thead><tbody id="measures"></tbody></table></div>
<p class="quiet">* 尾音 RMS 为匹配后、源文件结束后的 1.6 秒平均值（dBFS），不含素材内部间隙。频带占比使用 20 Hz–16 kHz 总能量为分母；表中省略的 8–16 kHz 部分仍包含在分母里。</p>
<h2>Body 的五个控制</h2>
<div class="tablewrap"><table><thead><tr><th>参数</th><th>做什么</th><th>调节建议</th></tr></thead><tbody>
<tr><td>Amount</td><td>给原湿声添加共鸣层；0% 关闭新层。</td><td>从 25–40% 起步，再按素材增加。</td></tr>
<tr><td>Decay</td><td>最低模式的振幅降到千分之一所需的目标时间，即 T60；40–1800 ms。</td><td>短促 bass 先试 180–360 ms；空隙更大的片段可拉长。</td></tr>
<tr><td>Damping</td><td>增加高阶模式的损耗，使其比低阶模式更快消退。</td><td>尾音过密时优先提高它。</td></tr>
<tr><td>Inharmonicity</td><td>改变模式间距。0 为谐波端点，增加后出现各材质的非整数间距。</td><td>要更明确的调性感，先试 10–30%。</td></tr>
<tr><td>Strike</td><td>强调输入的起音激励，同时减少持续段的激励。</td><td>短击提高；长 growl 可降低。</td></tr>
</tbody></table></div>
<p>Body 在主频谱链之后添加，随后仍经过全局 Color / Transient 混合、Output、Soft limit 与 Bypass。因此 Color 0 不包含 Body。它采用固定高通激励和输出保护，Main 的 Low / High、Gate、Clarity 与 Bands EQ 并不直接重塑 Body 层。Main 的频谱显示位于 Body 之前，不显示新层对最终输出的贡献。</p>
<h2>实现范围与验证边界</h2>
<p>本版是 24 个阻尼模式组成的物理启发共鸣效果。Glass / Metal / Wood 使用原创的目标模式比和材料损耗曲线；String 使用有刚度的弦模态近似。没有声称实现 MASTER SPARK 的私有算法、真实琴体测量、波动方程求解、完整管道传播、非线性接触或多物体耦合。</p>
<p>Body 不增加原有的 3072 采样固定延迟：44.1 kHz 约 69.66 ms，48 kHz 为 64 ms。保留原 VST3 身份和 0–76 参数 ID，新增项追加在后。旧状态缺失 Body 时明确恢复为 0%。</p>
<p>Windows x64 插件在独立宿主与 Wine 环境中验证真实加载、处理、状态和原生控件。这不等同于用户电脑上的 Windows / FL Studio 会话验收。安装程序仍需你在目标宿主中重新扫描插件。</p>
<h2>测量方法与来源</h2>
<p>输入包括你已有的 D♯ minor bass 和三条可由源码重建的原创诊断 bass，均为 150 BPM。每段使用同一输入与采样率，Iris 移除其报告的固定延迟，MASTER SPARK 的报告延迟为 0；保留共鸣器自身的相位和群延迟。</p>
<p>响度用 FFmpeg 对前 6.4 秒做输入测量，匹配仅乘以一个固定增益，不增加均衡、压缩或限幅。全段用 4 倍多相重采样估计峰值，目标响度必要时统一降低以留出至少约 1 dB 峰值余量。频谱为 8192 采样窗 Welch 分析，左右声道能量平均。此估计并非经过认证的真峰值计。</p>
<p>网页试听嵌入 192 kbps MP3；另附的 A/B WAV 为 24-bit PCM，测量基于未压缩 WAV。所有完整设置、文件哈希与实际输出结果保留在验证文件和源码的 validation07 目录。参考插件二进制不包含在交付的源码中。</p>
<p>前端按 <a href="https://open-design.ai/official/">OpenDesign 官方</a>的便携设计规范流程完成，保留 DESIGN.md 与设计决策记录；并借鉴 <a href="https://imagi.ro/product/autochroma">Autoc​hroma</a>的轻松、简洁取向。模态与弦式共鸣的公开概念可对照 <a href="https://pichenettes.github.io/mutable-instruments-documentation/modules/rings/manual/">Mutable Instruments Rings 官方手册</a>。这次使用的是 OpenDesign 的官方 Skills / Systems 资料，并未宣称运行其桌面应用。</p>
<footer>此文件包含本次私人试听音频。请保留原素材的使用与分发权限。Iris Color 原创代码采用 MIT 许可，DM Sans 使用 SIL OFL；第三方参考插件归其作者所有。</footer>
</main><script>const DATA=__DATA__;
const input=document.querySelector('#input'),player=document.querySelector('#player');let active='iris07-glass',generation=0;
function row(){return DATA.report.clips.find(r=>r.input===input.value&&r.scene===active)}
function draw(){const r=row();document.querySelector('#current').textContent=DATA.labels[active];document.querySelector('#target').textContent=r.audition_target_lufs.toFixed(1);document.querySelector('#gain').textContent=(r.audition_gain_db>=0?'+':'')+r.audition_gain_db.toFixed(2);document.querySelector('#peak').textContent=r.audition_true_peak_4x_dbfs.toFixed(2);for(const b of document.querySelectorAll('.scene'))b.setAttribute('aria-pressed',String(b.dataset.scene===active));document.querySelector('#scene-note').textContent=active.startsWith('preset-')?'完整预设：包含 Body 和其他音色参数的组合变化。':active.startsWith('masterspark-')?'参考场景：由你提供的 MASTER SPARK 真实处理，并非等参数对照。':active.startsWith('iris07-')&&active!=='iris07-off'?'控制变量：保持原 Glass 预设，只改变 Body 参数。':'基准场景：先用这两版确认关闭 Body 的原始路径。';const rows=DATA.report.clips.filter(r=>r.input===input.value);const order=Object.keys(DATA.labels);rows.sort((a,b)=>order.indexOf(a.scene)-order.indexOf(b.scene));document.querySelector('#measures').innerHTML=rows.map(r=>{const b=r.spectral_energy_percent;let tail=r.tail_1_6s_rms_dbfs+r.audition_gain_db;return '<tr><td>'+DATA.labels[r.scene]+'</td><td class="num">'+r.integrated_lufs.toFixed(2)+'</td>'+['sub_20_120','low_mid_120_500','mid_500_2500','high_2500_8000'].map(k=>'<td class="num">'+b[k].toFixed(1)+'%</td>').join('')+'<td class="num">'+(tail<-150?'静音':tail.toFixed(1))+'</td></tr>'}).join('')}
function change(){const pos=document.querySelector('#keep').checked?player.currentTime:0,playing=!player.paused;const own=++generation;player.pause();player.src=DATA.audio[input.value][active];player.onloadedmetadata=()=>{if(own!==generation)return;player.currentTime=Math.min(pos||0,Math.max(0,player.duration-.01));if(playing)player.play().catch(()=>{});};draw()}
document.querySelectorAll('.scene').forEach(b=>b.addEventListener('click',()=>{active=b.dataset.scene;change()}));input.addEventListener('change',change);document.querySelector('#loop').addEventListener('change',e=>player.loop=e.target.checked);change();
</script></html>'''
    doc = doc.replace('__BODY__', body).replace('__OPTIONS__', controls).replace('__BUTTONS__', buttons).replace('__DATA__', payload)
    dst = out/'Iris-Color-v0.7.0-Listen.html'
    dst.write_text(doc)
    # Full-length, lossless comparison of five representative scenes.
    order = ['dry', 'iris06', 'iris07-glass', 'masterspark-modal', 'masterspark-resochord']
    segments, timeline, position = [], [], 0.0
    rate = None
    for scene in order:
        path = lab/'matched'/(scene+'--user-bass-Dsharp-minor.wav')
        sr, raw = wavfile.read(path)
        if rate is not None and sr != rate: raise ValueError('A/B sample rates differ')
        rate = sr
        audio = raw.astype(np.float64)/2**(raw.dtype.itemsize*8-1)
        duration = len(audio)/sr
        timeline.append({'scene': scene, 'name': dict(SCENES)[scene], 'start_seconds': position, 'end_seconds': position+duration})
        segments.append(audio);segments.append(np.zeros((round(.3*sr),2)))
        position += duration+.3
    segments.pop()
    temporary = lab/'ab-float.wav'
    wavfile.write(temporary, rate, np.concatenate(segments).astype(np.float32))
    wav = out/'Iris-Color-v0.7.0-Bass-AB.wav'
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-y','-i',str(temporary),'-c:a','pcm_s24le',str(wav)],check=True)
    (lab/'ab-timeline.json').write_text(json.dumps(timeline,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'html':str(dst),'bytes':dst.stat().st_size,'ab_wav':str(wav),'timeline':timeline},ensure_ascii=False,indent=2))

if __name__ == '__main__': main()
