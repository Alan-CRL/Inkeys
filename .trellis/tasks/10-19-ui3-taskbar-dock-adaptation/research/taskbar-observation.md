# Research: 任务栏方向、自动隐藏显隐与性能

- Query: 如何复用当前Display任务栏采样，准确区分方向、自动隐藏模式及实际升起状态，并保持兼容与低开销。
- Scope: mixed（源码 + Microsoft官方API）
- Date: 2026-10-19

## 当前代码证据
- Inkeys/Inkeys/Display/Display.cppm:99–117：MonitorInfo仅保存taskbarThicknessPx，没有taskbar edge、autoHide、实际shown/hidden或dock boundary。
- Display.cpp:379–467 SampleTaskbars：已有ABM_GETAUTOHIDEBAREX逐屏逐边owner、普通主栏ABM_GETTASKBARPOS、Shell_TrayWnd/Shell_SecondaryTrayWnd兼容枚举、失败缓存；采样后只保留厚度，局部Owner含edge但未发布。
- Display.cpp:668–694 DisplayWindowProc：TaskbarCreated及WM_SETTINGCHANGE等触发Refresh；WM_CREATE设置2秒timer，Taskbar刷新不重新枚举EDID。
- Display.cpp:977–1022 Refresh：Taskbar分支复制上次快照，仅重采任务栏；SemanticallyEquals去重后发布generation，所有内部锁外通知订阅者。
- Inkeys/Inkeys/Window/Window.cpp:973、1003、1139：已有DisplayObserver，归Window Service owner消息循环；无需为了事件监听新增长期线程。
- 本轮前工作树干净；本轮仅创建新的Trellis任务，上一任务提交授权不延续。

## 官方API核查
- ABM_GETAUTOHIDEBAREX按monitor矩形及edge返回自动隐藏appbar HWND；返回空既可能无该appbar，也可能错误，不是当前收起/升起状态。保持旧SHAppBarMessage调用，旧Shell不支持时需普通GETAUTOHIDEBAR/GETSTATE等兼容回退。https://learn.microsoft.com/en-us/windows/win32/shell/abm-getautohidebarex
- ABM_GETSTATE的ABS_AUTOHIDE只表示配置为自动隐藏；不能用它判断当下是否已经升起，也不能通过填目标monitor让它成为逐屏查询。https://learn.microsoft.com/en-us/windows/win32/shell/abm-getstate
- IsWindowVisible只反映WS_VISIBLE及父链，窗口即使被遮挡仍可返回true；隐藏任务栏可保持该style，不能把它当实际升起证据。https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindowvisible
- EVENT_OBJECT_LOCATIONCHANGE表示对象位置/大小变化；SHOW/HIDE为额外线索，不能据官方事件文档承诺每个Explorer或第三方Shell动画都一定发完整事件。https://learn.microsoft.com/en-us/windows/win32/winauto/event-constants
- SetWinEventHook的WINEVENT_OUTOFCONTEXT无需注入DLL，回调交付到注册线程，该线程必须有消息循环，且回调可能重入。https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook
- UnhookWinEvent必须在安装hook的同一线程执行；不能直接从Display::Shutdown的任意调用线程卸载。https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unhookwinevent

## 推荐策略（尚未实现）
1. 在现有逐屏任务栏数据上增补可靠性、明确edge、autoHide模式、规范化Hidden/Raised/Unknown状态及稳定任务栏上沿；保留完整厚度作为独立缩放数据。无任务栏不能直接等同自动隐藏。
2. 实际显隐以归属本屏的任务栏窗口屏幕几何/有效可见带为主要证据，配合可见style及必要的cloaked查询（若采用，动态调用/失败可降级）。自动隐藏的微小露出条须视为Hidden，正常升起可按完整厚度推导稳定top；禁止用动画中每个rawRect.top当新目标。
3. 安装非注入WinEvent监听时仅关注已识别Shell进程/任务栏HWND及必要事件，过滤OBJID_WINDOW/CHILDID_SELF；回调只排队合并的轻量更新，不持显示锁调用Shell/枚举/渲染，不在回调内同步Notify产生重入。
4. 复用DisplayObserver owner线程及PostMessage/短timer合并，同一周期最多处理一次更新。建议事件burst合并约16–33ms；相同Hidden/Raised/目标线不发布generation，不唤醒Bar/PPT。
5. 为Shell漏事件保留受限轻量轮询：仅已确认底部autoHide的已缓存HWND可约200ms采样矩形，非相关屏不高频采样；原2秒结构复查仍用于发现任务栏设置变化/Explorer重建。高频路径不得EnumWindows/枚举EDID/重新查拓扑。参数是实施预算，不代表本轮已性能实测。
6. HWND销毁/Explorer重建后失效旧缓存与hook，重新发现/注册；未知期间保留同屏同几何下最后可信dock目标，不凭一次失败跳到屏底。确实没有任务栏时回退屏底，真正结构变化更新geometry。Hook由owner WM_DESTROY等对称清理，迟到事件按epoch/窗口身份拒绝。
7. Shell变化原始矩形保留在观察器内部；发布给业务的snapshot以逻辑状态和最终dock target为主，避免一段Shell动画产生几十代显示快照和Bar retarget。

## 边界与待验证
- 顶/侧任务栏不会作为底部障碍；底部自动隐藏规则与“任务栏厚度作为缩放基础量”开关独立。
- 不依赖Explorer私有注册表、私有消息、跨进程subclass、注入或UIAccess新权限。
- 官方没有统一返回“Windows任务栏此刻已展开”的公开bool；基于窗口几何的判断属于实现推断，Win10/Win11动画或第三方Shell是否同步更新HWND矩形需要后续实机证据，不能在规划阶段承诺所有Shell支持。
- 隐藏露出条阈值/迟滞应抽为纯状态机并覆盖1–数px露出、部分动画、快速反向。具体阈值须依据既有Shell行为与真实验证记录决定，不能让厚度读数随状态动画变化。
- 本轮没有注册hook、探测用户当前任务栏、启动GUI或修改产品代码。
