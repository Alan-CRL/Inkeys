# P3 短产品 Probe 与原生错误观测实施

2026-10-23；仅实现分配给 p3_product_probe 的文件。未构建、未启动 GUI、未运行实屏测试。Root 已报告 P2 尝试在初始化前被安全界面门阻止；此事实不能证明排除通过，P2/P3 像素、GUI 与性能继续为 **UNVERIFIED**。

## 文件与边界

- `Inkeys/IdtMagnification.cpp`：保留正式 backend 的原 API 顺序/短路/决定，仅明确文档支持 GetLastError 的 SetLayeredWindowAttributes/GetClientRect 失败时立即保存 nativeError/valid。PrintWindow、GdiFlush、RedrawWindow、InvalidateRect、MagSetWindowFilterList、MagSetWindowSource、MagGetWindowSource、MagGetWindowTransform 的 Return value 未承诺设置 last-error，全部使用0/false，不把残留线程错误伪装为本次采集错误。包装 DIB/Service 或几何值不一致同样 valid=false。PW 后仍总是执行原 GdiFlush；未改 FreezeCapture 文件。
- 同文件的 `--scroll-follow-debug-exclusion-test` 从产品 `ReadProbe` 获取实际绘制的 previewRect、previewBackground；检查普通窗口、当前设置 owner、背景像素与实际命中、窗外源图案、未覆盖的逐像素源 ROI，并要求正式 Mag 返回被窗口覆盖的 256×192 ROI 与源吻合（沿原每通道容差3）。保留两图案各至少三帧、真实关闭/重建过滤、正常 Stop/drain；不进入原默认测试长尾。默认 RunFreezeCaptureLiveTest 行为和阈值未改。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`：新私有第四参数复用原空页产品、原始 Mag、真实 Business/StateMonitoring/Channel/ACK 与私有根目录。
- `Inkeys/IdtMain.cpp` / `IdtMagnification.h`：严格单参 `--scroll-follow-debug-product-test` 在普通初始化前进入；错位/多参/后缀与 harness-disabled 按原派发门返回2。新入口未增加 EXE/工程项。
- Main 正常窗口服务停止后，以及 capture/product fixture 停止服务后，调用 UI 所有者提供的 `ShutdownExports()`。optional Open 之后没有其它提前返回路径；创建前失败不可能持有导出任务。

## 真实短流程

初始化并等待真实 Tracking/双窗口 ACK 后，同一 business/capture session 执行 absent → open → closed 三个各15秒阶段，各有独立1秒热身。Open 计时之后、controls hold 之前，在仍可见的普通诊断窗前执行正32/反16的真实 tagged wheel，Source 前台且仅以 HWND_TOP|SWP_NOACTIVATE 抬本进程诊断窗。滚轮点(32,height/2)必须实际命中 Source 并位于当前调试窗外；每次收敛后重新核验窗外GT和覆盖输入。Closed 计时处于同一世界纹理的最终+16静止姿态，不声称三阶段截图逐像素相同。仅 open 阶段降低专用 Source 的 z-band，使用同一产品 Open factory；不改变调试窗普通/非置顶属性。所有阶段共用 tiny 数值 LiveTrace Observer，固定存储只含计数/和/最大值；不启用 MotionTrace，不记录输入数组。显示 totalTrackerMs 只对实际已显示 packet 采样，不把分段和冒充总耗时。

日志分别给 Capture accepted FPS/interval/Read、attempts/rejected/API/skipped、实际 Tracker 各分段、预览复制/显示年龄/刷新/消费/绘制速率以及阶段边界 process working set/private bytes/GDI/USER。Capture Read 是 backend.Read 时长，**不含 Analyze，也不是隔离的 PW 时间**。本机有限观察不是通用 benchmark。

open 时另外在计时窗外检查真实调试窗 header 留白3×3像素和 WindowFromPoint，源99点仅跳过精确的当前自有调试窗外框；原(64,64,256,192)精确源图案保持。实际借用的 Tracker 输入中，预览下方256×192 ROI 必须仍匹配底层源图案（容差3），不是列表成员判断。

open 性能之后有单独75秒 GUI 控件时段。日志写出实际按钮屏幕中心与本次私有 export 目录。Root 可操作 Freeze、标记、倍率、zoom/pan、导出与恢复；观察器只读核验冻结身份不变且 accepted 继续，记录实际操作。未观察到的操作保持 UNVERIFIED；resumed_newer_frame 只证明恢复后显示了新帧，另记与当前 Capture sequence 的间距，不把它冒充完整 latest-only 证明。导出文件仍须独立检查。

真实 WM_CLOSE 后要求 Service/Handle 消失、Enabled=false、旧 Channel weak_ptr 过期，再恢复原专用源层级并执行 closed 计时。原 InkWheel 的 tagged OS 正32/反16已在 open 时段执行，保持最终16px；正常 Off/drain/真实反向 ACK/工具内容不变断言仍执行，再增加真实 Off/on/off 验证关闭后不自动重开。无 Tracker/Target/viewport/ACK mutator。

仅新模式 watchdog 为210秒；默认短90秒和长480秒不变。所有安全层/交互桌面门、取消、own input归还、MTA、私有配置/保存根与原线程清理保持。安全层出现不继续前台或像素操作。

## 验证

- 已执行四源码的 UTF-8 解码/BOM/全CRLF断言：三个传统 cpp/h 保持 BOM，inl 保持无 BOM。
- 已执行 assigned source 的 `git diff --ignore-submodules=all --check`，通过。
- 已静态核对默认 wrappers、harness 两侧新入口及 watchdog 分支；未运行 MSBuild/CLI/GUI，按 Root 的并行写入约束留待集中构建。
- 未新增依赖、VCX 项、长 UInk/Tracker 矩阵、记录系统或可变测试接缝；没有 commit/push/PR。

## 待验收

Root 使用完整 ARM64 Debug solution 构建与定向短检查。解锁并实际通过既有安全门后，分别运行最小排除和新产品入口，严格等待进程退出代码，检查真实像素/资源/阶段日志。新产品布局要求主屏至少1344×720；其它尺寸本夹具明确失败，不是产品最低系统/屏幕要求。GUI、有限性能、PNG/JSON 导出和手工控件未实际执行前均不可写为通过。Rejected/Paused GUI 未构造专用模式，保持人工/UNVERIFIED，不扩大成长夹具。

## 原生错误有效性审查来源

按 Microsoft 的 [GetLastError 合同](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror)，未声明写 last-error 的函数可能留下先前值。只有 [SetLayeredWindowAttributes](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setlayeredwindowattributes) 和 [GetClientRect](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclientrect) 在本次检查的生产调用中明确支持失败后的 GetLastError，因此只对它们发布 nativeErrorValid=true。

已逐项核对无扩展错误承诺：[PrintWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow)、[GdiFlush](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush)、[RedrawWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-redrawwindow)、[InvalidateRect](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-invalidaterect)、[MagSetWindowFilterList](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowfilterlist)、[MagSetWindowSource](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowsource)、[MagGetWindowSource](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-maggetwindowsource)、[MagGetWindowTransform](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-maggetwindowtransform)。这不是断言实现永不写 last-error，而是其值不能作为有保证的本次失败凭据。
