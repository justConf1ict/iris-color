# Iris Color · v0.7.0 CrashFix

Windows x64 VST3 音阶染色效果器，含 Prism/Legacy 核心及 Glass、Metal、Wood、String 四种 Material Body 模型。

**当前修复为预发布版，尚未完成真实 FL Studio 内验收。** 已根据真实 FL64 dump 定位 GUI 崩溃，并在 Windows 隔离 VST3 宿主中复现、修复和回归验证。不要把隔离宿主测试当作真实 DAW 稳定性证明。

[English](README.md) · [预发布下载](https://github.com/justConf1ict/iris-color/releases/tag/v0.7.0-crashfix.1) · [调查报告](reports/iris-color-fl-crash.md) · [构建与测试](BUILDING.md)

## 下载和安装

- [Windows VST3 ZIP](https://github.com/justConf1ict/iris-color/releases/download/v0.7.0-crashfix.1/Iris-Color-v0.7.0-CrashFix-Windows-x64.zip)：插件、DM Sans 字体与许可。
- [完整源码 ZIP](https://github.com/justConf1ict/iris-color/releases/download/v0.7.0-crashfix.1/Iris-Color-v0.7.0-CrashFix-Source.zip)：含锁定 SDK，可离线构建。
- [SHA-256 校验文件](https://github.com/justConf1ict/iris-color/releases/download/v0.7.0-crashfix.1/SHA256SUMS.txt)。GitHub 自动生成的 Source code 归档不含插件二进制或 SDK checkout。
- [历史 v0.5.0 发布](https://github.com/justConf1ict/iris-color/releases/tag/v0.5.0) 保留不变；其历史发布状态不代表已完成真实 FL 验收。

先保存工程并完全关闭所有加载 Iris 的宿主。备份原来的 `Iris Color.vst3`，再把 ZIP 内整个同名目录复制到 `C:\Program Files\Common Files\VST3\`，保留 `Contents` 和资源目录。系统目录写入可能要求管理员权限。不要在 FL Studio 运行时覆盖 DLL，不要删除其他插件。

如果旧路径是单个文件，先把该项移到备份位置，再放入新的完整目录。此发布没有 Setup EXE；源码中的 NSIS 生成方法见构建文档。旧 Fix1 安装器只迁移旧式路径，不包含本次 GUI 修复。

在 FL 的 Manage plugins 中验证重扫 Iris，然后先用空工程检查 GUI 打开/关闭/重开、停止/播放、四种材质、自动化和工程恢复。

## 修复内容

原 DLL 的 GDI+ `GenericTypographic()` 缓存跨编辑器关闭后失效。隔离宿主不播放音频时，第二次开窗即在真实 dump 同一偏移 `+0x3A8F6` 崩溃。修复使用绘制作用域内独立的 StringFormat；未删除 Material Body、禁用 GUI 或修改 DSP/参数编号/状态格式。另修正 Windows SDK `small` 宏重名，并补齐原源码包漏带的两份字体。

验证包括 17 项 CTest、180 个采样率/block/channel/材质组合、16 组传输检查、ASan、双实例 GUI 和完整字体下两项 10 分钟测试。原始生成测试输出及限制见[验证记录](reports/validation/crashfix/verification.json)。

已知限制：新文字格式存在少量边距/亚像素差异，严格像素一致性检查未通过；ASan 下处理 60 秒音频耗时约 268 秒，不能作为实时性能通过；真实 FL Studio 验收仍未执行。版本资源保持 `0.7.0`，用发布标签和 DLL 哈希区分此次修复。

## 使用

选择 Root/Scale，保持 Color 高于 0%，再逐步提高 Body Amount。Body 默认 0%，包括恢复旧项目时，避免未经选择加入共鸣层。四种材质共享 24 个固定模态槽位，属于物理启发的模态近似，不是测量材料或完整物理模拟。音频与 GUI 实现位于 [src](src) 和 [plugin](plugin)。

项目代码采用 [MIT](LICENSE)；字体为 SIL OFL，SDK 和其他组件保留各自[许可](third_party/NOTICES.md)。
