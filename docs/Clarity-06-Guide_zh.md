# Iris Color 0.6.0 · Clarity 中文使用指南

**Windows x64 VST3 · 音阶染色、频带平衡与动态音色**

0.6 的核心变化是 **Clarity**：保留 Prism 的调音和共振方式，同时让你调整处理带内低中频与上方谐波的相对比例。新实例默认 35%，旧状态恢复为 0%。界面采用暖纸白、深色文字、DM Sans 与鼠尾草绿，常用参数仍集中在 Main、Bands + EQ、Motion 三页。

[返回中文说明](../README_zh.md) · [English README](../README.md)

![Iris Color 0.6 Main 原生界面](screenshots06/Main100.png)

*以下截图均来自实际原生界面，画面参数用于说明操作，与第 2 节的比较起点不同。*

## 1. 安装、升级和打开旧工程

保存工程并关闭宿主，运行 **`Iris-Color-v0.6.0-Windows-x64-Setup.exe`**，允许管理员提示后完成安装。Setup 会把完整插件和资源放到标准 VST3 位置，通常为 `C:\Program Files\Common Files\VST3\Iris Color.vst3`，不需要手动拖文件夹。

在 FL Studio 中，打开 **Options → File settings → Manage plugins**，勾选 **Verify plugins** 和 **Rescan previously verified plugins**，点击 **Find installed plugins**，随后在 Mixer 效果槽加载。操作路径见 [Image-Line 官方安装说明](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)。

升级保留同一插件标识。如果过去在自定义目录手动安装过其他副本，清理扫描路径中的重复项后再扫描。卸载请使用 **Windows 设置 → 应用 → 已安装的应用 → Iris Color**。

旧工程的默认处理如下：

| 状态来源 | Core | Clarity |
|---|---|---|
| v0.5，保存为 Prism | 恢复保存的 Prism | 缺失时为 **0%** |
| v0.5，保存为 Legacy | 恢复保存的 Legacy | 缺失时为 **0%**，不生效 |
| 更早版本，没有 Core 字段 | **Legacy** | **0%**，不生效 |
| v0.6 | 恢复保存值 | 恢复保存值 |
| 新实例 | **Prism** | **35%** |

要在旧工程里加入新平衡，选择 Prism、把 Clarity 从 0 调到合适值，再保存。选用新版工厂预设会载入包括 Clarity 35% 在内的音色参数，同时保留当前 Core 和调性。旧参数仍能正确恢复时，预设名也可能显示 Custom，因为新版工厂值已包含 Clarity。

## 2. 用一分钟听清 Clarity 的变化

先选一段带有持续音与起音的素材，按素材设置 Root 和 Scale。若沿用此前的处理中段，可从下面这组参数开始：

| 项目 | 比较起点 |
|---|---|
| Core | **Prism** |
| Root / Scale | 按素材设置 |
| Color / Snap / Decay | **150% / 100% / 200 ms** |
| Low / High | **230 Hz / 4000 Hz** |
| Clarity | 依次比较 **0 → 35 → 70%** |
| Morph / Gate / 五个创意效果 | 先关闭，减少同时变化的参数 |
| 三段 Color / Broad EQ / Paint EQ | 三段 Color 都为 **100%**；EQ 为 **0 dB** |
| Transient / Width | **0% / 100%**，先固定比较条件 |
| Output | 从 **−6 dB** 起步，按最终电平调整 |

比较时只改变 Clarity。先听 0 到 35 时低中频的主导感和上方谐波是否发生了合适的变化，再用 70 确认变化方向，最后回到适合素材的位置。确定整体平衡后，再调整 Transient、Width 和创意效果。

**请匹配最终播放响度。**使用 DAW 电平表与耳朵检查输出，必要时用 Output 或后级静态增益微调。强限幅也会影响比较，因此要保证几段使用相同的 Soft Limit 状态并留出输出余量。

## 3. Clarity 怎样操作

Clarity 位于 **Main 页右侧**，范围为 **0–100%**。横向拖动滑条，按住 Shift 可细调；右键或聚焦后按 Enter 可输入准确数值。输入后 Enter 应用，Escape 取消。双击复位到新版默认 **35%**；要比较旧版，请直接输入 **0**。

| 数值 | 含义 |
|---|---|
| **0%** | 关闭新增平衡处理，保留旧 Prism 路径 |
| **20–35%** | 可作为轻度调整的试听范围 |
| **35%** | 新实例与新版工厂预设的默认值 |
| **70%** | 用来比较较明显的高低中频重心变化 |
| **100%** | 最大调整量，按素材判断是否需要 |

这个参数调整的是 **Low / High 范围内的湿声平衡**。原低频和高频保护支路保留；Clarity 不改变 Root、Scale、Snap 或 Decay 的设定。在 **Legacy** 中，控件显示 **Prism only** 且不生效。

Clarity 会配平当前湿声分析帧的能量，以限制增加高处能量时的整体增益偏差。配平位置在 **Wet EQ 之后、Width 和 Peak Tamer 之前**。因此，Width、后续混合、Output 与 Soft Limit 仍可能改变最终 RMS 或响度；不能把这个控制理解为任意设置下输出响度都恒定。

## 4. Main 页：先决定音色主体

| 参数 | 范围 | 作用与调节方向 |
|---|---|---|
| **Core** | Prism / Legacy | 选择处理核心。Clarity 只用于 Prism；旧状态按保存内容恢复 |
| **Color** | 0–200% | 0–100% 从对齐干声过渡到处理声；100–200% 增加音阶共振的锐度与强度 |
| **Snap** | 0–100% | 控制向允许音符靠拢及调谐中段的参与量；100% 用于明确的音阶染色 |
| **Clarity** | 0–100% | 调整处理带内的高低中频重心，默认 35% |
| **Low / High** | 60–1500 / 1000–18000 Hz | 决定主要处理范围。先把范围放在需要染色的部分，再调 Clarity |
| **Decay** | 25–1500 ms | 调节共振的持续感。Prism 中它是各音高的衰减参考，高音与低音不会统一拖尾同样的毫秒数 |
| **Morph** | 0–100%，另有开关 | 将湿声频谱幅度逐渐靠向干声幅度，保留湿声相位；较大值会更多保留原素材的幅度包络 |
| **Gate** | −96–0 dB，另有开关 | 根据干声中对应频率的强弱关闭湿声频率，阈值越高通常越容易收紧尾部 |
| **Transient** | 0–100% | 起音时增加原始干声的参与，调整冲击感与起音染色程度 |
| **Width** | 0–160% | 改变湿中段的立体声宽度；100% 为原宽度，0% 收至中央，超过 100% 加宽 |
| **Output** | −24 至 +12 dB | 设置最终输出增益，便于留出余量和做等响度比较 |
| **Soft Limit** | 开 / 关 | 对较大的采样峰施加软限幅；推高输入或输出时会改变波形 |
| **Bypass** | 开 / 关 | 切回保持宿主延迟的原始干声，稳定旁路为单位增益 |

Root 和 Scale 定义目标音级。Custom 键盘按绝对音名选音；启用 MIDI 后，接收的音符可参与控制目标音集。

**Snap 0 会回到未调谐中段，Pitch Dive 与 Scale Motion 所在的调谐支路随之退出。**试听这两个效果时先把 Snap 设为 100%。Clarity 仍按 Prism 的当前湿声工作。

Morph 100% 使用干声幅度与湿声相位，后续 EQ、Clarity 等还会改变频谱。Color 0 的湿声参与量为零，但 Output 与 Soft Limit 仍然生效。需要原声快速对照时使用 Bypass；需要只比较 Clarity 时固定其他控制，并切换 Clarity 0 / 35 / 70。

## 5. Bands + EQ：局部塑形

![Iris Color 0.6 Bands + EQ 原生界面](screenshots06/Bands100.png)

三个 **Band Color** 控制各频段在 Global Color 超过 100% 后增加多少共振锐度和强度。它们的范围都是 0–200%。可以先保留 100 / 100 / 100%，再针对过强或不足的部分调整。

两条分频线可以整条拖动，也可输入 `700 Hz`、`3k` 等频率。L/M 范围为 120–2000 Hz，M/H 范围为 1000–12000 Hz；后者至少是前者的 1.25 倍。Shift 拖动细调，Enter 应用数值，Escape 取消。

**Broad EQ** 提供三段 −12 至 +12 dB 增益。**Paint EQ** 提供 16 个固定频率节点，范围为 −24 至 +12 dB；横向绘制会连接中间节点，选中节点后可以输入 dB，双击复位单点。

**Flat EQ 会把三段 Broad EQ 和 16 点 Paint EQ 一起归零。**Clarity、Band Color 和其他音色参数保持当前数值。Clarity 处理的是已有湿声的整体频带平衡；精确修整某一个频率时，使用 EQ 节点会更直接。

## 6. Motion：让音色随时间变化

![Iris Color 0.6 Motion 原生界面](screenshots06/Motion100.png)

| 效果 | 主要作用 | 对应预设起点 |
|---|---|---|
| **Attack / Tail** | 分别调整起音和尾部的染色强度，用 Transition 决定过渡速度 | Bloom |
| **Pitch Dive** | 从正或负半音偏移逐渐回到目标音高 | Dive |
| **Scale Motion** | 按节拍或自由速率轮流强调允许的音级 | Ripple |
| **Formant Motion** | 用 A / E / I / O / U 元音形状与运动调整湿声 | Vowel |
| **Peak Tamer** | 压低局部突出的频谱峰，左右声道共用衰减 | Silk |

在 Prism 中，前三项从 Color 100% 起已作用于主染色层。Legacy 保留对 Color 超过 100% 后附加共振的原有处理方式。Formant、EQ 和 Peak Tamer 在两个核心中均可用于湿声塑形。

使用 Clarity 后如果某些谐波显得突出，可以降低 Clarity、微调该频段 EQ，或开启 Peak Tamer。Tamer 的 Maximum 控制最大允许衰减，它与最终 Soft Limit 是两个独立的控制。

工厂预设共有 **Init、Glass、Clean、Gun、Water、Bloom、Dive、Ripple、Vowel、Silk**。新版预设包含 Clarity 35%。调性、Core、MIDI、Output、Bypass 和 Soft Limit 等工程上下文在选择预设时保留。

## 7. 测量能说明什么

Clarity 的独立合成测试使用粉红宽带输入，固定 **Prism、Color 150%、Snap 100%、230–4000 Hz、Width 100%**，并关闭 Morph、Gate、Transient、创意效果和限幅。相对 Clarity 0：

| 测量 | Clarity 35% | Clarity 70% |
|---|---:|---:|
| 1.5–3.5 kHz 相对 300–800 Hz 的能量比例变化 | **+2.07 dB** | **+4.14 dB** |
| 实际全频 RMS 变化 | **约 +0.001 dB** | **约 +0.002 dB** |

这说明在该比较条件下，频带比例变化主要来自音色重心调整。它没有给任意素材、宽度或限幅组合规定最终响度，也没有给听感偏好打分。判断哪一个值适合工程，仍应匹配播放响度，并在完整编曲中试听。

开发验证还覆盖了 Clarity 0 与旧版逐采样一致、外侧低频和高频保护、Color 0、Bypass、Legacy、固定延迟与处理分块一致性。Linux DSP 验证共完成 **9 组新行为检查、9 组旧 C++ 回归（110 条通过项）、Clarity 0 与 35 下各 8 组 Prism 音频检查，以及 3 个失谐 Motion 用例**。对应检查源文件见 [Clarity 测试](../tests/dsp_color06_audio.py)、[Prism 音频测试](../tests/dsp_color05_audio.py)和 [Motion 测试](../tests/dsp_color05_motion_offgrid.py)。

Linux 的 adapter、transport、状态和预设检查已经通过，七个 Windows 验证程序也已在 Wine 下全部通过，包括真实原生窗口和 Clarity 操作。两组 44.1 kHz 的 Windows / Linux 离线输出逐采样一致：Clarity 35 / Width 100，以及 Clarity 70 / Width 130 加组合创意效果。每组 282240 帧，最大采样误差与相对 RMS 误差均为 0。这是所测配置的跨平台一致性结果；真实 Windows / FL Studio 会话验收尚未记录。

安装器在隔离的 Wine 9.0 x64 环境中通过了 v0.6 全新安装、v0.5 升级、同版重装、安装／卸载占用保护和正常卸载检查。10 个安装文件均与正式 payload 哈希一致；卸载后自有文件及注册项清除，5 个额外／用户文件哨兵保留。9 项打包回归测试也已通过。[安装运行记录](validation06/installer-runtime-validation.json)记录了具体结果；真实 Windows / FL Studio 的实机会话尚未验收。

## 8. 常见问题

**为什么 Clarity 是灰色的？** 当前 Core 为 Legacy。选择 Prism 后可以使用；旧工程是否切换核心，应根据你希望保留或改变的音色决定。

**为什么调高 Clarity 的变化很小？** 先检查 Color 是否为 0、Bypass 是否开启，以及素材的主要能量是否处于 Low / High 之间。接近单一持续音的素材也可能只有很小变化，因为该控制同时配平湿声能量。

**为什么旧工程打开后 Clarity 是 0？** 这是兼容性行为。旧状态缺少 Clarity 时明确恢复为 0，避免自动套用新实例的 35%。你可以按需调高并保存。

**为什么升级后看到 Custom？** 预设名称由当前音色参数匹配。旧设置的 Clarity 0 与新版工厂预设的 35 不同，名称变化并不说明参数丢失。

**延迟有没有改变？** 默认仍为 3072 采样，44.1 kHz 时约 69.66 ms，48 kHz 时 64 ms。两种 Core、Color 0 和稳定 Bypass 都维持这一延迟；Clarity 不增加额外延迟。

**图上的处理谱就是最终输出吗？** 处理谱用于观察湿声变化。最终 Color / Transient 混合、Output 和 Soft Limit 还会影响输出，最终电平请看 DAW 的输出表。

**旧版截图在哪里？** [Prism 0.5 指南](Prism-05-Guide_zh.md)和 `screenshots05/` 保留历史界面。旧文档中的试听与 Validation 文件属于原发行内容，当前源码包不包含那些私有参考素材。

源码构建与安装程序打包命令见 [当前 README](../README_zh.md)。Iris Color 是独立实现的效果器，不宣称复制其他产品的专有算法或 HQ 模式。
