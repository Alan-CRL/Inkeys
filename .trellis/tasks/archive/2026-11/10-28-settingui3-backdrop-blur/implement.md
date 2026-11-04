# 执行计划：统一 Win10/11 动态背景模糊

按顺序执行，每步完成后可独立编译验证。

## 1. 背景应用与撤销

- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：`ApplySettingBackdrop` 删除 `DWMSBT_MAINWINDOW` 与 legacy Mica（属性 1029）两个分支，只保留能力门控后的 `SetSettingAcrylic`，成功返回 `BackdropMode::Acrylic`。
- 同文件 `DisableSettingBackdrop` 删除 `DWMWA_SYSTEMBACKDROP_TYPE` 与 legacy Mica 的复位调用，保留 accent 撤销、`DWMWA_REDIRECTIONBITMAP_ALPHA` 与 frame margins 复位。
- 删除 `DwmwaLegacyMicaEffect` 常量；确认无其他引用。
- 保留 `ResolveSettingAccentState` 的交互期 `BlurBehind` 切换，不改其逻辑。

## 2. 枚举与解析

- `Inkeys/Inkeys/UI/Setting/Setting.Theme.h`：`BackdropMode` 删除 `Mica`；`ResolveBackdropMode` 改为单参数 `bool acrylicEnabled`。
- 全库搜索 `BackdropMode::Mica`、`ResolveBackdropMode(` 的调用并更新：`Setting.Pages.cpp`、测试文件。材质调色逻辑只区分 Solid 与非 Solid，确认无需新增分支。

## 2b. 模糊层对齐与边框外缩放

- `Inkeys/Inkeys/UI/Setting/Setting.Layout.h`：`HitTestWindowFrame` 增加外侧扩展宽度参数。恢复态下，窗口矩形之外但在扩展范围之内返回对应的边/角命中；客户区与标题栏按钮保持优先；最大化不返回缩放命中。抽出一个纯函数，根据主窗口矩形、边框宽度与圆角半径计算边框窗口的矩形与区域，供测试直接验证。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：新增边框窗口过程。仅当平台 `acrylic` 能力为 true 且 composition 开启时，在主窗口线程创建 `WS_POPUP | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` 的窗口，`SetLayeredWindowAttributes` 设 `LWA_ALPHA = 0`，owner 为主窗口。它不进 `Window::Service`，不创建 swap chain。
- 边框窗口的 `WM_NCHITTEST` 把屏幕坐标交给 `HitTestWindowFrame`；`WM_NCLBUTTONDOWN` 先激活主窗口，再向主窗口发送 `WM_SYSCOMMAND`（`SC_SIZE` + 方向）。
- 主窗口在 `WM_WINDOWPOSCHANGED`、`WM_SHOWWINDOW`、`WM_DPICHANGED` 时同步边框窗口的位置、尺寸与区域；最小化与隐藏时隐藏它；`WM_DESTROY` 时销毁它。边框宽度用 `QuerySettingFrameThickness`。
- Win11 上边框窗口用 `CreateRoundRectRgn` 组合出只含边带与外角的区域，挖掉圆角内侧；Win10 用矩形边带。圆角半径实机确认。
- 修订（2026-10-29）：accent 平台恢复态下 `ResolveVisibleWindowFrameInsets` / `QuerySettingClientFrameInsets` 的左/右/下为 0，顶部维持现状；最大化与 Win7/8/8.1 不变。主窗口 `WM_NCHITTEST` 在 accent 平台恢复态不返回缩放命中（`topResizePixels` 为 0）。边框窗口挖空的就是主窗口矩形。
- 阴影：accent 平台恢复态设置 1 px frame margin（优先只设顶部），由框架几何负责，`DisableSettingBackdrop` 不清除；背景应用/撤销、composition 变化、最大化/还原后重新确认。最大化与非 accent 平台 margin 为 0。

## 3. 测试

- `InkeysHeadlessTests/setting_session_state_tests.cpp`：`ResolveBackdropMode` 断言改为“acrylic 开启为 Acrylic，否则 Solid”，删除 Mica 优先级用例；平台能力版本边界用例保持不变。
- `InkeysHeadlessTests/setting_design_tests.cpp`、`setting_home_warp_tests.cpp`：材质循环从 `{Mica, Acrylic, Solid}` 改为 `{Acrylic, Solid}`，断言文案同步。
- `HitTestWindowFrame`：外侧扩展范围内返回四边与四角命中，客户区与标题栏按钮区域不返回缩放命中，最大化不返回缩放命中。
- 边框窗口几何：矩形比主窗口各大一个边框宽度；Win11 圆角区域不覆盖圆角内侧。
- 静态审计：确认 `Setting.cpp` 不再引用 `DWMWA_SYSTEMBACKDROP_TYPE` 与 `DwmwaLegacyMicaEffect`，边框窗口不进入 `Window::Service`。 `DWMWA_SYSTEMBACKDROP_TYPE`、`DWMSBT_`、属性 1029、`DwmEnableBlurBehindWindow` 不再出现在 Setting 活动路径。

## 4. 规范

- `.trellis/spec/native-desktop/rendering-and-ui.md`：背景能力合同（约 1621–1622 行）、验证矩阵（1643–1647 行）、错误示例（1686–1694 行）与材质调色段落（2359 行起）改为“Win10 build >= 17134 与 Win11 使用 accent 动态模糊，其余 Solid；不使用系统 backdrop / Mica，不提供 Aero 回退”。

## 验证命令

- 构建并运行 headless：`InkeysHeadlessTests.exe --no-window`（设置相关用例：session state、design、home warp）。
- 完整 `Debug|ARM64` solution 构建，确认无新警告。
- GUI 实机验证需用户授权，且本机为 Windows 11：打开设置窗口，确认背景随下层窗口实时变化；四边模糊与窗口对齐、圆角正常、阴影存在。鼠标在四边与四角的边框内外两侧都能拖动缩放，顶部不再需要把鼠标伸进窗口，窗口内侧的滚动条与内容不受影响；最大化、还原与 Snap 正常。拖动设置窗口时不卡顿；关闭后重开正常。Win10 实机重点确认三侧不再溢出、直角。Win11 实机重点确认圆角外侧没有直角模糊块。Windows 10 与 Windows 7 的实机确认另行安排，代码层面以版本门控与测试覆盖。

## 审查点

- 步骤 2 完成后全库无 `BackdropMode::Mica` 残留引用再进入测试。
- 提交前核对规范段落与代码一致。

## 回滚点

- 每步独立提交前可整体回退到本分支当前提交；无配置或数据格式变化。
