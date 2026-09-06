# Setting Fluent2 响应式界面迁移

## Goal

将当前 Dear ImGui 设置窗口迁移为基于 ImFluent 的现代 Fluent2 响应式界面，并调整生命周期，使 ImGui、backend、字体和图片在 Initialize 后常驻，Show/Hide 只控制窗口可见性、连续绘制及 presentation resources，从而获得即时打开、平滑动画和稳定的共享 WARP 渲染体验。

## Background

- 前置任务 `08-13-unified-render-pipeline-setting` 已将 Setting 接入共享 D3D11.1 WARP device、唯一串行渲染线程和独立 swap chain/RTV。
- ImFluent 固定采用 `lukaasm/ImFluent@fe7cf3ef784afc81aed55f24f39fcaa15cdf96cb`；已验证可在 ARM64、C++20、Dear ImGui 1.92.7 下编译。
- 当前 Setting HWND 已由 Window Service 在进程生命周期内创建一次；普通关闭应继续映射到 Hide。

## Requirements

- R1：将 ImFluent 源码快照 vendoring 到 `Inkeys/additional/imfluent/`，保存 MIT 许可、固定 commit、来源和升级说明，不使用 submodule 或构建期下载。
- R2：ImFluent 本地补丁仅公开项目字体绑定接口并提供全局 context 重置接口；不修改控件绘制算法，不调用系统字体加载。
- R3：保持 Setting 公开 `Initialize/Shutdown/Show/Hide/Toggle/IsVisible/WindowProc` 接口兼容。
- R4：`Initialize()` 在注册客户端后同步等待渲染线程建立 ImGui context、Win32/DX11 backend、字体图集和图片 SRV；失败时完整回滚。
- R5：隐藏时停止连续绘制并释放 swap chain/RTV，但保留 ImGui context、backend、字体和图片；显示时恢复 presentation resources 并立即绘制。
- R6：device epoch 改变时，即使隐藏也立即重建 DX11 backend 和 device-dependent 图片；仅在可见时重建 swap chain/RTV。`Shutdown()` 才最终释放常驻资源。
- R7：继续使用共享 D3D11.1 WARP、内嵌预编译 CSO 和现有 Win32/DX11 backend，不引入运行时 shader 编译。
- R8：窗口使用自绘 Fluent 标题栏，同时保留原生拖动、八方向缩放、最小化、最大化、还原、系统菜单、Snap 和 Win+方向键行为。
- R9：默认窗口为 `960×700 DIP`，最小客户区约 `720×520 DIP`；进程内 Hide/Show 保留窗口状态，进程重启不持久化位置和尺寸。
- R10：DPI 使用 `systemDpiScale × settingGlobalScale`，自定义倍率统一为 `1.0–2.0`；系统 DPI 或倍率变化才重建字体，普通 resize 只重排和 resize buffers。
- R11：为优先完成浅色视觉修正，Setting 运行时暂时固定使用 ImFluent Light preset，不跟随 Windows 暗色或高对比主题；保留既有主题消息刷新边界，不新增持久化主题配置，后续恢复动态主题另立任务。
- R12：导航按逻辑 DIP 响应：`>=900` LeftOpen、`760–899` LeftCompact、`<760` CompactOverlay；所有页面共享同一导航数据源。
- R13：主页完全替换星空/视差，提供作者/软件概览、外部链接和固定宽高比教程图片预留区。
- R14：迁移所有现有页面的视觉控件，同时保留业务状态、配置持久化、异步 FIFO 和运行时同步行为。
- R15：使用 ImFluent 内建动画并增加约 160ms 页面进入动画，不在每帧加载资源；禁用动画优化另行处理。
- R16：本轮支持预热静态图片的轻量展示接口；GIF、视频和完整快捷键编辑增强仅保留扩展边界，不引入媒体依赖。
- R17：保留工作区现有 `Inkeys/PptCOM.dll` 及其他线程改动，不回退无关文件。
- R18：设置窗口壳层必须采用 ImFluent Demo 的标准组合：NavigationView pane 与 NavigationView content 成对使用，保留库自带的 pane toggle、选择指示器和展开动画；窄屏继续使用 CompactOverlay。
- R19：页面中的开关、下拉框、按钮、滑块、设置行、信息提示和分组优先直接使用 ImFluent 原生控件；兼容包装不得继续自行绘制或用原生 ImGui 控件仿制同名 Fluent 控件。
- R20：移除跨页面常驻的“更新状态 / 检查更新”底栏；更新状态和操作只在软件版本页面内呈现。
- R21：自绘标题栏不以 DWM composition 为启用条件；DWM 仅提供可选边框、阴影和圆角，关闭 Aero 或属性不受支持时仍保持同一套 Inkeys caption 视觉和命中。
- R22：caption buttons 使用固定 46 DIP 单元格和独立 10 DIP Fluent glyph；按下/释放由 Win32 non-client 消息跟踪并投递标准 `WM_SYSCOMMAND`，关闭继续映射到 Hide。
- R23：拖动窗口时不得在每次 `WM_NCMOUSEMOVE(HTCAPTION)` 唤醒 Settings 渲染；最大化/还原和 frame refresh 期间不得短暂露出系统蓝色标题栏或第二套 caption。
- R24：Setting 背景按运行时能力优先启用 Win11 DWM Mica，旧 Win11 使用 legacy Mica 属性，Win10 使用动态探测的 Acrylic 等价透明材质；DWM 关闭、API 缺失或调用失败时无错误回退到现有浅色实色背景，Win7 不新增硬依赖。

## Acceptance Criteria

- [ ] Initialize 返回前，常驻 ImGui/backend/font/image 资源已可用，Show 不执行字体或图片磁盘加载。
- [ ] Hide 后不再提交连续帧，仅释放 swap chain/RTV；再次 Show 保留会话和窗口状态并立即呈现。
- [ ] 隐藏或显示期间的 device epoch 改变均正确重建 resident device resources，无持续内存增长。
- [ ] 所有原设置页面均可访问，业务交互、保存、异步操作和即时同步行为保持不变。
- [ ] 主页、导航、控件和标题栏符合 Fluent2/WinUI3 视觉方向，窗口在宽、中、窄三档无重叠或裁切。
- [ ] 拖动、自由缩放、双击标题栏、最小化、最大化、系统菜单、Snap、Win+方向键和多 DPI 正常；切换 Windows 明暗/高对比后 Setting 仍保持浅色 preset。
- [ ] ARM64 Debug 完整 Solution 构建、headless tests 和 `git diff --check` 通过；真实窗口完成动态检查。
- [ ] 可见空闲帧时间相对同等旧页面无超过约 10% 的持续回退，打开/缩放/切页无明显卡顿或资源增长。
- [ ] 导航、页面标题、设置卡片、ToggleSwitch、ComboBox、Button 和 Slider 的视觉及动画与固定 ImFluent Demo 的 Fluent2 语言一致，不再混用旧伪 Fluent 控件。
- [ ] 标题栏拖动无明显卡顿；最大化/还原过程中顶边不闪现原生蓝色标题栏，且 resize、Snap、系统菜单和 caption commands 保持正常。
- [ ] 支持的 Win11/Win10 环境显示 DWM 背景材质；禁用 composition、属性不受支持或 Win7 环境保持可读的浅色实色背景，启动、显隐和主题消息不报错。

## Out Of Scope

- GIF、视频解码与播放。
- 动画禁用和低功耗专门优化。
- 新的主题或窗口位置持久化配置。
- 暗色、高对比样式适配，以及恢复跟随 Windows 主题。
- 恢复旧 IdtFloating 或替换共享 WARP/CSO 渲染路径。
