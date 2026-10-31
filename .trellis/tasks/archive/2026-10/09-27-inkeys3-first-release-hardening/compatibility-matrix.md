# Win7 SP1 + 仅 KB2670838 图形兼容矩阵

产品约束：正式 Draw3 透明呈现仅 DComp 和 ULW；两个 Win7 DWM 透明方案都禁用。微软 Platform Update 文档指出 Win7 没有 DComp、D3D11CreateDevice 最高 FL11.0、WARP 仅 FL11.0，CreateSwapChainForComposition 不可用。ULW 是 Win7 的预期透明路径，不依赖额外 KB。用户明确报告 Win7 SP1+仅 KB2670838 已实测 FLIP_SEQUENTIAL 可用，必须保持现有 FLIP，不增加 bitblt/swap effect 回退；本轮未取得该实测的设备、驱动、DXGI 和原始日志。当前开发机是 Win11 ARM64，以下 Win7 完整运行格子仍需人工。

| OS/设备 | 预期设备 | 预期 presenter | 静态/自动化检查 | 真机门禁 |
| --- | --- | --- | --- | --- |
| Win7 SP1+KB2670838，硬件支持 FL11.0 | Hardware FL11.0；11.1 枚举 E_INVALIDARG 后重试 | DComp 探测失败后 ULW | 核返回码、feature level、ULW FLIP、透明 alpha/dirty | 启动、书写、透明、resize、sleep/device-lost |
| Win7 SP1+KB2670838，硬件不支持 FL11.0 | Hardware 创建失败→WARP FL11.0 | ULW | 核 Hardware 失败后资源清理及 WARP 初始化，Shader 的 FL11.0 要求 | 同上，加 CPU/帧延迟/长期资源 |
| Win7 SP1+KB2670838，Hardware 与 WARP 均失败 | 明确初始化失败 | 不显示错误透明画布 | 核错误报告、窗口/线程清理、无输入捕获 | 启动失败无残留窗口或重启循环 |
| Win11 ARM64 开发机 | 独立无 HWND FL11.0 probe：Hardware/WARP 都创建成功，D3D11_OPTIONS 查询成功且动态 SRV NO_OVERWRITE=1；强制 false 的 DISCARD 回退也经 WARP buffer 回读 | Draw3 配置 DComp 优先、失败到 ULW；UI3 Bar 在隔离 GUI 烟测为 WARP/ULW 已提交，Select 下 Draw3 双画布隐藏 | 最终 Debug/Release ARM64 主 Solution 与 no-window renderer-map CLI exit0；隔离 GUI 10秒仍运行、Bar可见、Select双画布隐藏，日志 `hf-final-debug-gui-10s-probe.log` 与该实例 UI3Diag | 本机仅证设备创建/上传及 UI3 Bar 此次成功呈现；Draw3 真 DComp/ULW、Pen/输入、Win7 仍未验，不外推目标平台 |

## 最终门禁状态（2026-09-29）

- 已完成：三架构 PE 头/import 静态检查、Hardware/WARP FL11.0 代码分支检查、DComp→ULW 代码路径、ULW `FLIP_SEQUENTIAL` 保持、DWM 两路径拒绝、三架构 Release 构建及无窗/隐藏 Host 自动回归。
- 未完成：Win7 SP1 仅 KB2670838 真机启动；硬件支持 FL11.0；硬件不支持 FL11.0 的 Hardware 创建失败→WARP FL11.0；DComp 不可用时 ULW FLIP 的真实 swapchain/alpha/dirty/resize/device-lost；真实触摸、长时间资源趋势、HC/H2 同设备体验。
- 用户明确提供的 Win7 `FLIP_SEQUENTIAL` 实测优先于微软通用 Swap effect 文档；本任务不增加 bitblt 回退，也不把 Win11 ARM64 或静态 import 结果外推为 Win7 通过。

H0 源码的自动列表为 DComp→DWM2→DWM→ULW；当前已完成 ARM64 Debug/Release 构建的工作区 diff 改为 DComp→ULW，并在自动、强制与恢复共用的初始化入口拒绝 DWM。`Draw3.GraphicsInitialization.cpp` 静态显示先尝试 Hardware `[11.1,11.0]`，收到旧系统 `E_INVALIDARG` 后以 `[11.0]` 重试；Hardware 失败后尝试同样协议的 WARP。`Draw3.TransparentPresentation.cpp` 仍使用 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`。这仅证明代码分支存在，不能证明 Win7 驱动、FL11.0 缺失机器与 WARP 上成功 Present。微软 Swap effect 文档写 Windows 8 起支持，与用户 Win7 实测不一致；该通用文档不作为回退依据，需在最终兼容报告注明冲突与本轮复验范围。官方参考：Platform Update https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7 ；Swap effect https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect ；UpdateLayeredWindow https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow 。

2026-09-29 本机静态补证：完整 Release|x64 与 Win32 Solution 重建均 exit0；`dumpbin /headers` 分别为 machine `8664`/`14C`、GUI subsystem 与 OS/subsystem `6.01`。两份 `/imports` 均无静态 `GetDpiForWindow`、`GetDpiForMonitor`、`DCompositionCreateDevice`、`AddDllDirectory`、`SetDefaultDllDirectories`，均有 `D3D11CreateDevice`；`Shcore.dll`/`dcomp.dll` 从 System32 绝对路径动态探测，缺失时走既有 fallback。原始 `hf-final-release-{x64,win32}-pe-{headers,imports}.log`、`*-dependents.log` 在忽略 TestResults。此证据仅覆盖直接导入与本机重建；不证明所有延迟/COM 调用在 Win7 SP1 仅 KB2670838 上能创建硬件/WARP、FLIP ULW swapchain 或输入呈现。Win32 本机 x86 仿真真实 UEF suite 先因 dump 未生成 exit62，后以首因记录和有条件 `MiniDumpNormal` 回退得到46,440B MDMP、整套exit0；ARM64/x64修改后高保真dump仍exit0。三者都不是 Win7 SP1+仅KB2670838 运行证据，默认手动模式0报告阶段有界保护另验。
