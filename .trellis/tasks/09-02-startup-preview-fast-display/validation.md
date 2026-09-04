# Startup Preview 验证记录

## 2026-09-04 纠偏补丁验证（最新）

本次补丁针对初版简化提交中的偏差做最小修正：恢复默认斜向 shimmer、补回人工观察延迟入口、把正常 handoff 接回 `OrderedHandoffReducer`，并让公开 `Stop()` 优先通过 render thread 释放 Startup Preview D2D 资源；管线已停时只做进程收尾兜底。未重新启动可见应用窗口做人工 smoke。

## 最新构建与测试证据

使用 x64 `MSBuild.exe` 构建完整 `InkeysRepo.sln`，并将 `TEMP/TMP` 指向仓库内临时目录；为规避当前沙箱环境中同时存在 `PATH/Path` 导致 CL 环境表重复的问题，最终构建使用干净 `cmd.exe` 环境补回单一 `Path`。下列命令等价目标退出码 0：

```text
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m:1 /nr:false /nologo
Build/x64/Debug/InkeysHeadlessTests.exe --no-window
```

`InkeysHeadlessTests.exe --no-window` 输出：

```text
PASS animation correctness
```

最终增量构建仍输出仓库既有的第三方 `hashlib++` C4267 警告；本补丁过程中重编 `IdtMain.cpp` 时也出现过既有 `IdtMain.cpp(1465,26)` C4244 窄化转换警告。本补丁未新增编译错误。

## 最新静态检查

- `git diff --check`：PASS，无空白错误。
- 修改文件 `git ls-files --eol`：工作区均为 `w/crlf`。
- Startup Preview 专属源码/工程登记中不再出现 BIN、Format/VisualConfig/CacheWrite、image cache、signature/layout epoch、Gaussian blur、live proxy、CPU staging 或 `--capture-startup-preview`。
- Shimmer headless 断言已改为默认方向同时包含 X/Y 分量，phase 0/1 的非零 soft-tail 支撑在窗口外，中点穿过 mask，wrap 两侧等于 base-only。
- 人工观察延迟现在由 `--startup-preview-manual-delay` 或 `INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1` 显式打开；默认启动和 `--startup-preview-smoke` 不会被该 hook 拖慢。

## Trellis context 恢复

- 当前任务已通过 `task.py start 09-02-startup-preview-fast-display` 重新挂回本 Codex 会话，后续 hook 可从该任务注入上下文。
- `task.json` 已记录分支 `draw`、实现提交 `9e27bd26707dfeb3204ce78bf53e3b884cf4b811` 和最新验证摘要。
- Startup Preview 任务规范、context 与 jsonl 已补上 `Client::StartupPreview` 调度/资源约束；`build-and-compatibility.md` 仅保留 AGENTS 派生的“Debug + 当前设备原生架构”验证规则。
- 旧 research 的 `Bar proxy`、image cache、CRC/signature 等历史调查已增加当前覆盖说明，不再作为实现建议。

## 最新未执行项（NOT RUN）

- 本轮没有启动 `Build/x64/Debug/Inkeys.exe --startup-preview-smoke <report>`，因为项目指令要求完成后仅做静态提交或无窗口测试，除非用户明确允许启动窗口。
- Win7 SP1 + KB2670838、DPI/多屏/负坐标、输入吞噬、焦点/Alt-Tab、topmost 竞争、真实 device loss/ULW failure、首次重试和最终 fatal 对话框视觉观察仍需专用环境或人工交互。

## 2026-09-04 初版简化提交验证（历史）

程序化灰色占位实现已完成自动验证。此前 BIN、494x105 抓帧、CRC/signature、Gaussian blur、proxy/staging、capture 和三类 cache 交接的历史 PASS 均已废弃，不作为本轮证据。

## 构建证据

使用 x64 `MSBuild.exe` 构建完整 `InkeysRepo.sln`，并将 `TEMP/TMP` 指向仓库内临时目录以适配沙箱。下列命令均退出码 0：

```text
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug   /p:Platform=x64  /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Release /p:Platform=x64  /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug   /p:Platform=Win32 /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Release /p:Platform=Win32 /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug   /p:Platform=ARM64 /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Release /p:Platform=ARM64 /m:1 /nr:false
```

构建仍输出仓库既有的第三方 `hashlib++`、`fmt`、Win32 SDK module 和窄化转换警告；本任务未新增编译错误。

## Headless 与 smoke

同样将 `TEMP/TMP` 指向仓库内临时目录后，以下四个本机可执行目标均输出 `PASS animation correctness`，退出码 0：

```text
Build/x64/Debug/InkeysHeadlessTests.exe --no-window
Build/x64/Release/InkeysHeadlessTests.exe --no-window
Build/Win32/Debug/InkeysHeadlessTests.exe --no-window
Build/Win32/Release/InkeysHeadlessTests.exe --no-window
```

ARM64 Debug/Release headless 均成功构建，但当前 Windows x64 宿主返回“not a valid application for this OS platform”，因此运行标记为 **NOT RUN（宿主架构不支持）**，不能写 PASS。

隐藏运行 `Build/x64/Debug/Inkeys.exe --startup-preview-smoke <report>` 实际得到：

```text
PASS
totalWidthDip=470.000000
previewFirstAlpha0Committed=1
previewFadeOutCommitted=1
barAlpha0Committed=1
barAlpha255Committed=1
automaticStopPosted=1
ownerThreadExited=1
previewInactive=1
recovery=visible
requestedAlpha=255
committedAlpha=255
```

报告不再包含 `cacheState`、signature、capture、proxy 或 deblur 字段。

## 逻辑与静态覆盖

- Headless 断言覆盖默认 `380/470 DIP` 推导、折叠 80 DIP 缝隙、非法缓存回退、DPI x zoom 舍入和有限值去重写回。
- Headless 通过生产 `Config::ReadMini` 读取临时旧版 `main.json`：已知 Enable/zoom 正常读取，未知旧字段被忽略，缺失 `CachedStartupBarWidthDip` 保持 470 DIP schema 默认值。
- Headless 断言覆盖 alpha-0 首帧、单调 fade、Preview committed 0 后才启动 Bar fade、Bar committed 255 后才销毁，以及 bypass 恢复门。
- Headless 断言覆盖 shimmer 本地 epoch、cosine ease、不同尺寸下完整 soft-tail 两端离屏和 wrap 的 base-only 端点。
- Headless 断言覆盖 2999ms/3s/180ms 进度门、真实 milestone 上限、无 300ms hold、自动重试普通颜色和最终 fatal 红色。
- 源码静态检查确认每次 Preview ULW 都填写 `pptDst`、`psize`、`pptSrc`，blend 为 `AC_SRC_OVER/0/AC_SRC_ALPHA`；`FillOpacityMask` 前后切换并恢复 antialias mode。
- Startup Preview 专属源码/工程登记中不再出现 BIN、Format/VisualConfig/CacheWrite、image cache、signature/layout epoch、Gaussian blur、live proxy、CPU staging 或 `--capture-startup-preview`。Bar 其他视觉效果中既有 Gaussian 保留，未误删。
- RC/resource/project/filter 无悬挂图片资源，PptCOM activation manifest 资源 221 保持原编号；项目依赖没有新增高版本静态 import，Shcore 仍为动态加载。
- 本任务修改的源码、工程和任务文件已恢复原 CRLF；`git diff --check` 无空白错误（仅提示两项既有无关文件仍为 LF），Trellis task validate 通过。

## 人工/平台验证（NOT RUN）

以下仍需专用环境或交互，本轮没有声称 PASS：

- Windows 7 SP1 + KB2670838、WARP、FL11.0、动态 Shcore 缺失回退；
- 96/120/144/192 DPI、混合 DPI、多显示器、负坐标和拓扑变化的实际视觉；
- 深浅桌面上的颜色/质感、shimmer 循环边界像素级截图比较；
- SuperTop 父/helper/UIAccess 完整链路；
- 点击吞噬、焦点、Alt-Tab、topmost 竞争和透明圆角 hit-test；
- 注入真实 device loss、GetDC/ULW/ReleaseDC/EndDraw failure；
- 首次自动重试与最终 fatal 对话框的人工视觉观察。
