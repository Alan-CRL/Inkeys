# Research: 鼠标绘制光标与 Draw3 现有能力

- **Query**: 调查生产与 Draw3 demo 的鼠标／屏幕笔／荧光笔光标、亮暗切换、runtime 同步和穿透边界，明确新笔形光标及粗细预览的实际增量。
- **Scope**: internal；只读生产代码、仓库内 demo 副本与已知本地 Draw3 来源目录，未执行远程访问。
- **Date**: 2026-10-18
- **阶段**: 本轮仅调查和规划；未改代码、spec、配置；未构建、运行测试或启动 GUI。

## 2026-10-19 用户澄清

默认鼠标画笔笔形、荧光笔屏幕笔同款预览加高亮边缘；高亮按 Mouse/ScreenPen/Digitizer 独立保存，默认高亮/半透明/高亮。原报告未找到独立 palette UI 的事实保留，但具体外观意图已确认，后续以 [cursor-appearance-clarification.md](cursor-appearance-clarification.md) 为准。

## Findings

### Files Found

| File Path | Description |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cppm:17` | 应用光标 shape、appearance、sample、visual 及策略接口 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:270` | Pen／Mouse／工具光标的主 visual 解析 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:379` | 与应用 visual 配套的系统光标隐藏决策 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:302` | 生产 Pen／Highlighter／Laser appearance 生成 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:10882` | 主 visual、多触点橡皮与帧合成边界 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:757` | mouseUsesSystemCursor 原子单一来源及系统刷新 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp:245` | 瞬态光标向 backbuffer 最上层提交 |
| `Inkeys/Inkeys/Drawing/Draw3/Assets/inkVertexShader.hlsl:77` | 当前 cursor 的中心对称 quad |
| `Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:269` | 当前 cursor SDF、fill／outline 与 premultiplied 输出 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1412` | 绘制 owner 消费产品状态 |
| `Inkeys/IdtState.cpp:215` | 产品工具／宽度／颜色／selectionMode 的唯一桥接投影入口 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:3903` | 设置→绘制现有板块 |
| `InkeysHeadlessTests/draw3_contact_tests.cpp:1123` | 直接使用生产 resolver 的透明度与来源测试 |
| `inkStrokeModelerTestTests/pen_cursor_tests.cpp:29` | demo 光标逻辑测试；不能代替生产测试 |

### 1. 已有三个开关的精确语义

默认值见 `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm:225`：

```cpp
bool drawingCursorDuringContactEnabled = false;
bool translucentInkCursorEnabled = false;
bool mouseUsesSystemCursor = true;
```

| 开关 | 已确认行为 | 尚不具备的含义 |
| --- | --- | --- |
| `mouseUsesSystemCursor` | 普通鼠标绘制工具默认保留系统箭头；false 时主 visual 可用应用 appearance；Eraser／Laser 保留专用光标 | 不是笔形／箭头图案选择；不是“隐藏触摸光标”开关 |
| `drawingCursorDuringContactEnabled` | 只控制普通 Pen／Highlighter 在屏幕笔 contact 时是否继续显示应用主光标，默认隐藏 | 不控制 Mouse contact；不控制 Touch 橡皮或 Laser／Eraser |
| `translucentInkCursorEnabled` | 在普通不透明 Pen 的屏幕笔 visual 中，false 将 fillAlpha 提升至 1；true 保留 appearance 原 alpha | 不是独立 RGB palette、明亮／暗色主题，也不是通用 opacity 开关 |

上述边界直接来自 `Draw3.PenCursor.cpp:292-330`。生产特意在 `:318` 加入 `!eraser && appearance.opacity >= 1.0f` 门禁，避免把荧光笔 0.35 或橡皮 Hover 0.5 改为完全不透明。Mouse 普通 Pen 在 `:325-329` 使用完全不透明填充，不继承屏幕笔的半透明模式。

系统 cursor 的配套决策在 `Draw3.PenCursor.cpp:385-401`：Eraser／Laser 不依赖 hover sample，始终隐藏系统 cursor；Pen owner 和 Touch visual authority 隐藏系统 cursor；Mouse owner 只有 `!mouseUsesSystemCursor && mouseSampleValid` 时隐藏。此决策与应用 visual 的来源选择必须一起扩展。

即时更新已经存在：Controller setters 在 `Draw3.DrawingController.cpp:7978-8009`，Contact／Translucent 变化发布 control wake；Mouse setter 委托 Window。`Draw3.WindowControl.cpp:757-767` 在鼠标选项实际变化时同时 `RequestDrawingCursorRender()` 与 `QueueSystemCursorRefresh()`，用于清掉旧应用图形并切系统 cursor。

### 2. 鼠标荧光笔预览已具备几何，产品设置尚未接通

生产 `ConfigureProductInkCursorAppearances` 已生成荧光笔矩形（`Draw3.DrawingController.cpp:324-339`）：

```cpp
const float highlighterDiameter = CanvasDiameterForTool(
    DrawingTool::Highlighter, visualStyle, dpiScale);
DrawingCursorAppearance highlighterAppearance = {
    DrawingCursorShape::Rectangle,
    highlighterDiameter / kHighlighterNibAspectRatio,
    highlighterDiameter,
    highlighterColor.x, highlighterColor.y, highlighterColor.z
};
highlighterAppearance.opacity = highlighterColor.w;
highlighterAppearance.fillAlpha = 1.0f;
```

- `DiameterForTool` 使用 `visualStyle.widthDip`（`:167-177`）；`CanvasDiameterForTool` 统一调用 `PenWidth::CanvasWidthPx(..., penWidthContext)`，只转换一次（`:186-195`）。因此粗细预览应复用这一来源，不能再乘一遍 DPI。
- 普通 Pen 当前是圆点，直径取实际 canvas 宽度与 `5 DIP * SystemScale(context)` 的最大值（`:305-320`）；这里的最小可见圆点尺寸不能直接当作新笔形图标尺寸。
- 工具 RGB 来自产品 visualStyle。`translucentInkCursorEnabled` 保留／覆盖 alpha，未切换独立亮暗色。
- 当前帧在 `:13169-13179` 消费最新样式；hover 随设置变化，已 Down 的笔画读取其锁存样式。新 cursor 的配置变化也需要即时唤醒帧，不等待鼠标移动。
- Mouse 的普通工具只因 `mouseUsesSystemCursor=true` 被 resolver 提前返回；设为 false 后，现有 Highlighter rectangle 已可进入 Hover 和 contact 路径（`Draw3.PenCursor.cpp:296-304`）。R4 的主要现有能力是“实际粗细矩形”，增量是产品策略／设置接通及验证，并非从零实现矩形 shader。

**spec 过时提示**：`.trellis/spec/native-desktop/draw3-integration.md:313` 与 `:338` 仍称荧光笔固定 `6.25 × 50 px`；当前生产源码已是产品宽度驱动，且 `Draw3.DrawingController.cpp:4059-4062`、`:4084-4088` 的现有生产 probe 断言同一宽度边界。本轮不改 spec；后续计划不能复制旧常量。

### 3. 新“笔尖左上／笔尾右下”不是现有圆点切换

`Draw3.PenCursor.cppm:17-22` 当前只有 `Circle`、`Rectangle`、`EraserGripCircle`。`DrawingCursorVisual` 的 `x/y` 在 `:103-108` 只是渲染中心，appearance 未定义热点／偏置。`ResolvePrimaryDrawingCursorVisual` 在 `.cpp:314-316` 原样复制 sample 的 `x/y`。目前没有可直接启用的斜向笔图形。

当前 CPU／GPU 契约有三个必须同时考虑的位置：

1. `Draw3.RendererPrimitives.cpp:330-335` 将 enum 映射成 shader type `4 / 5 / 6`，三元表达式的最后一个分支直接落到 EraserGripCircle；简单追加枚举会被误画成橡皮。上传两枚 InkPoint，第一枚为中心和 halfSize，第二枚为 outlineWidth 与 fillAlpha。
2. `Assets/inkVertexShader.hlsl:77-93` 只接收 type 4..6，按 `center ± halfSize ± 2px` 生成 quad；`Assets/inkPixelShader.hlsl:269-307` 只处理矩形／圆／橡皮抓手，按 `opacity * fillAlpha` 合成填充，输出 premultiplied Add／Retain。
3. `Draw3.PenCursor.cpp:546-567` 的 CPU dirty bounds 也按中心 ± halfSize + 2px，不能在 shader 单独向右下画出尾巴后继续使用旧 bounds。`:570-576` 的等价判断包含当前 appearance；新增任何热点／主题／图案字段必须进入等价与 bounds 语义。

两个技术候选，均为待评估方案，不代表已作产品选择：

| 候选 | 需要的最小机械增量 | 本轮证据／限制 |
| --- | --- | --- |
| 扩展现有 transient shape + 非居中热点 | 增加可识别的笔形、明确笔尖位置与图形包围框的偏移，统一 resolver、renderer 映射、VS／PS、CPU dirty bounds 和等价判断；笔尖仍对应原 sample，笔尾沿右下延伸 | 复用现有瞬态合成；应只作用于 Mouse 的目标画笔范围，避免改变屏幕笔圆点／图形工具的已有合同；不选用已有 shader type 冲突值 |
| HWND owner 使用自定义 `HCURSOR` | 使用带笔尖热点的资源或缓存，按工具／样式／DPI 和 selection／穿透切换系统 cursor；明确资源生命周期和鼠标进入／离窗／旧 Pen owner 的恢复 | 仅列待评估：本轮未查此方案的 Windows API 及 Win7 细节；现有 runtime spec 明确避免自建 HCURSOR 和每次粗细变化重建资源，采用前须协调合同，不能把它当作已证实的低风险捷径 |

若走 transient shader 路径，Win7 兼容体必须同步纳入改动范围。`Draw3.Renderer.cpp:699-719` 在 Win7 WARP 默认选择 `Draw3.Win7GuardedShader.h` 的内嵌完整 PS 字节码，而非正式 `.cso`；选择边界见 `Draw3.ShaderCompatibility.h:9-17`。只修改正式 HLSL 会使两条路径的笔形能力分离。既有生成脚本位于 `.trellis/tasks/archive/2026-10/10-02-win7-main-canvas/research/Generate-Win7-GuardedShader.ps1`；本轮没有执行或重新生成。

### 4. 瞬态合成与穿透的现有边界

- `Draw3.PenCursor.cppm:99` 将 visual 明确定义为瞬态，不进入 L0／L1／L2。
- `Draw3.DrawingController.cpp:10887-10897` 在 `SelectionMode()` 直接返回零绘制 cursor。产品 `IdtState.cpp:226-227` 将 Selection 与 ClickThrough 一起投影为 `selectionMode`，判定函数定义于 `Inkeys/IdtState.h:21-24`。
- `Draw3.DrawingController.cpp:14777-14786` 将上一帧和当前 visual 的 bounds 合并到 dirty，先重建旧区域；别的几何触发 Present 时也重建当前 cursor 区域，避免半透明叠加加深。
- `:14908-14909` 在各图层合成之后才绘制 currentCursorVisuals。新笔形应保留这一瞬态生命周期，导出／历史／Undo 不应包含 cursor。
- `Draw3.WindowControl.cpp:1261-1274` 的系统刷新先检查 `WindowFromPoint` 是否为自己／子窗，避免私有消息改变别的窗口当前 cursor；`:1291` 仅用 `SetCursor(hide ? nullptr : defaultCursor_)`。
- `IdtState.cpp:1963-1970` 可见窗口服务对辅助表面固定 click-through，并让主 Drawpad 的 click-through 跟随 selectionMode；实际普通 desktop／PPT 的呈现握手仍由该模块统一管理。
- `SetSelectionMode` 现状只更新原子并 `RequestControlWake()`（`Draw3.WindowControl.cpp:588-593`）。新样式切换需要明确覆盖绘制→穿透→绘制、设置在穿透时改变、鼠标静止、抬笔后无 Move 等生命周期，不能单凭当前 zero-visual 分支宣称“系统箭头必然即时恢复”。

### 5. Host 与设置入口：现成 Controller API 尚未成为产品选项

**名称澄清**：在生产与仓库 demo 中没有找到名为 `HostConfiguration` 的类型。当前组成是 `HostStartOptions`（`Draw3.Host.h:183`）和 Controller 接收的 `StrokeModelConfiguration`（`Draw3.DrawingController.cpp:7894-7899`）。三项 cursor 开关均在后者；HostStartOptions 没有对应字段。

当前数据流：

```text
IdtState::PublishDraw3State
  -> PublishProductState 的逐字段 merged
  -> Bridge::ProductState
  -> Host::PumpBridgeState（绘制 owner）
  -> Window tool / style / selectionMode
  -> Controller 帧边界 appearance + resolver
```

- `IdtState.cpp:215-229` 只发布当前工具、交互模式、橡皮配置、笔宽、颜色、selection 和 autosave；`Draw3.Product.cpp:90-105` 又逐字段 merge，新增字段不能只在一侧填入。
- `Draw3.Bridge.h:194-207` 当前无 cursor preferences。`Draw3.Host.cpp:1412-1453` 也无三项 cursor setter 的映射。
- Host 在 `:1758-1761` 创建默认 StrokeModelConfiguration，只额外覆盖断触接续；所以默认鼠标箭头等行为仍来自 Controller configuration 默认值。
- 现成独立偏好同步样例是接续开关：`Host::SetInterruptedStrokeReconnectEnabled` 在 `:2337-2342` 更新期望原子并唤醒，不直接跨线程碰 Controller；`PumpBridgeState:1414-1417` 在 bridge revision 去重之前由绘制 owner 同步。
- 技术上既可扩展不可变桥接状态，也可为独立偏好复用 Host 期望快照／owner 消费模式。两者应择一明确所有权和唤醒，不能从设置线程直接引用 drawing 指针。具体配置归属／迁移由主调查报告决定。
- `Inkeys/IdtDrawpadFacade.cpp:8` 是旧 UI 符号兼容存根；`:79-82` 只把 Mouse message 转发产品，`:85-87` 明确 RTS 由 Host 初始化；此文件没有可恢复的活动 cursor policy owner。

设置→绘制目前包含交互模式（`Setting.cpp:3918`）、橡皮（`:4007`）及触摸接续（`:4145-4185`），未发现鼠标样式或亮／暗 cursor 板块；`:7774` 的“光标调试信息”仅是实验日志开关。新光标块需新增，沿现有卡片、三语资源与配置快照写盘合同。接续卡的 `QueueConfigWrite()` 用法见 `:4176-4179`。

`IdtState.cpp:725` 已通过 `DeferDraw3SyncIfRendering()` 避免设置渲染锁内同步 HWND；`Setting.cpp:199-207` 的业务 worker 分别处理 Sync 和冻结 Config snapshot。新增设置的 runtime 更新不能绕开该线程边界。

### 6. Draw3 demo 与亮／暗选项的证据边界

核对范围：

- 本仓库 `inkStrokeModelerTest/`：三项 capability 的 module／setter 存在，入口 `main.cpp:105-118` 使用默认 configuration。搜索未找到非定义／非测试的 setter UI 调用。
- 既有 spec 指向且本机存在的来源目录 `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/`：同样找到 `inkStrokeModelerTest/draw3/drawing_controller.cpp:1256-1287` 的 setter，未找到它们的 UI 调用；`draw3/window_control.cpp:1478-1537` 当前可见键盘分支是工具选择／橡皮等快捷键。
- 两个范围均未找到独立 cursor palette／brightness／light-dark UI。不是断言用户记忆错误；可能指其他 Draw3 分支／版本／入口，后续需要精确来源。当前可以确认的是透明度切换 API，而不能把它改称亮／暗开关。
- demo resolver `inkStrokeModelerTest/draw3/pen_cursor.cpp:286-298` 的 Pen alpha 归一化比生产更宽；生产 `.cpp:318-329` 已保护荧光笔／橡皮真实 alpha。直接搬 demo resolver 会丢失产品约束。

### 7. 现有测试与后续可安排的无窗口验证

本轮只读取测试源码，以下不是已运行／通过的结论。

| 现有位置 | 能证明的范围／用途 |
| --- | --- |
| `InkeysHeadlessTests/draw3_contact_tests.cpp:1123-1214` | 生产 resolver 的 highlighter opacity 0.35、普通鼠标系统／应用路径、Touch 不生成主 visual、橡皮 hover／contact alpha |
| 同文件 `:1216`、`:1281`、`:1343` | 系统合成 touch Move 过滤、序列和 cursor ownership；可扩充 Win7 API 缺失／真实鼠标接管矩阵 |
| `InkeysHeadlessTests/draw3_mouse_input_tests.cpp:51`、`:92`、`:134`、`:171` | 鼠标绘制 lifecycle、admission、capture loss 和 stop interleaving；不是视觉／Win7 设备证明 |
| `InkeysHeadlessTests/draw3_bridge_tests.cpp:50` | ProductState 发布与 revision；若 cursor preferences 进 bridge，应纳入读写／去重测试 |
| `Draw3.DrawingController.cpp:4041-4088`；入口 `Inkeys/IdtMain.cpp:1376` | 生产 `--draw3-pen-width-test` 的实际宽度／DPI／大屏矩形 cursor 断言；适合增加新 icon hotspot 和荧光笔粗细边界 |
| `inkStrokeModelerTestTests/pen_cursor_tests.cpp:29` | demo appearance、sample、authority、contact 与系统 cursor 矩阵、bounds；该 vcxproj `:184-185` 链接 demo 模块，不能代替生产 resolver |
| `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:133-134` | 明确链接生产 `Draw3.PenCursor`，生产回归应放此入口或现有生产 probe |

后续实施可安排：

1. 生产纯逻辑矩阵：鼠标原版／新式 × Pen／HardPen／Highlighter／Eraser／Laser × Hover／Contact／无 sample；屏幕笔与 Touch 不被鼠标图案偏好误伤；selected scope 须在产品设计中列明。
2. 笔形解析与 bounds：笔尖严格等于输入 sample；尾部向右下；各 DPI、负／极大坐标、画布四边裁剪、旧／新图形并集、主题变化即时无残留。
3. 生产设置／owner 同步：默认／合法／非法值、快照／写盘、静止光标切换、穿透状态下改样式、返回绘制、无重复 render wake；主调查负责配置详细测试范围。
4. 若扩展 shader，再安排无交互窗口的编译和 GPU／读回验证，覆盖正式资源与 Win7 guarded PS。隐藏 HWND／GPU probe 与纯 `--no-window` 分开说明；未经本轮授权均未执行。
5. Win7 单指／多指擦除、小 cursor 来源、抬笔系统 cursor、落笔闪现、亮暗视觉可辨性、SDR／HDR／多显示器只能另行真机／人工验收。当前源码和自动逻辑测试无法证明这些视觉问题已解决。

### External References

无网络资料；本报告仅源码能力和本地合同，未对 Win32 新 API／Win7 行为作额外兼容性承诺。自定义 HCURSOR 候选 API 留主计划待评估。

### Related Specs

- `.trellis/spec/native/runtime-and-rendering.md:337-377` — 现有 Pen／Mouse／Touch／Contact policy、dirty bounds、禁止逐次重建 HCURSOR；此合同目前覆盖 demo／历史路径，生产差异需按源码核对。
- `.trellis/spec/native-desktop/draw3-integration.md:294` — 工具 opacity、snapshot、active stroke 锁存；固定荧光笔宽度文案已过时，本轮保留原文。
- `.trellis/spec/native-desktop/draw3-shaders.md:3` — 产品 HLSL、FxCompile 与资源链。
- `.trellis/spec/shaders/index.md:23-35` — CPU／GPU layout、bounds／AA 与完整 shader 资源验证合同。

## Caveats / Not Found

- 未发现可直接复用的斜向笔图形、独立亮暗 palette，亦未发现 cursor preferences 的生产 Host／bridge／设置接线。
- 原调查未确认具体 Draw3 版本/入口；2026-10-19 用户已明确半透明/高亮边缘及分场景保存，该版本查找不再阻塞外观规划。参数与边缘 alpha 的实际差距见补充报告。
- 当前确认软件应用光标存在现成荧光笔矩形与真实宽度来源；是否统一鼠标新式、哪些 Pen／HardPen／Shape 使用笔形以及主题默认值仍属产品决策。
- 小的第二枚橡皮是否是系统 cursor／系统反馈／陈旧主 visual，本报告不作定论；需结合输入与 Win7 专项证据。
- 没有运行测试、构建或 GUI，未验证 Win7 真机，也没有写入研究目录以外的文件。
