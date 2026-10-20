# Display observer 实施记录（2026-10-19）

## 责任与合同
仅修改 Display.cpp/Display.cppm、display_tests.cpp；Bar/Startup/Window 由同任务其他代理负责，不改工程、任务状态或 spec。
MonitorInfo.taskbar 保存 TaskbarEdge、autoHide、TaskbarExposure、同屏同几何 lastKnownExposure 和稳定 dockTopPx；taskbarThicknessPx 独立于显隐。
ResolveDesktopBottomDockLine 和 ShouldProtectTaskbarBottomEdge 是统一纯消费者入口；QueryTaskbarMonitorInfo 仅为启动 Mini 查询几何、真实 DPI 和任务栏，不创建窗口、不枚举 EDID、不发布 snapshot。

## 结构发现与厚度
复用现有 2 秒结构采样和设置/显示/设备消息。ABM_GETAUTOHIDEBAREX 明确逐屏 owner，旧 Shell 主屏用 GETAUTOHIDEBAR；GETTASKBARPOS 提供主栏位置/完整厚度，EnumWindows 只补 Shell_TrayWnd/Shell_SecondaryTrayWnd。
兼容副栏按屏边距离归属；现代 EX owner 优先，隐藏矩形不按大部分面积归邻屏。
完整厚度的生产 ResolveTaskbarFullThickness 拒绝触发条/瞬态缩小。主栏 GETTASKBARPOS 完整读数允许立即缩小；没有位置查询接口的副栏，完整可见且与本屏对应边对齐的矩形连续两次结构采样相同才允许缩小（整合审阅补齐Top/Left/Right对称处理）。快速采样永不更新厚度。副栏调整厚度的确认最长可能约 4 秒；不将 Shell 动画中每个高度都当 UI 比例。

## owner 线程、事件与清理
既有 DisplayObserver 的 Window Service owner 消息循环安装非注入 WINEVENT_OUTOFCONTEXT hook。每个已识别 Shell PID 一组，回调再过滤当前 hook、缓存 HWND、OBJID_WINDOW/CHILDID_SELF 和 SHOW/HIDE/LOCATIONCHANGE/DESTROY。
回调仅读 owner-local thread_local targets/hooks，标记纯 TaskbarEventBatch 并 PostMessage，不拿 refreshMutex、不查 Shell、不调用订阅者。owner 合并 25ms，重复事件不会重启期限；销毁事件升级一次结构重查。
仅有已缓存底部 autoHide HWND 时启用 200ms fallback；此路径只读缓存 HWND 的 PID、矩形、可见 style，不 EnumWindows、不 SHAppBarMessage、不枚举 EDID/拓扑。2 秒结构恢复照旧。
Shutdown 先关闭原子 epoch，异步投递 owner cleanup，不在 refreshMutex 内同步等待 owner。卸载 hook 和清理三个 timer 全部由安装线程的停止消息/WM_DESTROY 执行。迟到 callback/message 按 epoch 与当前 HWND/hook 身份丢弃；WM_DESTROY 是最终对称清理。
TaskbarEventBatch 是生产纯状态机，测试覆盖 burst、期限不重启、结构提升、PostMessage 失败重试、重复 timer、旧 epoch 与停止门禁。

## 规范化和回退
微小露出带按真实 DPI 阈值（3 DIP，至少 2px，最多完整厚度 1/4）为 Hidden；达到完整厚度一半为 Raised，中段沿用最近可信端点。Unknown 不伪装 Hidden。目标上沿始终屏底减完整厚度，不跟逐帧 Shell rect.top。
状态/锚点进入既有 snapshot semantic equality，同值不生成新 generation；通知依然在 Display 内部锁外。
查询失败只复用同 HWND monitor identity、deviceName、bounds、真实 DPI 的旧状态；Unknown 优先保留同几何 lastKnownExposure。明确移除副任务栏时清除，不借 UI 比例的主屏 fallback 充当本屏任务栏存在证据。
顶部/左右任务栏纯解析为屏底；普通底栏和 Raised 为稳定任务栏 top；Hidden 为屏底减 round(5*真实 DPI/96)；未知无历史按有效 workArea 降级。PPT/白板优先级由 Bar 原场景层持有，不在 Display 混入业务场景。

## 检查及限制
已检查三个既有源码文件仍为 UTF-8 BOM + CRLF，git diff --check 通过；没有启动 GUI、操控任务栏、commit 或 push。完整 solution 和 --no-window 验证由父会话整合后统一执行，此记录不宣称编译/测试已运行。
新增用例调用生产纯函数，覆盖四 edge/负坐标/真实 DPI、Hidden/Raised/Unknown、显隐迟滞、完整厚度缩小与瞬态保留、快照去重和事件批次生命周期。真实 WinEvent 投递、Explorer 动画矩形、Hook 资源运行时和性能/视觉没有在本轮无窗口范围验证。
Shell 没有公共“此刻已升起”布尔值，基于窗口几何的显隐属于实现推断；Win11/DComp 或第三方 Shell 若不移动 HWND/不发事件，只能根据可获得的窗口几何兼容降级，不冒充已完成跨系统实机验证。
