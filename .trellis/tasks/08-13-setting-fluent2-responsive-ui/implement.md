# Implementation Plan

1. 审计 Setting session 状态机、Window Service 窗口合同、DPI/字体/图片入口、页面路由、项目项和 headless tests，记录所有权与现有业务边界。
2. 导入固定 ImFluent 源码、许可和升级说明，应用两处最小补丁并登记 vcxproj/filters；先做 ARM64 编译验证。
3. 将 Setting session 拆分为 resident 与 presentation 生命周期，实现同步 Initialize、Hide/Show、epoch rebuild 和 Shutdown 合同，更新纯状态测试。
4. 恢复原生可缩放窗口样式与自绘 Fluent 标题栏，实现非客户区命中、最小尺寸、DPI 和主题处理，并扩展窗口测试。
5. 建立 DPI/theme/responsive 纯函数和 ImFluent 项目适配层；绑定内嵌字体和静态图片预热接口。
6. 以统一导航模型迁移全部页面控件，先替换主页，再逐页保持原业务回调、配置写入和 FIFO 行为；增加页面进入动画。
7. 执行 headless tests、ARM64 Debug 完整 Solution 构建、静态引用审计和 `git diff --check`。
8. 启动真实 ARM64 应用，动态检查窗口状态、Snap、DPI、主题、所有页面、动画、重复 Hide/Show 和 device epoch；记录无法自动化的观察。
9. 使用 `trellis-check` 复核规范、行为、测试与第三方许可，修复范围内问题并更新 native desktop spec。
10. 按固定上游 Demo 重构 NavigationView content 壳层和导航分组，恢复 pane toggle/展开动画；删除跨页面更新底栏。
11. 将旧 Combo/Button/Toggle/Slider 兼容层收敛到 ImFluent 原生控件，并以 SettingsCard/标准文字层级逐页替换旧绝对坐标卡片。

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
- 仍待人工验收：可见页面的标题栏直接拖动、八方向实际拉伸、Snap/Win+方向键、窄/中/宽页面导航、CompactOverlay、所有业务页面、动画、主题/高对比切换、多显示器 DPI、重复 Hide/Show、device epoch 实机恢复，以及帧时间/内存/Show 延迟指标。
