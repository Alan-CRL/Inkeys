# Design

## Dependency Boundary

ImFluent 作为带来源记录的源码快照进入 `Inkeys/additional/imfluent/`，产品工程直接编译 `imfluent.cpp`。本地补丁只在公开头文件增加 `SetFluentTextStyleFont` 和 `ResetContext`，后者对静态 `g_Ctx` 做值重置；全部补丁用 `INKEYS PATCH` 标记并在 UPSTREAM 文档列出。用项目已内嵌字体绑定 ImFluent 文本层级，避免 Segoe 系统字体依赖。

## Resident Session State

Setting 渲染客户端维持两个正交维度：resident session 和 presentation state。resident session 由渲染线程拥有 ImGui context、Win32 backend、DX11 backend、font atlas 及图片 SRV；presentation state 只拥有 swap chain/RTV 和可见/occluded/resize 状态。

`Initialize()` 注册客户端后发布初始化请求，并用已有同步栅栏等待首个渲染回调完成 resident 初始化。Hide 只隐藏 HWND、清除 continuous 位并销毁 presentation state。Show 显示/聚焦 HWND、请求 Setting，并在渲染线程创建 presentation state。epoch 变化先销毁旧 presentation、图片和 DX11 backend，再用新共享设备重建 backend/图片；隐藏时在此结束，显示时继续创建 swap chain/RTV。Shutdown 同步在渲染线程逆序清理全部资源，再注销客户端。

## Window Contract

HWND 延续 Window Service 单次创建。窗口使用无 `WS_CAPTION` 的 `WS_POPUP | WS_THICKFRAME` 顶层样式，Inkeys 在 client area 绘制标题栏。普通态 `WM_NCCALCSIZE` 保留四边 1px 可见 non-client frame，resize hit zone 则从 DPI 对应的系统 frame metrics 推导；最大化态的 non-client geometry 完整交给默认过程。`WM_NCHITTEST` 先解析 resize hit，再保留默认过程的非客户区结果，仅对 `HTCLIENT` 解析 caption buttons、图标和 `HTCAPTION`。

## DPI, Theme And Layout

唯一逻辑尺寸单位为 DIP。系统 DPI 与 Setting 全局倍率计算 effective scale，并写入 ImGui `FontScaleDpi`/对应字体构建；控件代码不再次乘自定义倍率。为先集中调整浅色样式，当前主题解析无条件选择 ImFluent Light preset；Windows AppsUseLightTheme、高对比状态和相关消息仍可经过既有刷新入口，但不得改变 Setting 的浅色结果。后续恢复动态主题时复用该边界，不在本轮新增持久化主题配置。

背景材质使用运行时能力级联：先尝试 `DWMWA_SYSTEMBACKDROP_TYPE/DWMSBT_MAINWINDOW`，再尝试旧 Win11 Mica attribute 1029，最后通过 `GetProcAddress(user32, "SetWindowCompositionAttribute")` 尝试 Win10 Acrylic。每次重应用前先撤销旧 backdrop、legacy Mica、Acrylic、redirection alpha 和 extended margins；任一层只有属性与所需 frame 调用均成功才发布透明渲染状态，否则继续下一层并最终回到 Solid。只有非 Solid 状态把 ImFluent `SolidBgBase`、ImGui `WindowBg` 和 D3D clear alpha 设为透明；Win7/API 缺失路径不改变现有不透明渲染。Setting session 退出时在 HWND 销毁前显式撤销材质并发布 Solid。

宽/中模式使用 ImFluent NavigationView 的 LeftOpen/LeftCompact；窄模式以 ImFluent CompactOverlay SplitView 承载同一导航模型。内容页使用滚动 child、最大可读宽度与 wrap layout。项目适配层仅负责响应式 settings row、自动换行文案、图片卡片和标题栏按钮。

## Migration And Compatibility

页面路由、状态、业务 action 和 FIFO worker 保持原实现，按页面逐段把旧伪 Fluent 外观替换为 ImFluent。主页单独重写。ImFluent 输出仍是标准 ImDrawData，因此现有 CSO backend 无接口变化。ImFluent 依赖 `imgui_internal.h`，固定 commit 与 ImGui 1.92.7 作为成对升级单元。

若集成出现阻断，可先保留页面业务渲染函数并仅启用 ImFluent 主题/导航适配；不得回退 resident lifecycle 或窗口合同。第三方升级通过替换上游快照、重新应用两处标记补丁、ARM64 严格编译与完整 UI 验收完成。

## Fluent2 Composition Follow-up

窗口主体遵循固定上游 Demo 的组合顺序：`BeginNavigationView` 渲染 pane，`EndNavigationView` 后立即进入 `NavigationViewBeginContent`，全部业务页面都在该滚动 content child 内绘制并由 `NavigationViewEndContent` 收口。窄屏使用 SplitView CompactOverlay 承载同一导航函数。不得把导航和业务内容继续作为两个互不相关的绝对坐标层。

`Setting.Widgets` 只保留旧页面调用签名的兼容职责。Toggle、Button、Slider 和 Combo 的最终绘制必须委托 ImFluent；Combo 的旧 Begin/Selectable/End 状态机迁移为一次性 `ImFluent::ComboBox` 后，页面调用点同步改写。标题栏的 Windows caption buttons 属于窗口 chrome，可保留专用绘制。全局更新底栏删除，版本页仍拥有更新状态和业务操作。

## Client-Drawn Chrome Follow-up

自绘标题栏始终启用，不能再以 DWM composition 作为显示开关。普通态以 1px NCCALCSIZE frame 保留系统 non-client border，resize 热区使用系统 DPI frame metrics；不使用 extended frame 或自定义 border color。客户区始终绘制 32 DIP title bar、46 DIP caption cells、应用图标、标题和版本 RightHeader。caption 几何继续由 `ResolveTitleBarGeometry` 同时提供给渲染与 client-titlebar hit-test。

窗口拖动、双击标题栏、系统菜单和 Snap 走 Win32 non-client 路径。caption button 的 pressed hit 由 WndProc 原子记录，只有同一按钮内释放才投递 `SC_MINIMIZE/SC_MAXIMIZE/SC_RESTORE/SC_CLOSE`。系统 move loop 中的 `WM_NCMOUSEMOVE(HTCAPTION)` 不得请求 D3D11/ImGui 帧；native sizing frame 与最大化/还原 geometry 始终保留 Windows 默认处理。

系统 move/size loop 使用原子 `None/Move/Size` 状态，鼠标 hit 与 `SC_MOVE/SC_SIZE` 共同决定类型。只有 Move 在 swap-chain 操作和 coroutine Resume/Present 前返回 `Idle`；Size 中 `WM_SIZE` 持续 `QueueResize + Request`，渲染线程消费 latest serial 并执行 `ResizeBuffers/Render/Present`。`WM_EXITSIZEMOVE`、Hide/Shutdown 清除状态并请求最终帧。
