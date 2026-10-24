# Research: UI3 字体最终像素字号、变换与缓存

- Query: 主栏/分页/属性控件的自由缩放是否保留非整数像素字号，以及最小可行截断入口。
- Scope: internal（官方单位文档由主会话查阅）；仅本次任务 research 写入。
- Date: 2026-10-24

## Findings

### 目标与现状

- 用户后续限定只处理主栏和控件 UI 字体，设置窗口 ImGui 字体不属于本次。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:3020` 的 `BarUIRendering::Word` 是主栏、属性面板以及分页/白板菜单的共享文字绘制入口。`tarSize = word.size.val * frameZoom * contentScale`，其中 contentScale 是文字切换动画缩放；现代码以该浮点值直接查 TextFormat。
- `Bar.Format.cppm:40–94` 的 `BarFormatCache::GetFormat` 用完整浮点字号和字体族、字体集、字重、对齐、locale 等共同作为缓存键。缓存每帧使用计数，经 `Clean` 淘汰，`CleanAll` 全清。直接在 cache 内无条件 floor 会错误处理逻辑 DIP 测量以及仍要经过 DC 缩放的调用，不能作为唯一修复。
- `Bar.Metrics.cppm:36,215–268` 定义按钮字号，例如 13、12、15 DIP。48DIP 高度基准对应 80DIP 设计高度的 0.6 倍时，13DIP 字号最终变为 7.8px，用户所述场景确有非整数字号来源。

### D2D 单位与真正最终缩放

- `Bar.Rendering.cpp:265–289` 用共享 epoch 创建 D2D device context，再创建 premultiplied BGRA/GDI target。未显式 SetDpi/SetUnitMode；生产路径按默认 96DPI 计算，frameZoom 已包含显示 DPI/缩放基准，不要再乘显示器 DPI。
- 同文件 `MapToDeviceBounds`（49–74）已有考虑 UnitMode/DPI 的坐标映射先例，可沿用查询方式。
- `Bar.Button.cpp:2055–2079` 的 `ButtonDrawTransform` 在调用 Word 前再叠加按钮 `pressScale`；这会让原本整数格式字号再次变为非整数实际像素。
- `Bar.RenderLoop.cpp:9452–9483` 构造底栏 body/grip/rigid DC 变换，body 存在独立 X/Y 缩放。字号作为 em 高度应按 Y 基向量的实际设备长度处理，不能假设 _11==_22 或 DC 恒为 Translation。
- `Bar.RenderLoop.cpp:11404–11426` 将浮窗完整字号资源放在 `popupGeometryScale` DC 变换下绘制。只截断 word.size 或 tarSize 而不考虑此变换依然不满足最终像素整数。
- 最小数字规则：raw 字号乘 DC Y 方向设备比例，得到 px；关闭选项时 floor(px)，再除以相同比例还原给 DWrite。比例可用 `hypot(_21 * dpiX/96, _22 * dpiY/96)`（PIXELS 模式 DPI 因子为 1）。所有输入须有限且比例大于零；floor 后为 0 则不画字，不能 clamp 到 1 导致文字反向放大。开启时直接保留 raw 字号。
- 该规则只保证字体 em 高度整数；保留非均匀动画、fractional origin 与现有抗锯齿意味着不能承诺所有视觉模糊消失。不要同时改抗锯齿模式或强制把图标/布局吸附整像素。

### 分页与白板控件

- `Bar.Scene.cpp:638–643` 的 `SetPose` 将 `scale * pose.scale` 设置到 renderer.frameZoom，DC 只设置 Translation，因此该路径的菜单缩放已经进字号而非 DC。
- `Bar.Scene.cpp:649–686` 所有分页/菜单/主题按钮均调用共享 `DrawBarButtonVisual` -> Word；主题 heading 和未保存提示也走 Word（680、896）。统一 Word 修复自然覆盖这些内容。
- `Bar.Scene.cpp:729` 的 `SetBounds` 更新 renderer.frameZoom 并 FullDamage；相同 bounds/scale 直接 early return。
- 仅 `backgroundOnly` 自定义 drawContent 会主动使用 scale DC（886–888）；分页标准文字不走这个分支。
- `PageControl.cpp:1274–1282` 没有 damage/动画/完整替换时 PresentScene 直接 idle；所以单纯唤醒 scheduler 无法保证重画。`NotifyLayoutChanged` 是可复用的公开通知（PageControl.cppm:1464、PageControl.cpp:4754），它会增加 publishedRevision 并 RequestAll；`ConfigureSurface:1126–1133` 在 revision 变化时重配分页，如果 scene 没有 invalidated 就设 forceFullPresentation，因此这一现有通知能使静止分页完整重画。
- 隐藏 surface 可能没有真正 Render。策略值必须在重新配置/Advance 检测，或为每个 surface 保留待应用 revision；不能只在当次可见 Render 清缓存，否则隐藏后再显示可能用旧布局。

### 独立文字与缓存遗漏点

- `Bar.Rendering.cpp:1567–1638` 的 `GetThicknessFineDialLabelLayout(value,zoom)` 是独立数字布局缓存，key 仅 value/zoom。字号为 10*zoom（1595–1596），layout width/height 为 64*zoom/20*zoom；`Bar.RenderLoop.cpp:10582` 取 layout、10620 DrawTextLayout。需要量化真实字号并使策略/有效比例进入 key 或在策略切换时清空；不能只修改 Word。
- `Bar.RenderLoop.cpp:7672` 的 dockModeTextFormat 直接 GetFormat，7684 创建实际文本测量布局，12104 在额外 `dockTargetIndicatorScale * textScale` DC 变换内绘制。绘制阶段需取该变换下解析的格式；测量仍可保留完整尺寸包络，避免动画时频繁重排指示器宽度。
- `Bar.RenderLoop.cpp:12118–12145` FPS 调试文字直接 GetFormat/DrawTextW，使用 12*frameZoom。若本次定义所有控件字体，需接同一策略；其测量 8769 走 MeasureText。
- `Bar.RenderLoop.cpp:11296,11307` 只是 tooltip 预热格式。若 Word 改用量化字号，则预热也应沿同一逻辑以免每帧保留用不到的 fractional 格式；不影响绘制正确性但影响缓存意图。

### MeasureText 契约和测量缓存

- `Bar.Rendering.cpp:3107–3159` 的 `MeasureText(content,fontSize,weight)` 输入/返回均为设计 DIP，与 Word 的最终像素输入不同。它用未缩放字号创建 layout，结果向上 ceil，以便布局留足空间；失败时按原始逻辑字号计算保守包络。
- 稳定状态同策略测量可先得到 `logicalSize = floor(fontSize * frameZoom) / frameZoom`，再用逻辑字号测量，返回仍为 DIP（相当于真实 px 尺寸除 zoom）。如果还需支持 DC/DPI，给解析函数显式传入有效比例，不要隐式乘第二次 frameZoom。
- 动画中 Word 仍依据 press/popup/body 等最终缩放解析字号，而布局保留完整未截断包络比较安全；不能每次 1px 跨档就修改 panel 的 target 几何。
- `Bar.RenderLoop.cpp:791–829` 状态字段初始化时预先缓存 holdLockLabel、colorPickerHold、footer 各类、annotation/overflow 各类测量。这些不会随 zoom/新选项自动重算；如果改成精确量化测量，须在稳定 zoom/策略变化时刷新，或者明确保留未截断的安全布局包络。
- 5820 的 preview measured size 只随文本变化更新；6081 annotationTitleSize 只随 title 变化；9181 envelope size 同样只随内容变化。策略和 zoom 应加入失效条件，否则会保留旧量化尺寸。
- `Bar.Scene.cpp:321–324` 的横向紧凑分页缓存 primaryWidth/secondaryWidth 只随文本更新；当前产品 `BarPptCompactPaginationEnabled=false`，不应为此重构分页，但需记录隐藏分支和策略切换行为。

### 最终选择：保守测量包络

主会话最终设计选择保留现有 `MeasureText` 原始未截断逻辑字号，作为实际字体只缩不扩的保守布局上界。此方案可行且改动最小：不修改 MeasureText 签名，不修改上述预览/提示/页码布局测量缓存，不因字体跨 1px 档位重排面板。原有 ceil/no-wrap/布局槽位语义继续保留；新策略仅约束实际绘制字号。现有测量有 ceil 安全量，字号截断后不需把这个安全量反向削掉。

### 建议最小改动结构

1. 纯数字 helper 可放 UI/Bar 下新 `Bar.FontSize.h`，只接受 rawSize、effectivePixelScale、allowFractional，便于既有 Headless 用同源生产函数测试；避免引入新的 C++ module 构建节点。
2. `BarUIRendering` 提供可复用的 `ResolveFontSize(ID2D1DeviceContext*,double)` 或等价公开包装；读取 DC transform/unit/dpi 并调用纯 helper。Word、细刻度与 RenderLoop 的直接文字均复用。
3. renderer 帧域冻结 allowFractional；公开 `UpdateFontRenderingPolicy(bool)->bool changed` 或等价方法，变化时只清实际细刻度 layout cache。主栏在帧起同步策略时设 `state.unclassifiedDamagePending=true`、`state.presentDecision.RequireVisualRetry()`，既有 `Bar.RenderLoop.cpp:7489–7504` 会将其纳入 active rendering；设置开关沿现有 UpdateRendering/NotifyLayoutChanged 唤醒全部目标。分页 Scene.Advance 在 `frameSeen && now==previousFrame` early return（561）之前同步策略并 FullDamage，保证直接离屏场景、相同 frameTime 与隐藏后恢复均可使缓存失效。不要每个 Word 自己读取原子配置导致同帧混合策略。
4. 传统 icon/SVG、panel 形状/布局缩放保持现状。不把字体精确测量变化扩散为整个 UI 重排架构。

### 可复用无窗口验证

- `IdtMain.cpp:1537` 的 `--bar-eraser-offscreen-test` 在配置、互斥体和产品 GUI 初始化前直接返回 `RunEraserAttributeOffscreenTest`；1539 的多目标 benchmark 同样无 HWND。
- `Bar.EraserAttribute.Test.cpp:157` 是真实 D2D/字体/Bar 场景离屏入口，已能取 CPU 像素（112 起）、切换字体/i18n并验证布局。已有 2370–2371 的 SetDpi/UnitMode 测试提供非96DPI上下文先例。
- `PageControl::RunOffscreenTests`（PageControl.cppm:1446）调用产品 Scene/布局，不创建 HWND；不要误用紧邻的 `RunHiddenWindowTests`，后者会创建真实四 HWND。
- 数字回归至少包括：13*0.6=7.8 -> 7；开关 true=7.8；整数不变；zoom、DPI、press、popup组合；非均匀变换的Y方向；PIXELS模式；极小 <1px 返回0；NaN/Inf/非正比例拒绝；切换和隐藏恢复时同内容同zoom重新绘制。

## Related Specs / References

- `.trellis/spec/native-desktop/rendering-and-ui.md`：共享 renderer/目标资源所有权、dirty 与 committed transaction。
- `.trellis/spec/native-desktop/startup-preview.md`：preview 为程序化图形且不绘制 UI 文字，不需独立扩展。
- `.trellis/spec/native-desktop/cpp-conventions.md`：最小diff、CRLF/编码、中文关键注释。
- 主会话已查阅 Microsoft 官方单位文档：https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-and-high-dpi 。本文源码调查未启动 GUI、未编译或运行测试。

## Caveats / Not Found

- 不把真实设备观感当作已验证结论。整数字号只是用户要求的可切换渲染策略；分数坐标、低 px 字号、灰度抗锯齿及动画都可能继续影响清晰度。
- 不改设置窗口 ImGui 字体，不恢复传统 IdtFloating，不改第三方字体渲染代码。
- 本文件所有 line anchors 以调查时源码为准；代理并行实施后行号可能漂移。
