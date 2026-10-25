# 窗口框架与深色主题阶段（2026-09-07 用户已批准）

基线 `89770357`。用户要求继续前阶段测试，明确允许 Computer Use，随后直接开始窗口样式和深色主题修改，边实现边验证；完成后暂存。此授权取代早先对窗口/深色与 Computer Use 的排除，不需要再次索取实施批准。没有要求自动 commit/push，本阶段结束使用 git add 暂存。

## 当前目标

1. 补完既有页面滚动、插件详情、支持/调试和窄窗口检查。用 Computer Use 的 Windows.Graphics.Capture/目标窗口输入，避免把 PrintWindow 旧帧误判为 UI 冻结。
2. 最大化使用当前显示器工作区，保留任务栏；还原、拖动、八方向缩放、系统菜单与原生窗口语义正常，不变成全屏。
3. 标题栏颜色与内容主题一致，不透出系统强调色蓝条；浅色与深色 caption/glyph/hover/非客户区边界一致。
4. 缩放热区与内容区分离，尤其右侧垂直滚动条整个可操作区必须属于 client；尽量采用真实系统 sizing frame/非客户区边界，实现像 WinUI 的窗口外缘调整，而不是从内容区拿出一大条 resize 区。最大化时禁用边缘 resize hit。
5. 设置窗口全部内容支持深色主题：背景、卡片、文字、说明、输入/选择/展开控件、滚动条、Notice、首页插画等一起切换。增加稳定可达的即时切换按钮，切换不重启、不重建字体或设备。

## 实施选择

- 沿用 Win32 HWND + ImGui/ImFluent + 共享 D3D11/CSO/resident/epoch 链，窗口修正限 Setting 特例，不改变 Bar/PPT 窗口合同。
- 优先标准可调整大小的顶层窗口语义（必要时恢复 WS_OVERLAPPEDWINDOW/WS_CAPTION 的系统行为），由自绘 caption 替换其标题内容；NCCALCSIZE/GETMINMAXINFO/NCHITTEST 采用同一 geometry 合同，不能只在某一条消息里补坐标。
- 主题按钮放在自绘标题栏右侧、caption buttons 左侧的独立 client 命中区，宽窗/收起/窄窗均可访问。与窗口拖动区互斥。
- 主题默认浅色，保持旧配置初始观感；用户切换即时生效并随设置保存。布尔偏好 `settingDarkMode` / `SettingDarkMode` 跟随已有设置窗口倍率/语言所在的 SetListStruct + opt/deploy.json，缺项默认 false；不迁移其他配置系统、不改系统主题。I18n 更新按现有源/生成流程。
- GUI 切换由渲染线程消费并应用 ImFluent preset 与项目 token，保存通过原 FIFO；不在 WndProc 跨线程改全局样式。主题换色不承担资源加载。
- 保留可选 Mica/Acrylic 能力边界；必要时修其与 caption 的透明/填色关系，不新增材质引擎或 renderer。
- 不扩展动画、触摸、动态脏区、视频等后续功能。

## 改动边界与所有权

- Frame 实施：Setting.cpp 的 Win32/标题栏及最终 theme glue，Setting.Layout.h/必要纯geometry头，Setting 专用 Window.cpp/.cppm 创建合同；因为当前1px NC与系统厚度resize内侵，需关联修正。
- Theme 实施：Theme/Design/Controls/Pages/Shell 的颜色和主题API、实时切换所需UI模型；SetListStruct/JSON读写/默认/i18n负责保存用户选择。与Frame代理明确Setting.cpp接线接口，禁止同文件同时覆盖。
- Tests：无窗口几何/主题/控件回归及既有窗口测试适当修正，主会话统一完整Solution构建和真实窗口检查。

## 验收

- 当前显示器 work area 最大化/还原正确；任务栏可见，双击标题/按钮/系统命令一致；不同监视器原点与 DPI 的最小/命中几何、USER32 最大化字段保留由无窗口用例覆盖；系统实际放置仍需 GUI。
- 普通窗口在真正frame上拖动可resize，右侧scrollbar可拖动/点击且窗口尺寸不变，最大化边缘不返回HTRIGHT等resize结果。
- 主题按钮不触发窗口拖动；切换立即更新全页/弹出控件/标题，文本与图标可读；重开软件读取原选择，旧配置仍浅色。
- 主题切换不写盘每帧、不重建atlas/device；所有原业务字段/动作不漂移。
- ARM64 host 完整 InkeysRepo.sln Debug|ARM64，超时至少5分钟；headless、i18n、CRLF/BOM、diff检查通过；实际浅/深、最大化、还原、滚动条拖动截图验证。
- 完成后暂存所有本任务代码、测试、规范与验收记录；不自动commit或push。

## 官方资料

- https://learn.microsoft.com/en-us/windows/win32/dwm/customframe
- https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccalcsize
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-minmaxinfo
- https://devblogs.microsoft.com/oldnewthing/20150501-00/?p=44964
- https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features

这些资料说明原生frame/client、默认最大化及 MINMAXINFO 的边界，不代表所有自定义组合自动正确；必须验证本应用实际消息顺序与像素几何。

最终最大化接线保留 USER32 预填的 ptMaxSize/ptMaxPosition，只限制 ptMinTrackSize。标准 overlapped style 负责工作区/任务栏语义；直接回填目标显示器尺寸可能触发系统二次补偿，不能用 Inset(Expand(workarea)) 的纯算术替代该消息协议。
