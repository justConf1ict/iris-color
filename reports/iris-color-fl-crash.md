# Iris Color v0.7.0 / FL Studio 崩溃调查（公开摘要）

日期：2026-10-20。原始取证材料保留本地；公开仓库不上传 crash dump、私人 DAW 日志或用户路径。

## 1. 根因及证据

两次真实 FL64 26.1.7.5653 崩溃均先记录 `C0000005` 读访问冲突，随后为 `C000041D` 用户回调未处理异常，模块为 Iris Color 0.7.0.0、偏移 **`+0x3A8F6`**。同进程的两个异常是同一故障链。

四份已有 minidump 通过本机 DbgHelp StackWalk64、原 DLL COFF 符号及反汇编离线分析，定位 GUI 消息线程：

```text
FL64 / FLEngine 消息循环 -> USER32
  -> NativeEditor::windowProc   +0x2F86E（WM_PAINT）
  -> NativeEditor::paint        +0x3073B
  -> NativeEditor::text         +0x35C76
  -> graphics<text lambda>      +0x3A81B
  -> text lambda               +0x3A8F6
```

MinGW 的 `GenericTypographic()` 把 StringFormat wrapper 缓存在模块全局变量中，wrapper 由 GDI+ 分配。最后一个 editor 关闭时 `closeWindow()` 调用 GdiplusShutdown；模块还在，但缓存已指向不可读对象。后续绘制在 `mov rcx, qword ptr [rax]` 读取 nativeFormat 时失败。

**隔离复现**：同一模块不调用音频 process，第一次打开/关闭 GUI 正常，第二次打开在完全相同偏移访问冲突，测试进程记录错误并 exit 77。[原版复现日志](validation/crashfix/gui-original-red.log)。没有人为污染缓存，也没有要求再次让 FL 崩溃。

分类为 **FL 主 GUI 消息线程执行插件原生绘制回调时，Iris 自身读取失效缓存**。这不是本次音频线程异常的证据；无音频实验仍失败。没有据加载后崩溃直接认定 DSP，也没有将其他日期/模块的历史崩溃合并归因。未发现驱动或安全软件导致该偏移故障的证据，不宣称穷尽排除外部因素。

约 10 秒不是已证实的固定计时器。GUI WM_TIMER 为 33 ms；缓存失效后相关重绘即可触发。真实宿主窗口启停/延迟绘制的精确时序仍未完整还原。

## 2. 版本对应

- 安装项为标准目录式 Windows x64 VST3，DLL 在 `Contents/x86_64-win/Iris Color.vst3`，不是旧式路径冲突。
- 原 DLL SHA-256：`8543459b4efa782b30c24c3b5db33e3dcdf9640c94dcf0a0bc4ba8493c4edae7`。
- 所有者提供的 Source-Fix1 ZIP SHA-256：`43d61c49e6ae8288d2b6d1bd1314e9767af3bb81e68faa41d785b6c0a2981671`。构建记录的 DLL 哈希与实际安装匹配，18 个生产源文件匹配记录。
- 原始源码作为修复前一提交导入，见[来源](../docs/import-provenance.md)，便于查看差异。其历史 manifest 顶层误写 0.6.0，未据此误判实际版本。
- 原源码 ZIP 漏带两份 DM Sans TTF；已从安装资源复制、核对原构建及字体 README 哈希后补齐。首次测试覆盖系统字体回退，补齐后另跑完整字体场景。

## 3. 文件、函数与差异

原 [plugin/IrisEditorWin.cpp](../plugin/IrisEditorWin.cpp) 的 `NativeEditor::text()` 第 309 行复制缓存；`closeWindow()` 原第 274～279 行释放 GDI+。行号来自与构建记录匹配的源码，原 DLL 没有映射源码行号的 PDB。

```diff
- Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
- format.SetFormatFlags((format.GetFormatFlags()|Gdiplus::StringFormatFlagsNoWrap)&
-     ~Gdiplus::StringFormatFlagsLineLimit);
+ Gdiplus::StringFormat format(Gdiplus::StringFormatFlagsNoFitBlackBox |
+     Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);
```

局部 format 在绘制返回前析构，保留原最终 flags `0x5004`、字体选择、trimming、alignment，不再依赖跨会话 wrapper。没有删除 Body、禁用 GUI 或吞掉访问冲突。另重命名 IrisBodyVisual.h 和 transport 测试中的 `small`，避免 Windows SDK RPC 宏冲突；新增 CMake 原生测试/ASan 开关。DSP、参数 ID、工厂 UID、状态格式、延迟不变。

## 4. 审查范围

| 范围 | 检查与边界 |
|---|---|
| 内存/缓冲区 | processSpan 的 I/O/channel 检查、预分配与超长 block 分块；实例和窗口创建/销毁/重开实际测试。 |
| 24 模态 Body | 固定数组、索引 clamp、有限值/状态界限、极点插值、denormal 清理；该类中无动态分配、锁或 Windows API。 |
| 音频线程 | vector 扩容在 prepare，复用 DSP 分配计数测试；GUI/host 消息在视觉请求路径。不是所有运行路径的形式化证明。 |
| 同步 | lock-free 参数 atomics、三槽所有权视觉 mailbox、消息长度/范围核验；未运行 TSan。 |
| GUI | KillTimer、DestroyWindow、WM_NCDESTROY、GDI/私有字体释放顺序；实际测双实例、缩放及宿主先销毁父窗口。 |
| VST3 | initialize、setupProcessing、active/processing、state、connections/handler、terminate/refcount，Mono/Stereo、32/64 位及自动化；宿主须遵守 setup/process 生命周期线程约定。 |

## 5. 实际验证

修复构建：MSVC 19.41.34120、Windows SDK 10.0.22621.0，VST3 SDK 3.8.1 commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`。DLL SHA-256：`4a9814488d045c5a3749c95058cefa0261c4c83c86c1bed50327059336ebdce0`。原 DLL 为 MinGW；不是相同工具链逐指令 A/B。

| 验证 | 结果 |
|---|---|
| 提交前 CTest 重跑 | 17 passed / 0 failed，14.76 秒。 |
| 窗口复现 | 原版第 2 次打开失败；修复版 25 次重开及 100/125/150% 缩放通过。 |
| 音频矩阵 | 180 组合：44.1/48/96 kHz × block 1/32/64/256/1024/2049 × Mono/Stereo × 4 材质/Body off，每组合 1 秒；含空 flush、无输入/输出。 |
| 完整字体 GUI-only | 墙钟 600 秒后继续 25 次重开，通过。 |
| 完整字体 combined | 600 秒音频，墙钟 600.006 秒，30 轮 GUI；材质、Body off、旁路、Decay/inharmonicity 自动化和静音交替通过，10/60 秒检查点在日志中。 |
| 系统字体回退 | 另完成 GUI-only 和 combined 各 600 秒，不混同私有字体结果。 |
| GUI 关闭音频 | 60 秒，墙钟 60.0071 秒，通过。 |
| 宿主/状态 | v0.6 及更早状态恢复、截断原子拒绝、83 个参数、32/64 位、in-place、精确旁路、MIDI/自动化和视觉通知通过。 |
| transport | 两种 Core 各 8 组，16 组通过。 |
| ASan | 25 次重开、Body、4 组 Body 状态迁移、60 秒音频+GUI 通过，无 ASan 报告；60 秒音频耗时 268.072 秒，不是实时性能通过。 |
| 双实例控件 | 普通及 ASan 均通过 12 轮双实例、四页/四材质、旁路/缩放/父窗口先销毁；每轮 GDI=10、USER=2，无增长，不等同完整堆泄漏证明。 |
| 打包 | 11 个真实载荷文件预检通过；测试 7 passed / 2 skipped，跳过需大小写敏感文件系统/symlink 权限的夹具，未改系统设置。 |
| 源码交付 | Source ZIP 独立解包、使用所含 SDK 离线配置编译成功，有已披露的原有编译警告。 |

命令见 [BUILDING.md](../BUILDING.md)。[验证 JSON](validation/crashfix/verification.json) 及[测试输出](validation/crashfix)保留证据；用户路径已替换，原始与公开输出哈希分别记录。

## 6. 未通过与未验证

- **真实 FL Studio 内修复版验收尚未执行，不能宣称所有 FL 崩溃已彻底解决。** 未在运行的 FL 替换安装 DLL，原安装不变。
- 无可安全隔离的 v0.6 DLL，未做真实插件 A/B；`dsp_color04_neutral` 需要独立 v0.3 基准，本次未执行。
- **严格文字像素一致性未通过**：Near/Center/Far 有 1441/822/1423 个不同像素；同 GenericTypographic 对照为 0，比较有效。新格式有数像素边距/亚像素变化，真实宿主紧凑标签待视觉验收。[左原版/右修复对照](validation/crashfix/text-layout-old-left-new-right.png)。
- 未运行 UBSan/TSan；Windows ASan 不等于完整泄漏/竞态检查。未穷举更多实例、驱动、多显示器和第三方插件组合。
- 精确 10 秒宿主时序未还原，但独立复现的模块偏移、访问类型、失效缓存和无音频触发均吻合。

## 7. 交付与生成方法

[v0.7.0-crashfix.1 预发布](https://github.com/justConf1ict/iris-color/releases/tag/v0.7.0-crashfix.1) 提供插件 ZIP、含锁定 SDK 的 Source ZIP 和 SHA-256。内嵌版本仍为 0.7.0，以 DLL 哈希识别此次修复；历史 v0.5 发布保留，不把此候选设为稳定版。

重新构建、CTest、动态宿主和现有 NSIS EXE 生成命令见 [BUILDING.md](../BUILDING.md)。本次未生成/运行 Setup EXE，未将旧 Fix1 重新命名交付。安装前保存并关闭所有宿主，备份原 Iris bundle，不修改其他插件。

## 官方参考

- [Microsoft GenericTypographic flags](https://learn.microsoft.com/en-us/windows/win32/api/gdiplusstringformat/nf-gdiplusstringformat-stringformat-generictypographic)
- [Microsoft GdiplusShutdown 生命周期](https://learn.microsoft.com/en-us/windows/win32/api/gdiplusinit/nf-gdiplusinit-gdiplusshutdown)
- [MinGW-w64 StringFormat 包装层](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/include/gdiplus/gdiplusstringformat.h)
- [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk)
