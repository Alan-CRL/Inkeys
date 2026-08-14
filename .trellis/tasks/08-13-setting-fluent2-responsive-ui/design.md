# Design

## Dependency Boundary

ImFluent 作为带来源记录的源码快照进入 `Inkeys/additional/imfluent/`，产品工程直接编译 `imfluent.cpp`。本地补丁只在公开头文件增加 `SetFluentTextStyleFont` 和 `ResetContext`，后者对静态 `g_Ctx` 做值重置；全部补丁用 `INKEYS PATCH` 标记并在 UPSTREAM 文档列出。用项目已内嵌字体绑定 ImFluent 文本层级，避免 Segoe 系统字体依赖。

## Resident Session State

Setting 渲染客户端维持两个正交维度：resident session 和 presentation state。resident session 由渲染线程拥有 ImGui context、Win32 backend、DX11 backend、font atlas 及图片 SRV；presentation state 只拥有 swap chain/RTV 和可见/occluded/resize 状态。

`Initialize()` 注册客户端后发布初始化请求，并用已有同步栅栏等待首个渲染回调完成 resident 初始化。Hide 只隐藏 HWND、清除 continuous 位并销毁 presentation state。Show 显示/聚焦 HWND、请求 Setting，并在渲染线程创建 presentation state。epoch 变化先销毁旧 presentation、图片和 DX11 backend，再用新共享设备重建 backend/图片；隐藏时在此结束，显示时继续创建 swap chain/RTV。Shutdown 同步在渲染线程逆序清理全部资源，再注销客户端。

## Window Contract

HWND 延续 Window Service 单次创建。窗口使用可缩放顶层样式和 client-area 标题栏；WndProc 负责 `WM_NCCALCSIZE`、`WM_NCHITTEST`、`WM_GETMINMAXINFO`、`WM_DPICHANGED`、主题通知及标准系统命令。命中测试优先返回缩放边角，再排除标题栏按钮/交互区域，最后返回 `HTCAPTION`。最大化时使用 monitor work area，保留 Snap 和系统快捷键。

## DPI, Theme And Layout

唯一逻辑尺寸单位为 DIP。系统 DPI 与 Setting 全局倍率计算 effective scale，并写入 ImGui `FontScaleDpi`/对应字体构建；控件代码不再次乘自定义倍率。主题由 Windows AppsUseLightTheme、高对比状态和相关消息驱动 ImFluent preset、ImGui style、clear color 与标题栏调色板。

宽/中模式使用 ImFluent NavigationView 的 LeftOpen/LeftCompact；窄模式以 ImFluent CompactOverlay SplitView 承载同一导航模型。内容页使用滚动 child、最大可读宽度与 wrap layout。项目适配层仅负责响应式 settings row、自动换行文案、图片卡片和标题栏按钮。

## Migration And Compatibility

页面路由、状态、业务 action 和 FIFO worker 保持原实现，按页面逐段把旧伪 Fluent 外观替换为 ImFluent。主页单独重写。ImFluent 输出仍是标准 ImDrawData，因此现有 CSO backend 无接口变化。ImFluent 依赖 `imgui_internal.h`，固定 commit 与 ImGui 1.92.7 作为成对升级单元。

若集成出现阻断，可先保留页面业务渲染函数并仅启用 ImFluent 主题/导航适配；不得回退 resident lifecycle 或窗口合同。第三方升级通过替换上游快照、重新应用两处标记补丁、ARM64 严格编译与完整 UI 验收完成。
