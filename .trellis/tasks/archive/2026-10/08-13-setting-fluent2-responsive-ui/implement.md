# Implementation Plan

1. 审计 Setting session 状态机、Window Service 窗口合同、DPI/字体/图片入口、页面路由、项目项和 headless tests，记录所有权与现有业务边界。
2. 导入固定 ImFluent 源码、许可和升级说明，应用两处最小补丁并登记 vcxproj/filters；先做 ARM64 编译验证。
3. 将 Setting session 拆分为 resident 与 presentation 生命周期，实现同步 Initialize、Hide/Show、epoch rebuild 和 Shutdown 合同，更新纯状态测试。
4. 恢复原生可缩放窗口样式与自绘 Fluent 标题栏，实现非客户区命中、最小尺寸、DPI 和主题处理，并扩展窗口测试。
5. 建立 DPI/theme/responsive 纯函数和 ImFluent 项目适配层；当前 theme 解析暂固定为 Light，绑定内嵌字体和静态图片预热接口。
6. 以统一导航模型迁移全部页面控件，先替换主页，再逐页保持原业务回调、配置写入和 FIFO 行为；增加页面进入动画。
7. 执行 headless tests、ARM64 Debug 完整 Solution 构建、静态引用审计和 `git diff --check`。
8. 启动真实 ARM64 应用，动态检查窗口状态、Snap、DPI、主题、所有页面、动画、重复 Hide/Show 和 device epoch；记录无法自动化的观察。
9. 使用 `trellis-check` 复核规范、行为、测试与第三方许可，修复范围内问题并更新 native desktop spec。
10. 按固定上游 Demo 重构 NavigationView content 壳层和导航分组，恢复 pane toggle/展开动画；删除跨页面更新底栏。
11. 将旧 Combo/Button/Toggle/Slider 兼容层收敛到 ImFluent 原生控件，并以 SettingsCard/标准文字层级逐页替换旧绝对坐标卡片。
12. 固化 client-drawn chrome：DWM 仅作可选增强，caption 使用 46 DIP cell/10 DIP glyph，并由 WndProc 跟踪按钮按下与标准系统命令。
13. 复查系统 move/maximize 消息链，消除拖动期间渲染唤醒和最大化瞬间的原生蓝色标题栏暴露；只用静态审计、完整 ARM64 Solution 构建和 `--no-window` 测试验证自动化范围。
14. 将 Setting frame 收敛为 `WS_THICKFRAME` + client caption：普通态只保留 1px 可见 non-client frame，resize hit 由系统 DPI frame metrics 推导，最大化 geometry 交回默认过程；将 modal loop 状态拆为 Move/Size，保持 Move 暂停优化并恢复 live resize。
15. 在浅色视觉调整阶段将 Setting 运行时主题固定为 ImFluent Light；保留主题消息刷新入口，用纯函数 headless 测试锁定系统暗色/高对比输入也不改变结果。
16. 增加 Setting DWM 背景材质能力级联：Win11 system backdrop、旧 Win11 Mica、Win10 动态 Acrylic，失败或 Win7 回退 Solid；仅成功路径开启透明根背景/clear，并覆盖 composition 变化后的重新探测。

## Risk And Rollback Points

- 导入 ImFluent 后先独立编译，避免把第三方兼容错误与页面迁移错误混在一起。
- 生命周期改造先用纯状态测试锁定 resident/presentation 边界，再迁移视觉层。
- 页面迁移按路由分段，每段都保留旧业务 action；不得为视觉统一重写配置或 worker。
- 本地 ImFluent 补丁只允许公开字体绑定和 reset；任何额外库问题优先在项目适配层解决。

## Verification Commands

```powershell
# 使用 ARM64 Host MSBuild，绝对路径在执行前只读定位
MSBuild.exe InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
<headless-test-path> --no-window
git diff --check
rg -n "LoadFluentSystemFonts|D3DCompile|D3DCompileFromFile" Inkeys
```

## Verification Record (2026-08-14)

- 完整 ARM64 Host MSBuild：`InkeysRepo.sln Debug|ARM64` 通过，`0 errors`；保留旧 Setting 页面数值转换警告。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 通过，输出 `PASS animation correctness`。
- `git diff --check` 无输出；ImFluent 工程登记、MIT License、UPSTREAM/commit/升级说明、两处 vendor `INKEYS PATCH` 和项目适配层的 ImGui `1.92.7` 编译期门禁已复核。
- 静态审计确认产品路径未调用 `LoadFluentSystemFonts()`，未新增运行时 `D3DCompile*`；ImFluent 继续输出标准 `ImDrawData`。
- Fluent2 页面静态复审通过：业务页已使用 ImFluent 原生控件组合，导航/content、CompactOverlay SplitView、Card/SettingsCard 及布局容器调用成对；旧 `imgui_toggle` 已从产品工程和调用路径移除，更新状态仅保留在版本页。
- 真实 ARM64 进程确认 Setting HWND 初始化后即存在且隐藏；style 为 `0x860F0000`，包含 thickframe/minimize/maximize/system-menu。
- 系统 DPI `200%`、用户倍率 `1.5` 下实测 13 类 `WM_NCHITTEST` 均正确；发现默认高度超过工作区后，已增加默认/最小尺寸工作区夹紧并补 headless 断言。重建后初始 bounds 为工作区 `0,48 2880x1776`。
- 真实 HWND 的最大化、还原、最小化和 `SC_CLOSE` 已验证；关闭后 HWND 继续存在且变为隐藏。
- 当前测试环境的 Bar 分层表面未发布可命中首帧，无法从真实业务入口调用 `Setting::Show()`；外部 `ShowWindow` 不等价于业务 Show，未将其冒充呈现生命周期验收。
- 仍待人工验收：可见页面的标题栏直接拖动、八方向实际拉伸、Snap/Win+方向键、窄/中/宽页面导航、CompactOverlay、所有业务页面、动画、固定浅色下的 Windows 主题/高对比切换、多显示器 DPI、重复 Hide/Show、device epoch 实机恢复，以及帧时间/内存/Show 延迟指标。
- 后续 client-drawn chrome 基线：标题栏不再依赖 DWM 是否开启；caption cell 固定 46 DIP、glyph 固定 10 DIP，应用自行跟踪 non-client button press/release；`HTCAPTION` 的 `WM_NCMOUSEMOVE` 不请求渲染。
- 步骤 14 取代了旧 full-client 方案：Setting style 为无 `WS_CAPTION` 的 popup + thickframe；普通态 `WM_NCCALCSIZE` 留出 1px non-client frame，八方向 resize hit 使用 `AdjustWindowRectExForDpi` 对应的系统 frame metrics，最大化态 `WM_NCCALCSIZE` 与 geometry 交回默认过程。
- Move/Size 已拆分：Move 继续暂停 Settings Present；Size 中 `WM_SIZE -> QueueResize -> Request -> ResizeSwapChain -> Render/Present` 持续运行。ARM64 Host `InkeysRepo.sln Debug|ARM64` 构建通过，`0 errors`；`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 输出 `PASS animation correctness`。本轮未启动 GUI，真实八方向 live resize、Snap Layout、active/accent border、最大化与多显示器 DPI 仍待人工验收。
- 2026-08-16 浅色样式阶段：`ResolveThemeMode` 对 Windows 浅色、暗色和高对比输入均返回 Light；不新增主题配置，动态暗色/高对比适配留待后续任务。ARM64 Host 完整 Solution 构建通过（`0 errors`），`InkeysHeadlessTests.exe --no-window` 输出 `PASS animation correctness`，`git diff --check` 无输出。
- 2026-08-16 背景材质阶段：新增 Mica -> legacy Mica -> Acrylic -> Solid 运行时级联；Windows 10 专有入口只通过 `GetProcAddress` 使用，Win7/API 缺失/调用失败不作为初始化错误。ARM64 Host 完整 Solution 构建通过（`0 errors`），`InkeysHeadlessTests.exe --no-window` 输出 `PASS animation correctness`，`git diff --check` 无输出；真实材质、composition 切换和透明 clear 仍待允许 GUI 的环境验收。
