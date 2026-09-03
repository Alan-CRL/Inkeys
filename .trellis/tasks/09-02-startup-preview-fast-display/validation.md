# Startup Preview Validation

验证日期：2026-09-03（Windows 11 x64，Visual Studio 18 / MSVC v143）

## 结论

当前实现通过完整代码审查、六配置 Solution 构建、可执行 headless 测试、真实 Bar capture、资源逐字节校验与启动烟测。未在本机执行的 Win7、WARP、混合 DPI、多显示器和 ARM64 运行时场景在末节单独列出，不能视为已通过。

## 自动验证

| 项目 | 结果 |
| --- | --- |
| `InkeysRepo.sln` `Debug|x64` | PASS，x64 MSBuild，完整 Solution |
| `InkeysRepo.sln` `Release|x64` | PASS，x64 MSBuild，完整 Solution |
| `InkeysRepo.sln` `Debug|Win32` | PASS，x64 MSBuild，完整 Solution |
| `InkeysRepo.sln` `Release|Win32` | PASS，x64 MSBuild，完整 Solution；6 个 shader 均重新编译成功 |
| `InkeysRepo.sln` `Debug|ARM64` | PASS，x64 MSBuild 交叉编译，完整 Solution |
| `InkeysRepo.sln` `Release|ARM64` | PASS，x64 MSBuild 交叉编译，完整 Solution；6 个 shader 均重新编译成功 |
| x64 Debug/Release `InkeysHeadlessTests.exe --no-window` | PASS，均输出 `PASS animation correctness` |
| Win32 Debug/Release `InkeysHeadlessTests.exe --no-window` | PASS，均输出 `PASS animation correctness` |
| ARM64 Debug/Release headless | 仅成功编译；x64 主机未运行 ARM64 二进制 |
| Trellis context validation | PASS，`implement.jsonl` / `check.jsonl` 各 7 项 |
| `git diff --check` | PASS；仅无关用户文件显示 Git 换行提示 |

Headless 覆盖包括：1000-unit progress plan、并发/重复/乱序/failure freeze、160-byte little-endian format、CRC/边界/分类、Preview reducer/handoff/recovery、Bar requested/attempted/committed alpha、RenderPipeline 顺序与 drain、cache writer failure/latest-only、Setting session state。

## Capture 与资源

- 正式资源：`Inkeys/Inkeys/UI/StartupPreview/StartupBarPreview-v1.bin`
- 尺寸：`494 x 105`，stride `1976`，payload `207480` bytes，总长 `207640` bytes。
- Visual signature：`F158AF6C1B51A72A8637EF11FF0A2E986978F2902120A6E482AC7840C2766C63`。
- 文件 SHA-256：`F9592D61CB77A27467D4847148E1BC4412398373C23056894E2D8FFB157E3285`。
- 两次独立 capture、正式 BIN，以及 Win7 安全加载修正后的再次 capture，SHA-256 完全一致。
- source parser validator PASS。
- Debug/Release 的 x64、Win32、ARM64 共 6 个 `Inkeys.exe`，资源 ID 306 均与 source BIN byte-for-byte 一致。
- PptCOM activation manifest 资源 221 未改；没有新增 EXE ID 1 manifest。

## 启动烟测

`Build/x64/Debug/Inkeys.exe --startup-preview-smoke <report>` 在最终代码上 exit 0，报告：

```text
PASS
cacheState=0
previewFirstFrameCommitted=1
barAlpha0Committed=1
barAlpha255Committed=1
automaticStopPosted=1
ownerThreadExited=1
previewActive=0
requestedAlpha=255
committedAlpha=255
```

已视觉检查的有效截图为 `C:\Users\AlanCRL\AppData\Local\Temp\codex-shot-2026-09-03_02-07-05.png`：底部 embedded blur Preview 可见，无错误对话框；其像素 payload 与最终 canonical BIN 相同。最终构建又执行多次烟测并全部 PASS；后续 screenshot helper 冷启动导致抓图发生在短暂窗口退出后，因此这些空白截图不作为视觉证据。

## 2026-09-03 layered surface 回归修复

- 人工在 COM 后加入 50 秒停顿后，发现 Preview HWND 虽为 visible/topmost、DWM 未 cloak，D2D target 与 ULW 源 DC 也有非零 alpha，ULW 仍返回成功，但桌面没有可见内容。
- 单变量验证确认：`pptDst` / `psize` 为 null 时首帧不可见且后续 surface 不刷新；显式提交 owner 已应用的 geometry 后立即恢复。
- 正式修复让 owner `SetWindowPos` 与 render thread ULW 共用 presentation mutex，并让每帧 ULW 回显 owner snapshot 的明确位置与尺寸；render thread 不产生独立 geometry。
- 完整 `Debug|x64` Solution 构建 PASS。保留 50 秒停顿，在同一进程约 1.0s、2.2s、8.0s 抓帧：三帧均显示 embedded Gaussian blur；1.0s 与 8.0s 主体区域有 31,483 个像素变化、最大通道差 56，确认 shimmer 持续更新；8.0s 预期 progress rect 相比 2.2s 新增 642 个变化像素，确认 5 秒门与真实进度条呈现。
- 验证截图：`C:\Users\AlanCRL\AppData\Local\Temp\inkeys-preview-fixed-refresh-{1000ms,2200ms,8000ms}.png`。
- 正常 SuperTop 父进程 -> helper -> 最终 UIAccess 进程链路重复通过；Preview HWND 为 visible/topmost、DWM 未 cloak，1.0s 与 8.0s 主体区域有 33,856 个变化像素，progress rect 新增 655 个变化像素。截图为 `C:\Users\AlanCRL\AppData\Local\Temp\inkeys-preview-fixed-supertop-{1000ms,2200ms,8000ms}.png`。

## 静态兼容审查

- Startup Preview 未使用 GDI+、WinUI Runtime、DirectComposition 或 Win10-only AlphaMask effect。
- Shcore 通过 `GetSystemDirectoryW` 构造 System32 绝对路径，再以 `LoadLibraryW` / `GetProcAddress` 动态探测；Win7 缺失时回退 `SetProcessDPIAware`。
- `dumpbin /imports` 对 x64、Win32、ARM64 最终产物均未发现本任务新增的 `SHCORE.DLL`、`SetProcessDpiAwareness` 或其他 Win8.1/Win10-only DPI 静态入口点。
- 最终产物仍有任务前既存的 Draw3 `GetDpiForWindow` 静态导入；`git grep HEAD` 确认不由本任务引入。
- `_M_ARM64EC` 分支保持源码兼容；仓库没有 ARM64EC 构建配置，因此未声称 ARM64EC 构建通过。

## 未执行场景

以下项目需要对应硬件、VM 或人工交互，当前未执行：

- Windows 7 SP1 + KB2670838 VM 的硬件 D3D11、强制 WARP、FL11.0 与实际启动。
- 120/144/192 DPI、混合 DPI、多显示器、负坐标与运行中显示器拓扑变化。
- SuperTop 提权 helper 的完整跨进程交接与 UIAccess 环境。
- 鼠标点击吞噬、前台焦点、Alt-Tab、实际 topmost 竞争的人工交互检查。
- 注入 D2D device loss、GetDC/ULW/ReleaseDC/EndDraw 真实系统级失败；纯 reducer/transaction 失败路径已有 headless 覆盖。
- 显式 D3D Debug Layer 运行时检查；当前 RenderPipeline 没有打开 debug layer。
