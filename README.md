# Iris Color

**Color bass 音阶染色效果器 · v0.5.0 · Windows x64 VST3**

将输入音频向所选音阶的音符调谐并添加共振染色，适合探索 Color bass 音色。它是加载在 DAW 效果器插槽中的音频插件。

## 1. 下载 Windows 插件包

### [点击下载 Iris Color v0.5.0 — Windows ZIP](https://github.com/justConf1ict/iris-color/releases/download/v0.5.0/Iris-Color-v0.5.0-Windows.zip)

[打开发布页面](https://github.com/justConf1ict/iris-color/releases/tag/v0.5.0) · 文件大小 **32,557,598 字节（约 31.05 MiB）**

请下载 **Iris-Color-v0.5.0-Windows.zip**。GitHub 自动生成的 **Source code (zip / tar.gz)** 是仓库源码归档，不是可安装的插件包，不含本次发布的插件二进制。

目标系统为 **Windows 10/11 x64**，需要支持 **VST3** 的 64 位 DAW。本次发布仅提供 Windows x64 VST3，没有 macOS、VST2 或独立应用版本。真实 Windows FL Studio 兼容性尚待验收，详见下方限制。

## 2. 安装与 FL Studio 扫描

1. 关闭 FL Studio 或其他 DAW，解压整个 ZIP。
2. 在解压后的 `Iris-Color-v0.5.0` 目录中，找到 **`Iris Color.vst3` 文件夹**，将整个文件夹复制到 `C:\Program Files\Common Files\VST3\`。写入该系统目录可能需要管理员权限。
3. 安装后应能找到 `C:\Program Files\Common Files\VST3\Iris Color.vst3\Contents\x86_64-win\Iris Color.vst3`。务必保留完整 `Contents` 及资源目录。升级时整体替换旧的同名插件文件夹。
4. 打开 FL Studio：**Options → File settings → Manage plugins**。启用 **Verify plugins**；升级时同时启用 **Rescan previously verified plugins**，再点击 **Find installed plugins**。
5. 扫描后在 **Mixer（混音器）的效果器插槽**加载 Iris Color。新扫描的效果器也可在 Browser → Plugin database → Installed → Effects → New 中查找。

扫描步骤依据 [Image-Line 官方安装说明](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)，不代表本插件已完成真实 FL Studio 验收。

## 3. 开始使用

1. 给音轨播放一段音频，在效果器插槽打开 Iris Color。新实例默认使用 **Prism 0.5** 核心。
2. 根据歌曲设置 **Root / Scale**；先用 **Snap 100%**，再逐步调整 **Color**。Color 0–100% 控制染色混合，超过 100% 进一步加强调谐共振。
3. 可从包内参考设置开始：**Root D#、Minor pentatonic、Color 150%、Snap 100%、Decay 200、Low 230 Hz、High 4000 Hz**。其他调性请相应调整 Root / Scale。先设 **Output −6 dB、Soft Limit 开启**，留出试听余量。
4. 先关闭创意效果听基础音色，再逐项尝试；用旁路比较处理前后。试听 **Pitch Dive / Scale Motion** 时保持 Snap 100%，Snap 0 会绕过它们所在的调谐支路。

完整安装包中的 **`README_zh.md`** 是中文快速入门，**`docs/Prism-05-Guide_zh.md`** 是详细指南。解压后打开根目录 **`Listen.html`** 可听对照示例。界面实拍图位于包内 `docs/screenshots05/`。

## 主要功能

- **Prism 0.5 与 Legacy 0.4 双核心**：新实例默认 Prism；不含 Core 字段的旧工程状态恢复为 Legacy。要给旧工程使用 Prism，请手动切换并保存。
- **Main、Bands + EQ、Motion 三页界面**，Color 分频点支持拖动及数值输入。
- **三段 Color、三段 Broad EQ、可连续绘制的 16 段湿 EQ**；Flat EQ 可将两层 EQ 一起归零。
- **Attack / Tail、Pitch Dive、Scale Motion、Formant Motion、Peak Tamer** 五组创意处理。Scale Motion 支持自由速率和宿主节拍同步。
- **10 个工厂预设**：Init、Glass、Clean、Gun、Water、Bloom、Dive、Ripple、Vowel、Silk。预设保留当前 Root、Scale、Core 等工程上下文。

## 已知限制

- 默认固定延迟为 **3072 采样**：44.1 kHz 约 **69.66 ms**，48 kHz 为 **64 ms**。两种核心及稳定旁路均保留该延迟；这不是零延迟效果器。
- 包内记录的 Windows 验证是在 **Wine** 下进行的；**尚未在真实 Windows FL Studio 会话中验收**。扫描、工程恢复、MIDI 路由及机器相关缩放仍需实际宿主确认。本页没有将包内历史记录当成本次重新测试的结果。
- 外部 `.vstpreset` 在 FL Studio 的直接导入兼容性未确认，可手动设置参数后使用效果槽菜单 **Save Preset as** 保存 `.fst`。
- 多音输入中的微分音偏移可能保留，不保证任意复音素材都能精确量化到目标音高；不承诺复刻其他商业插件的私有算法或音质。

## 文件校验与许可

Windows ZIP：**32,557,598 字节**。SHA256：

```text
ED2C8666033DAC3998693E64E58948B7E3E6B928D5DA197175E7AFE191FAA457
```

项目原创代码采用包内 **MIT** 许可；第三方组件遵循各自许可，详见包内 `LICENSE`、`third_party/NOTICES.md` 和插件 `Contents/Resources/`。示例中的用户提供参考录音保留原有权利，不属于项目 MIT 音频素材。
