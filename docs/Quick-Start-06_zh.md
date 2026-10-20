# Iris Color v0.6.0

Windows x64 VST3 · Clarity 音色调整 · 新界面 · 自动安装程序

## 安装与升级

1. 保存工程并关闭 FL Studio，以及其他正在使用 Iris Color 的宿主。
2. 双击 **Iris-Color-v0.6.0-Windows-x64-Setup.exe**，按提示安装。写入系统插件目录时，Windows 会请求管理员权限。
3. 打开 FL Studio 的 **Options → File settings → Manage plugins**，勾选 **Verify plugins** 和 **Rescan previously verified plugins**，点击 **Find installed plugins**。扫描完成后，在 Mixer 效果槽加载 Iris Color。

安装程序会自动将完整插件及字体资源放入系统的 64 位 VST3 目录，通常为 `C:\Program Files\Common Files\VST3\Iris Color.vst3`。无需手动拖放文件。以后在这个位置升级，也可以直接运行新版安装程序。

如果旧副本曾装在其他自定义目录，请留意宿主是否仍扫描到了那个副本。安装器只更新标准目录中的 Iris Color，不会自动搜索和删除其他位置的插件。

卸载入口是 **Windows 设置 → 应用 → 已安装的应用 → Iris Color**。安装器目前没有代码签名证书。

安装位置和 FL Studio 扫描步骤参考：[Steinberg](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html)、[Image-Line](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)。

## 这次声音怎样调整

Main 页新增 **Clarity**，范围 0–100%，仅在 **Core: Prism** 下作用。

| Clarity | 用途 |
| --- | --- |
| 0% | 关闭本次新增的音色调整，用于与 v0.5 对照 |
| 35% | 新实例默认值；先从这里试听更打开的谐波平衡 |
| 70% | 更明显地提高染色频段上部相对低中频的比例 |

Clarity 作用于 Low／High 选定的染色频段，逐渐改变频谱倾斜，并做带内频谱帧能量配平。配平发生在 Width 之前；改变 Width、动态效果或素材后，最终响度仍可能变化，试听时可以再微调 Output。

**容易误解的地方：旧工程不会在升级后自动变亮。** 没有 Clarity 字段的旧状态按 0% 恢复，已有参数和 Core 保持兼容。想在旧工程尝试新音色，请确认 Core 是 Prism，再把 Clarity 调到 35% 或 70%。双击 Clarity 会回到新默认值 35%；要比较旧音色，请明确调到 0%。

操作：点击滑轨或左右拖动调整，按住 Shift 细调；右键数值直接输入百分比。原有固定延迟仍为 3072 采样。

## 界面

参考 [Autochroma](https://imagi.ro/product/autochroma) 的简约方向，使用暖纸白背景、深棕文字、鼠尾草绿主色，以及少量黄和陶土色。字体改为随插件提供的 DM Sans，减少装饰、光晕和视觉噪声，保留 Main、Bands + EQ、Motion 三页。

## 20 秒对照试听

打开 **Iris-Color-v0.6.0-Listen.wav**。三段来自你之前提供的同一段素材，只有 Clarity 设置不同，并通过固定增益匹配到相同的测量响度。各段之间留有 0.4 秒空隙。

| 时间 | 设置 |
| --- | --- |
| 0.0–6.4 秒 | Clarity 0% |
| 6.8–13.2 秒 | Clarity 35% |
| 13.6–20.0 秒 | Clarity 70% |

文件为 44.1 kHz、双声道、24 位 WAV；未额外加入 EQ 或限幅。它用于比较这一段素材的音色，不能代表所有素材上的主观偏好。原始录音权利保持不变。

## 验证范围

已经完成 DSP 回归、旧状态兼容、正式 Windows／Linux 两组离线输出逐采样一致检查，以及 Wine 中的七个 Windows 验证程序。原生 HWND 编辑器的三页、Clarity 交互、字体和 100%／125%／150% 缩放已通过检查。

安装器已在 Wine 中通过 v0.6 全新安装、从 v0.5 升级、重装、文件占用保护、卸载和额外用户文件保留检查。10 个安装文件的哈希全部吻合。

真实 Windows 10／11 和 FL Studio 会话尚未实机验收。完整源码、构建方法、测试脚本、第三方许可和验证记录另见源码包。
