# 共用定格 Mag 的截图验证与后续门槛

## 范围与当前阶段

2026-10-21 用户后续明确收窄：直接共用定格已有 Mag，在 Inkeys.exe 内验证 PrintWindow(2)，移除独立 Capture-Only 原型与共享 runtime lease。当前未实现连续滚动会话、Tracker、Draw3 视口、主栏或 UInk 接入。旧独立方案的接口与结果仅属历史，不能作为本轮实现合同或当前通过证据。

## 既有线程与寿命

- Inkeys3 的 WindowService Overlay STA owner 在 CreateGroup 内调用 PrepareMagnifierWindow / MagnifierHostCreated / MagnifierChildCreated，并通过 readyPromise 等待就绪；保留同线程初始化与窗口创建，不新增第二对 Mag 或另一次初始化。
- Inkeys2 2025-02-15 的 0c0aaf0e99a4d5ced0902c1d692855ef61449f96 使用窗口线程与 promise/future 同步创建；后续协调线程的分工在 Inkeys2 也存在。用户确认当前 Inkeys3 保留关键设计即可，无需重新路由普通定格操作。
- 普通 MagnifierThread / Coordinator 更新方式与 WindowService 创建、销毁保持原样。本轮不得为读回验证加入通用 owner RPC、全局运行库 lease 或不必要的 Window 生命周期改造。
- 正常退出先 StopMagnifierCoordinator / join producer，再 StopAndJoin WindowService，由原 owner 反初始化。未来如引入并行捕获或异步长读回，需另行设计真实资源排空与销毁边界；本轮诊断不能代替该验收。

## 复用路径与像素

新增显式诊断入口 --freeze-mag-printwindow-test / RunFreezeMagnifierPrintWindowTest()，仅 harness 可用；harness 关闭或参数错误直接返回，不进入普通 GUI。入口位于用户配置、单实例及普通业务初始化前，专用窗口自动关闭、输出进入临时目录，不读写用户文档与配置。

诊断调用真正的定格生命周期和 Win32MagnifierOperations，不复制 Mag 初始化。按 PrepareHidden → SubmitFilter → SubmitSource → Invalidate → Redraw → 等待消息泵 → PrintWindow(child, dc, 2) / GdiFlush 执行。新增诊断在原 Host 的消息/定时消息处理，普通 Freeze 前后对照仍走原协调线程。

- PrepareHidden 先 alpha=0 再显示，保留可绘制生命周期；不把 SW_HIDE、最小化或屏外当作等价状态。父 Host 使用原 DefWindowProc，禁止与测试源共用按阶段同色的绘制逻辑。
- 排除沿用 BuildCurrentFilterList 的角色窗口：Mag、Freeze、双画布、分页、Bar、Setting；不能描述为已经枚举所有未知同进程顶层窗口。排除失败仍是失败，不能继续把包含覆盖层的画面当作底图。
- 尺寸依据真实 child client 与 MagGetWindowSource。原 Host 避全屏高度少一像素、原 Source 宽高少一像素；本轮如实记录，不悄悄变更几何或伪报完整主屏元数据。
- API 返回成功只证明调用成功。专用图案须有唯一变化阶段、已绘制确认与分布像素核对；蓝色覆盖窗须先有屏幕正对照，再证明原排除后读回底层图案。
- 首帧允许较长等待、后续允许上一帧延迟。固定等待、RedrawWindow 或 DwmFlush 不能当作最新 compositor 帧的完成凭据。静止画面重复有效，不能仅因 hash 不变判断停更；变化内容必须最终更新。
- PrintWindow 属于同步调用；测试进程的最终超时强制退出只保证专用窗口不遗留，不算正常资源清理通过，也不能被迁入产品作为取消办法。

## 可选回调候选

无回调必须可用，不能将回调作为捕获前提。测试从尚未登记回调的原冷窗口开始，靠更新、消息泵、等待与必要重试取得静止和变化画面，并在恢复无回调后继续验证。用户允许上一帧和延迟；当前轮确认此项准备完成后结束，不提前进入后续阶段。

用户要求实际尝试 MagSetImageScalingCallback / MagGetImageScalingCallback。动态解析、检查登记结果并保存/恢复原值；只作为同步候选，图像仍由 PrintWindow 获取。回调只做必要直通与计数，不保留系统借用图像，不等待、不输出日志。记录调用线程、进入/退出与实际像素关系。

回调 API 已废弃且文档存在 DWM 条件限制；不能仅因这一点跳过用户要求的实测，也不能预先承诺可用。登记失败或未触发时使用有界等待，不关闭 DWM，不增加禁止捕获属性，不扩展 Win7 图像导出。回调返回后的计数属于实验信号，只有实测才能说明本机行为，不能视为所有系统最终整帧完成保证。

## 检查与错误矩阵

| 条件 | 必须记录或行为 |
| --- | --- |
| 非法参数或 harness 关闭 | 立即退出，不落入普通产品初始化 |
| 初始化与窗口建立 | 记录 PID/TID/HWND，核对原 owner 与 ready 等待 |
| 静止、连续变化、首帧/旧帧 | 分别验证有效像素；等待可接受但永久不更新失败 |
| 蓝覆盖窗 | 先证明确实遮挡，再验证原角色排除后的多点底图 |
| 回调登记失败 / 零触发 | 如实报告，不能宣称验证了回调完成信号 |
| 回调登记前、期间、恢复后 | 比较真实 PrintWindow 图像，原回调必须恢复 |
| 普通定格前后 | 走真实协调路径；像素诊断与原定格行为结果分别报告 |
| 退出 | 窗口销毁、MagUninitialize 线程与 GDI/USER 变化；超时退出另列 |
| 后续完整滚动批注 | 未实施，不能用此诊断放行主栏/Viewport 或宣称功能完成 |

实际构建使用完整 InkeysRepo.sln Debug|ARM64、vswhere 定位 ARM64 原生 MSBuild，在同一次 PowerShell 规范 Path/PATH 并禁用 node reuse。已知 sandbox 的 MSVC C1902 必须先作为环境差异核对，不通过改项目/PDB 配置绕过。定向软件内测试后执行现有 --no-window 及 diff、工程 XML、物理编码/BOM/EOL 检查；没有资源/i18n改动时沿用现有检查范围。

当前验证以捕获子任务最新 implement.md / research 为准。历史原型与快速独立探针只用于解释失败和实验变量，不能替代实际软件结果。旧 Windows、触控/笔、锁屏、浏览器/Word/PDF 及长期性能未覆盖时逐项列明。

参考：[PrintWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow)、[MagSetWindowSource](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowsource)、[MagSetImageScalingCallback](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetimagescalingcallback)。
