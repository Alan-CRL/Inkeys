# UI3 窗口所有权调用审计

本轮范围是 UI3 共享渲染与输入热路径中的原生窗口策略和完成判定。ULW/D2D/GDI 的必要呈现事务保留；本审计不将所有 Win32 调用都归为可删的窗口管理。

| 调用链 | 处置与合同 |
| --- | --- |
| PageControl RenderSurface → SetBounds/Show/Hide → Submit(wait=true) | P1 已迁为固定候选/真实回执；ULW 已定位后只 verifyBounds；成功快照不提前推进 |
| Whiteboard RenderBackground → SetBounds/Show/Hide、BackgroundPrepared/Active 的实时 HWND 查询 | P2 已使用相同机制与 owner 快照；独立 contentRevision 退休同请求旧像素；native pending 不重复整屏绘制 |
| PageControl pair 输入直移、Pointer Down 同步 Promote | P3：完整 pair 回执与服务呈现 lease；promote 沿稀疏异步事件，不能被普通像素候选覆盖 |
| Bar 主按钮直移 SetWindowPos | P3：保持原 translation / geometry barrier / 固定表面，owner 执行，完整回执后重基准；ULW 与直移不能相互拉回 |
| StateMonitoring Prepare/Complete 同步返回 | P4：沿原协调器异步请求/回执推进，进展通知唤醒既有等待；不以通知替代 ready、不放宽失败预算 |
| Bar tool clickFunc → SyncDraw3State → Setting owner / Draw3 原生 reconciliation | 补充同类入口：UI 只发布业务状态与原生 desired，唤醒现有 StateMonitoring 执行原生副作用；不能让共享输入 worker 跨 HWND owner 等待 |
| Freeze 按钮/快捷键 → RequestTopmostRefresh | 补充显式非阻塞 request-only 入口；不改变旧同步 API 的 bool 完成语义；失败恢复和既有周期刷新保留 |

生命周期 Create/Destroy/Start/Stop、停止渲染并注销后的 Whiteboard/PageControl 清理仍允许必要同步。Ppt 后台事实发布和独立业务协调路径的原生操作与 render callback 区分；不得为了本任务将整个 Service 的同步 API 偷换成入队即成功。

原生 WndProc 的触摸/鼠标归一化、捕获票据、屏幕坐标以及输入排斥使用的 WindowFromPoint/GetCapture/ClientToScreen 不是背景 ready 证据；它们继续按原输入合同执行。诊断不进入默认实时 HWND 采样。业务身份/工具版本、设备资源与 Window 实际代次各自保留，不能仅凭一个裸原子 RECT 代替完整事务。

最终完成情况、构建/测试与未覆盖的真实 Office/WPS/DWM/设备场景以 verification 为准。本文件不代替规范，也不将计划中的 P3/P4 写成已通过。
