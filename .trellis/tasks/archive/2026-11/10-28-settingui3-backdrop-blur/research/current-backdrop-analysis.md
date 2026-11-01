# 现状调查：Setting 窗口 backdrop 级联

调查日期：2026-10-28，分支 `bugfix/settingui`。只读调查，未改代码。

## 代码位置

| 内容 | 位置 |
| --- | --- |
| 平台能力解析（`acrylic` = Win10 build >= 17134） | `Inkeys/Inkeys/Window/Window.cppm:44-55` |
| 版本查询（`RtlGetVersion`，不依赖清单） | `Inkeys/Inkeys/Window/Window.cpp:4042-4073` |
| Accent policy 结构、动态解析 `SetWindowCompositionAttribute` | `Inkeys/Inkeys/UI/Setting/Setting.cpp:234-283` |
| 移动/缩放时 Acrylic→BlurBehind 切换 | `Setting.cpp:285-294`，`Setting.SessionState.h:150-163` |
| 撤销全部 backdrop | `Setting.cpp:296-311`（`DisableSettingBackdrop`） |
| 级联主函数 | `Setting.cpp:313-361`（`ApplySettingBackdrop`） |
| 主题应用、透明 alpha 门控 | `Setting.cpp:406-450` |
| 透明 clear | `Setting.cpp:2331-2332`，`4359-4362` |
| 退出时撤销 | `Setting.cpp:4458-4460` |
| `BackdropMode {Solid, Mica, Acrylic}` 与 `ResolveBackdropMode` | `Setting.Theme.h:12-33` |
| 材质调色（Mica/Acrylic 共用 alpha 204/230/245/245） | `Setting.Controls.cpp:1545-1551` |
| Swap chain：`DXGI_SWAP_EFFECT_DISCARD`（BitBlt 模型），R8G8B8A8 | `Setting.Base.cppm:79-90` |
| 规范 | `.trellis/spec/native-desktop/rendering-and-ui.md:1621-1622, 1643-1647, 1661, 2359-2374` |
| 测试 | `InkeysHeadlessTests/setting_session_state_tests.cpp:300-310, 371-380, 643-651`；`setting_design_tests.cpp:1350, 5118`；`setting_home_warp_tests.cpp:897, 913-938` |

## 当前级联（`ApplySettingBackdrop`）

1. `DisableSettingBackdrop` 撤销旧状态。
2. 若 DWM composition 关闭或 `platform.acrylic == false`（Win7/8/8.1、Win10 < 17134）→ `Solid`。
3. 高对比度 → `Solid`。
4. 尝试 `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW` + `DwmExtendFrameIntoClientArea(-1)` + `DWMWA_REDIRECTIONBITMAP_ALPHA`；成功 → `Mica`。
5. 失败则尝试旧 Win11 属性 1029（legacy Mica）；成功 → `Mica`。
6. 都失败 → `SetWindowCompositionAttribute(WCA_ACCENT_POLICY, ACCENT_ENABLE_ACRYLICBLURBEHIND)`，GradientColor 深色 `0xCC202020`、浅色 `0xCCF7F7F7`；成功 → `Acrylic`。
7. 全部失败 → `Solid`。

## 三个系统上的实际路径

- **Win10 (>= 17134)**：步骤 4/5 的 DWM 属性不存在，调用失败 → 落到 Acrylic accent。这是唯一可见的路径，与用户反馈一致。
- **Win11**：步骤 4 的 `DwmSetWindowAttribute(DWMSBT_MAINWINDOW)` 返回成功，于是发布 `Mica`，从不走 Acrylic accent。但画面上看不到 Mica。
  推测原因（未在实机验证）：Mica 由 DWM 画在“扩展进客户区的 frame”下方，需要客户区像素真正透明。本窗口用 BitBlt 模型（`DXGI_SWAP_EFFECT_DISCARD`）的 swap chain，加上自定义 `WM_NCCALCSIZE` 去掉非客户区；DWM 对 BitBlt 重定向表面的 alpha 处理与 Mica 期望的合成路径不一致，`DwmSetWindowAttribute` 返回 S_OK 并不等于材质可见。不论根因细节如何，本任务改为 Win11 也走 accent 路径，Mica 分支整体移除，不需要再追这个根因。
- **Win7**：`platform.acrylic == false` → 直接 `Solid`。现在就没有透明效果，代码中也没有 Aero `DwmEnableBlurBehindWindow` 回退。需求“Win10 以下禁用、无 Aero 回退”在 Win7/8/8.1 上已经成立，只需保持并在规范中写明。

## 改动面（供 design 使用）

- `ApplySettingBackdrop`：删除步骤 4、5，只保留 accent 路径。
- `DisableSettingBackdrop`：Mica 属性撤销是否保留需决定（见 design；建议保留 `DWMSBT_NONE` 撤销一次，防止旧版本残留属性，属性在 Win10 上调用失败无害）。
- `BackdropMode::Mica`、`ResolveBackdropMode` 的 Mica 参数、调色/缓存测试中的 Mica 分支：随之移除或收敛。
- `ResolveSettingPlatformCapabilities` 的 `acrylic` 判定：Win11（build >= 22000）已包含在 `build >= 17134` 中，无需改动，除非决定扩大 Win10 下限（见 PRD 待定问题）。

## Win10 模糊层在左/右/下溢出一圈

用户反馈（2026-10-28）：Win10 上模糊层在左、右、下三侧比可见窗口大一圈；顶部对齐。Win11 当前没有这个问题。

### 原因（代码推断，未实机测量）

- 窗口样式是 `WS_OVERLAPPEDWINDOW`（含 `WS_THICKFRAME`），`WM_NCCALCSIZE`（`Setting.cpp:1305-1316`）用 `QuerySettingFrameInsets` 从窗口矩形内缩客户区。
- 内缩量来自 `QuerySettingClientFrameInsets`（`Window.cpp:4075-4107`）：左/右/下 = `AdjustWindowRectExForDpi` 得到的 sizing frame（96 DPI 下约 8 px，即 Win10 的不可见缩放边框）；顶部 = `DWMWA_VISIBLE_FRAME_BORDER_THICKNESS`，该属性 Win11 才有，Win10 查询失败，顶部为 0（`Setting.Layout.h:92-100`）。
- accent policy 的模糊覆盖整个窗口矩形（`GetWindowRect`），包括这圈不可见的非客户区，而不是只覆盖客户区。
- 结果：Win10 上内缩为 0 的顶部对齐，内缩约 8 px 的左/右/下三侧露出一圈模糊。这与反馈的三侧完全一致，可以作为原因的旁证。
- Win11 现在没问题，是因为 Win11 走的是 Mica 分支，DWM 按可见窗口边界绘制。**本任务把 Win11 改为 accent 后，Win11 很可能出现同样的溢出**，所以修复必须同时覆盖 Win10 和 Win11。

### 缩放热区的现状（用户补充的约束）

用户要求（2026-10-28）：缩放热区必须保持“以窗口边框为中心、两侧各有一段距离”的现有设计；不能把热区全部移到窗口内侧，否则会挡住滚动条并影响使用。顶部现在就是这个情况，希望一并修掉。目标交互与 WinUI3 原生窗口一致。

现状代码：

- `HitTestWindowFrame`（`Setting.Layout.h:162-186`）只在 `outer` 与 `client` 之间的环带返回边/角命中；客户区一律返回 `Client`。
- 恢复态下 `WM_NCCALCSIZE`（`Setting.cpp:1305-1316`）左/右/下内缩约 8 px（96 DPI），顶部为 0。所以左/右/下的热区在窗口外侧，顶部的热区落在窗口内侧，正好对应“顶部有问题”的反馈。
- 因此“热区全部在内侧”不是现状，只是方案 A 会引入的新问题，不能采用。

### 可选修法

| 方案 | 做法 | 优点 | 代价 |
| --- | --- | --- | --- |
| D（推荐） | 保留现有约 8 px 非客户区环带；accent 平台额外加一个透明、不绘制的边框子窗口，覆盖窗口矩形外侧同样宽度的一圈。鼠标落在该子窗口时由它转发 `WM_NCHITTEST`，按“以边框为中心”返回边/角命中 | 模糊只画在主窗口矩形内，与可见边界对齐；热区跨在边框两侧；顶部与其他三边一致；不改渲染 | 多一个窗口；需要跟随移动/缩放/DPI/显示隐藏；子窗口不能出现在任务栏与 z-order 策略里 |
| A（放弃） | 四边内缩为 0，热区移到客户区内侧 | 模糊对齐 | 热区全部在内侧，挡住滚动条，正是用户排除的做法 |
| B | `SetWindowRgn` 裁掉不可见边框 | 改动小 | 关闭 DWM 阴影与 Win11 圆角，region 窗口合成更慢 |
| C | accent flags 画边框 | 无 | 只能加边框线，不能裁剪模糊区域 |

### 方案 D 的依据

- accent 模糊覆盖 `GetWindowRect` 的整个窗口矩形，不会画到窗口矩形之外。边框子窗口在窗口矩形之外，所以它保持透明，模糊不会进入它。
- Win32 的命中测试只发给光标正下方的窗口。窗口矩形外侧的光标现在落到别的窗口上，主窗口收不到 `WM_NCHITTEST`，所以必须由那个位置上的窗口自己回答。
- 子窗口用 `WS_POPUP | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，`SetLayeredWindowAttributes` 设 `LWA_ALPHA = 0`，不创建 swap chain、不绘制。`WS_EX_TOOLWINDOW` 让它不进任务栏，也不进 Alt-Tab。
- 子窗口的 `WM_NCHITTEST` 把屏幕坐标交给主窗口现有的 `HitTestWindowFrame`，只是把“外侧”扩展为边框宽度。返回的 `HTLEFT` 等发给子窗口后，子窗口把 `WM_NCLBUTTONDOWN` 转成主窗口的 `WM_SYSCOMMAND`（`SC_SIZE` + 方向），缩放循环仍由系统在主窗口上执行。
- 子窗口不走 `Window::Service`。`Service` 的 `EnumWindows` z-order 快照（`Window.cpp:1848-1907`）把同进程、owner 链回到已知窗口的窗口当作允许穿插的辅助窗，所以只要 owner 设为主窗口就不会破坏该策略。创建与销毁放在 `ImGuiWndProc` 内，与主窗口同线程。

### 方案 D 需要处理的消息

- 跟随：主窗口 `WM_WINDOWPOSCHANGED`、`WM_SHOWWINDOW`、`WM_DPICHANGED` 时重算子窗口位置与尺寸；最小化与隐藏时隐藏子窗口。
- 圆角：Win11 上主窗口四角是圆角，子窗口不能填进圆角切掉的区域，否则直角模糊块会露在圆角外面。子窗口用 `CreateRoundRectRgn` 组合区域，只保留四条边带与四个外角，挖掉圆角内侧。圆角半径取 `DWMWA_WINDOW_CORNER_PREFERENCE` 的默认值，需实机确认。
- 阴影：主窗口保留现有非零非客户区，DWM 阴影行为不变，不新增 frame margin。
- 激活：`WS_EX_NOACTIVATE` 避免子窗口抢焦点；按下时显式把主窗口设为前台再开始缩放。
- 销毁：主窗口 `WM_DESTROY` 时销毁子窗口。
- 旧系统：Win7/8/8.1 不启用 accent，也不创建子窗口，框架几何保持原样。

### 方案 A 的验证项

- DWM 阴影：Win10 上零非客户区的 `WS_THICKFRAME` 窗口需要非零 frame margin 才保留阴影；margin 区域是否被 accent 画出可见线条需实机确认。
- Win11：零内缩后 DWM 是否仍按圆角裁剪模糊层；若出现直角或溢出，再决定是否设置 `DWMWA_WINDOW_CORNER_PREFERENCE`。
- 最大化态保持现有完整内缩，不受影响。
- Aero Snap、拖动缩放、`WM_GETMINMAXINFO` 的最小尺寸计算（`Setting.cpp:1515-1531`）需随新内缩一起验证。

## Win11 上使用 accent policy 需要注意的点（实机验证项）

- **圆角**：Win11 窗口默认圆角，accent 模糊层是否按圆角裁剪需实机看。若出现直角模糊溢出，可设置 `DWMWA_WINDOW_CORNER_PREFERENCE`；但这属于视觉调整，先验证再决定。
- **拖动卡顿**：Win10 1903+ 上 Acrylic accent 在拖动/缩放时有明显延迟，现有代码已在 move/size loop 内切到 `ACCENT_ENABLE_BLURBEHIND`。Win11 沿用同一逻辑。
- **系统“透明效果”开关 / 节电模式**：accent policy 不受系统“透明效果”开关自动控制（与 Mica/系统 Acrylic 不同）。是否需要读取该开关见 PRD 待定问题。
- **性能**：模糊由 DWM 合成，代价在 DWM 进程/GPU，随下层窗口变化实时重算；应用侧不增加帧数。设置窗口可见时本来就按 60 FPS 上限连续绘制，本任务不改变渲染频率。
