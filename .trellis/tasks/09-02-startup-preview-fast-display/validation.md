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
