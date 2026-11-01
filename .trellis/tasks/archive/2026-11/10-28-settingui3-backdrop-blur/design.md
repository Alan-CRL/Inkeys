# 设计：统一 Win10/11 动态背景模糊

## 现状

`ApplySettingBackdrop`（`Inkeys/Inkeys/UI/Setting/Setting.cpp`）按三级联尝试：

1. Win11 `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW`，成功发布 `BackdropMode::Mica`。
2. 旧 Win11 Mica 属性 1029，成功同样发布 Mica。
3. 动态解析 `SetWindowCompositionAttribute`，accent policy 为 `AcrylicBlurBehind`（拖动/缩放中切 `BlurBehind`），成功发布 `BackdropMode::Acrylic`。

Windows 11 在第 1 步就返回成功，所以永远到不了第 3 步；而第 1 步的效果是与壁纸的静态合成，并且在本窗口上实际看不到。Windows 10 没有这两个属性，落在第 3 步，效果正常。

能力门控在 `Inkeys/Inkeys/Window/Window.cppm` 的 `ResolveSettingPlatformCapabilities`：`acrylic = major > 10 || (major == 10 && build >= 17134)`。Win7/8/8.1 与早期 Win10 已在此处被排除，直接 Solid。

## 方案

### 背景应用只保留 accent 动态模糊

- `ApplySettingBackdrop` 删除两个 Mica 分支，只保留：能力门控（`acrylic` 标志 + composition 开启 + 非高对比度）通过后调用 `SetSettingAcrylic`，成功返回 `BackdropMode::Acrylic`，否则 Solid。
- 撤销函数 `DisableSettingBackdrop` 同步删除 `DWMWA_SYSTEMBACKDROP_TYPE` 与 legacy Mica 属性的设置，只保留 accent 撤销、`DWMWA_REDIRECTIONBITMAP_ALPHA` 复位与 frame margins 复位。
- `DwmwaLegacyMicaEffect` 常量随之删除。
- 拖动/缩放期间的 `BlurBehind` 切换（`ResolveSettingAccentState` + `SetSettingInteractiveOperation`）原样保留，它正是性能约束所在：Acrylic 状态会在每次重绘时重采样并重模糊，交互期间切到普通模糊避免拖动卡顿。

### 枚举与解析函数

- `BackdropMode` 删除 `Mica`，保留 `Solid`、`Acrylic`。`Acrylic` 在本项目中的含义就是“accent 动态模糊”，不改名，避免牵动调色板与缓存 key 的既有语义。
- `ResolveBackdropMode` 删除 `systemBackdropEnabled`、`legacyMicaEnabled` 参数，改为 `ResolveBackdropMode(bool acrylicEnabled)`：true 返回 Acrylic，否则 Solid。
- 所有按 `BackdropMode::Mica` 分支的调用点（`Setting.Pages.cpp` 测试夹具、`setting_design_tests.cpp`、`setting_home_warp_tests.cpp`、`setting_session_state_tests.cpp`）改为只区分 Solid 与 Acrylic。材质调色板对非 Solid 的处理本来就相同，不产生视觉差异。

### 能力门控不变

- `ResolveSettingPlatformCapabilities` 的版本判定保持原样：Win10 build >= 17134 与 Win11 为 `acrylic = true`，其余为 false。Win10 以下因此天然禁用，无需新增回退代码。
- `IsSettingCompositionEnabled` 与高对比度检查保持原样。

## 边界与兼容

- 透明清屏、根背景 alpha、自绘标题栏不透明填充均由“backdrop != Solid”门控，不依赖 Mica/Acrylic 的区分，删除 Mica 后行为不变。
- 主页入场缓存 key 中的 backdrop 字段是整数快照，枚举值变化只影响缓存命中，不影响正确性；同一次运行内枚举稳定。
- 不触碰 swap chain（`DXGI_SWAP_EFFECT_DISCARD` 的传统 `CreateSwapChain`）、不引入 DirectComposition、不修改 ImFluent vendor，与现有规范约束一致。
- 设置窗口以外的窗口（悬浮栏、白板、消息框）不使用这套 backdrop，不受影响。

## 模糊层对齐：左/右/下零非客户区，外侧热区交给透明边框窗口

### 原因

accent 模糊覆盖整个窗口矩形。恢复态下 `WM_NCCALCSIZE` 只把顶部内缩到 0，左/右/下保留约 8 px 的不可见 sizing frame，所以这三侧的模糊比可见内容大一圈。Win11 换到 accent 后会出现同样的问题。详见 `research/current-backdrop-analysis.md`。

### 约束

缩放热区不能整体移进客户区，那会挡住滚动条。用户 2026-10-29 确认：accent 平台恢复态下，左/右/下非客户区设为 0，让模糊与可见窗口对齐；缩放热区只放在窗外，由边框窗口提供，宽度与现在的 sizing frame 相同（约 8 px），保持现在左/右/下的手感，也与 Win10/11 原生窗口一致；阴影必须保留。

> 修订记录：初版方案 D 保留了约 8 px 的非客户区环带，但 accent 模糊覆盖的窗口矩形本身就包含这圈环带，所以溢出没有修好，左/右/下热区也变成 16 px。实现阶段发现，已按下文修订。

### 方案（方案 D 修订：左/右/下零非客户区 + 窗外边框窗口）

- 几何：accent 平台（`acrylic` 能力为 true 且 composition 开启）恢复态下，`QuerySettingClientFrameInsets` 的左/右/下为 0；顶部维持现状（Win10 为 0，Win11 为 DWM 可见边框 1 px）。最大化态保持完整内缩。Win7/8/8.1 与早期 Win10 保持原样。窗口创建时的尺寸外扩（`Window.cpp` 创建 Setting 时调用的同一函数）随之一致。
- 判定只看平台能力，不看 backdrop 当前是否成功。高对比度或 accent 调用失败时几何不变，也避免在 `WM_NCCALCSIZE` 里读跨线程状态。几何变化时用 `SWP_FRAMECHANGED` 重算，沿用现有 `WM_DWMCOMPOSITIONCHANGED` 路径。
- 主窗口自身的 `WM_NCHITTEST`：accent 平台恢复态下不再返回缩放命中，包括原来标题栏空白处的顶部内侧带（`topResizePixels` 为 0），热区只在窗外。标题栏拖动、按钮与客户区行为不变。
- 边框窗口：矩形比主窗口四周各大一个 sizing frame 宽度，挖空主窗口矩形（Win11 为圆角矩形）。它用 `WS_POPUP | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，`SetLayeredWindowAttributes` 设 alpha 1/255（alpha 0 的分层窗口收不到鼠标），不创建 swap chain、不做任何绘制。
- 命中测试：边框窗口的 `WM_NCHITTEST` 交给 `HitTestWindowFrame` 的外侧扩展参数，返回边/角命中；按下时先激活主窗口，再向主窗口发送 `WM_SYSCOMMAND`（`SC_SIZE` + 方向），缩放循环由系统在主窗口上执行。
- 四边因此一致：热区都在可见边缘外侧约 8 px，顶部不再需要把鼠标伸进窗口。
- 阴影：Win10 上零非客户区窗口需要非零 frame margin 才保留 DWM 阴影。accent 平台恢复态下设置 1 px frame margin（优先只设顶部 1 px）。这个 margin 属于框架几何，不属于背景材质：`DisableSettingBackdrop` 不能把它清掉，背景应用/撤销、composition 变化、最大化/还原后都要重新确认。最大化与非 accent 平台保持 margin 为 0。margin 是否产生可见亮线、阴影是否完整需实机确认。
- 跟随：主窗口处理 `WM_WINDOWPOSCHANGED`、`WM_SHOWWINDOW`、`WM_DPICHANGED` 时重算边框窗口的位置与尺寸；最小化与隐藏时隐藏它；`WM_DESTROY` 时销毁它。边框宽度沿用 `QuerySettingFrameThickness`，随 DPI 变化。
- Win11 圆角：边框窗口用 `CreateRoundRectRgn` 组合成只含四条边带与外角的区域，挖掉圆角内侧，避免直角模糊块露在圆角外面。圆角半径实机确认。Win10 无圆角，用矩形边带即可。
- 归属：边框窗口不进入 `Window::Service`，由 `ImGuiWndProc` 在主窗口线程上创建与销毁。owner 设为主窗口，使 `Service` 的 z-order 快照把它当作允许穿插的辅助窗。`WS_EX_TOOLWINDOW` 让它不进任务栏与 Alt-Tab。
- 旧系统：Win7/8/8.1 不启用 accent，不创建边框窗口，框架几何保持原样。

### 不采用的方案

- 保留非客户区环带：模糊仍覆盖环带，溢出不消失（初版方案 D 的错误）。
- 热区移到客户区内侧：挡住滚动条，与用户要求相反。
- `SetWindowRgn` 裁剪主窗口：会关闭 DWM 阴影与 Win11 圆角，region 窗口合成更慢。
- accent flags 加边框：只能画边线，不能缩小模糊区域。

## 回滚

改动集中在 `Setting.cpp` 的 backdrop 应用/撤销与边框窗口、`Setting.Theme.h` 的枚举与解析函数、`Setting.Layout.h` 的命中测试、对应测试与规范段落。回滚即恢复三级联与 `BackdropMode::Mica` 并去掉边框窗口，无数据迁移、无配置格式变化。
