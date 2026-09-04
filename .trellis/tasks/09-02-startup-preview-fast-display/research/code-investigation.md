# 源码调查结论（历史基线，仅供溯源）

> **历史记录，不是当前产品需求。** 本调查在上一版 embedded BIN、磁盘图片 cache、Gaussian blur、Bar proxy/staging 和 developer capture 方案下完成。最终方案已改为程序化中性灰圆角占位；其中关于图片格式、签名/CRC/epoch、proxy/staging、cache writer、图片资源和 capture 的结论均已废弃，不能指导当前实现或验收。仍有效的内容仅限启动边界、DPI、共享 RenderPipeline、ULW/owner 线程和 Bar committed alpha 等基础设施事实。

## 入口、SuperTop 与 DPI

- `wWinMain` 位于 `Inkeys/IdtMain.cpp:139`。SuperTop 主块为 `:591-743`，所以合法 T0 是紧随其后的 `:744`，不是日志或图形初始化之后。
- `Inkeys/SuperTop/IdtSuperTop.cpp:223-253` 的 `LaunchSurperTop()` 通过 `runas` 启动 helper，等信号后父进程 `exit(0)`；`:52-218` 的 helper 等原进程退出后用 `-SuperTopComplete` 创建最终进程。入口中的 helper 分支同样直接返回。
- 当前 DPI 逻辑在 `IdtMain.cpp:856-894`，位于大量文件/系统工作之后；动态加载 Shcore 并调用 `SetProcessDpiAwareness`，但所有失败都警告后调用 `SetProcessDPIAware()`，没有把“已由其他来源设置”的 `E_ACCESSDENIED` 视为成功。
- 旧 drawpad/tester surface resize 在同一 DPI 块 `:886-891`，应与进程 awareness 拆开，后者提前到任何 HWND 和 DPI-sensitive 初始化之前。

## RC 与构建

- `Inkeys/Inkeys.vcxproj` 仅有 Debug/Release x Win32/x64/ARM64；没有 ARM64EC project configuration，源码在 `IdtMain.cpp:294-296` 有 `_M_ARM64EC` 分支。
- 六个配置均设置 `GenerateManifest=false`；虽然 Manifest Tool 配置有 `EnableDpiAwareness=PerMonitorHighDPIAware`，但关闭 manifest 生成后它不会形成 EXE manifest。
- `Inkeys/Inkeys.rc:214` 将资源 221 的 `RT_MANIFEST` 指向 `../PptCOM/PptCOM.manifest`。`IdtMain.cpp:1213-1220` 用 `CreateActCtx/ActivateActCtx` 显式激活它；它不是 EXE 资源 ID 1 的 DPI manifest。
- `resource.h` 的 301-304 是 Draw3 shader，305 是 MessageBox error；下一可用资源 ID 是 306。第二阶段计划让默认 BIN 使用 306 和专用资源类型，不改 221。
- 现有 `InkeysRepo.sln` 和 `InkeysHeadlessTests` 均有 Win32/x64/ARM64 Debug/Release 配置；主工程依赖 PptCOM，按根 AGENTS.md 必须构建完整 Solution。

## Config、Setting 与 Display

- `Other.Config.cppm:252-303` 的 UI3 实验配置当前有 Debug、EdgeLighting、Animation；新字段归属明确为 `Experimental.Inkeys3.UI3.StartupPreview.Enable`，类型沿用 `IdtAtomic<bool>`，默认 true。
- `Other.Config.cpp:677-681` 的 `ReadMini()` 最终进入 `ReadImpl()`；`:745-764` 先 `ApplyDefaults(paths)` 再叠加文件，因此缺失文件/字段也可得到 true。
- early read 只需要开关及构造 visual signature/几何所需的 `UI.Bar.Zoom`、`FixedButtonsA1`、`ExtensionButtons`、`FixedButtonsA2`。主题和语言目前固定深色/中文，不另读冗余字段。
- `Setting.cpp:1310-1328` 维护 UI3 session snapshot，`:375-398` 的 `QueueConfigWrite()` 在 setting worker 上写配置；实验选项位于 `:8018+`，增加一张 70 DIP 卡片并增加 75 DIP 内容高度即可。
- Display 的 `Initialize()` 同步 Refresh；`Display.cpp:197-292` 动态加载 `GetDpiForMonitor`，否则用 `CreateDC/GetDeviceCaps`，再枚举 monitor/registry EDID。进程 DPI awareness 必须先完成。

## RenderPipeline

- `RenderPipeline.cppm` 当前 client 为 Bar、四个 PPT/PageControl client、Settings、WhiteboardFreeze。新增 StartupPreview，dispatch order 必须插在 Bar 之后，让同一周期先发布最新成功 Bar proxy。
- `RenderPipeline.cpp:482-523` 初始化是幂等的，已初始化返回 `S_FALSE`；创建 multi-threaded D2D factory、DWrite、WARP epoch 和唯一 scheduler render thread。现有初始 backend 是 WARP。
- `:62-123` 先请求 FL11_1/11_0；收到 `E_INVALIDARG` 后只用 FL11_0 重试，符合 Win7 Platform Update 行为。
- callback 在 `:278-410` 串行 dispatch；`Unregister` 在 `:430-437` 清空 callback 并等 active count 归零，不能从自己的 callback 内调用。
- 设备丢失会恢复新 epoch。Preview 的 target、bitmap、effect、brush 和 proxy 都必须按 generation 重建；禁止把可变 Bar target 跨 client/thread 暴露。

## Bar 提交与初始化

- `Bar.Initialization.cpp:115-170` 依次完成窗口、UI、Display tracking、format、preset/components/state/position、mouse hook、render client 和 interaction；窗口不存在、停止信号和 client 注册失败可 silent return。
- `Bar.Rendering.cpp:65-124` 的 target 是 BGRA8 premultiplied、GDI compatible 的 D2D bitmap，随 device generation 更新；它本身是可变 target，不能直接作为 Preview 输入或缓存源。
- `Bar.RenderLoop.cpp:831-844` 注册/注销 `Client::Bar`；`:846-862` 当前把 `blend_.SourceConstantAlpha` 固定为 255。
- `:12357-12392` 依次 GetDC、UpdateLayeredWindowIndirect、ReleaseDC、EndDraw；只有全部成功时 `Bar.PresentDecision.h:144-151` 的 `IsCommitted()` 为真。失败路径 `:12577-12600` 保留 full-dirty retry。
- 成功后才推进 dirty/viewport/present mapping，并在 `:12616-12619` 设置 floating window visible。这是唯一可靠的首帧、100%、交接和缓存候选门。
- 安全帧桥接应在渲染线程把精确 crop 复制到 `D2D1_BITMAP_OPTIONS_NONE` bitmap；缓存抓取再复制到 `CPU_READ | CANNOT_DRAW` staging，Map 后只把普通内存交给 writer。

## Window Service、Draw3 与其他启动生产者

- `Inkeys/Inkeys/Window/Window.cpp:152-195` 的 Window Service 启动 overlay/setting 两个 owner thread 并等待 promises；`:746-946` 在 owner thread 创建 HWND，`:1036-1074` 用 queue + promise 跨线程执行，`:949-998` 在 owner thread 销毁。
- `Window.cpp:1421-1440` 的 topmost refresh 只更新 owner chain root。Preview 在 Window Service 之前创建，所以应有独立 owner thread，并注册“成功 refresh 后只 post、不反向同步调用”的 observer。
- 实际编译的周期 topmost 逻辑是 `Window.Legacy.cpp:90-171`；旧 `IdtWindow.cpp` 仅为工程 `None`。
- `Draw3.Product.cpp` 调用同步 `Host::Start()`，内部与 drawing thread 握手。`Draw3.Host.cpp` 可报告 external HWND attach（484-494）、graphics device（519）、presenter（535-539）、RTS（656-671）、controller（575-576）和 first present（582-584）。回调只做原子 milestone 报告。
- Whiteboard `Whiteboard.cpp:232-268` 注册 PageControl/Freeze/Display；Freeze 当前忽略 ULW 结果；PPT UI 在 `IdtPlug-in.cpp:624-640` 注册 client。它们适合作为并行真实 milestone。

## 可复用可靠性代码

- `Draw3.AutoSave.cpp:327-352`、`:477-561` 已示范临时文件、flush 和替换的强可靠写法；Startup cache 应沿用同等级 durable write，不把失败升级为致命。
- 仓库第三方 zip 代码含 IEEE CRC-32，Abseil 提供的是 CRC32C。为了避免把新模块耦合到 zip 内部，计划在 StartupPreview cache 内保留小型 IEEE CRC-32 实现并用标准向量测试。
- 现有 Bar/D2D 路径已有 GaussianBlur、Balanced/Soft border、`FillOpacityMask` 与抗锯齿恢复范式，应复用而不是引入新图形框架。
