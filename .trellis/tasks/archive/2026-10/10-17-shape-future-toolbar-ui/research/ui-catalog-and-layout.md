# Research: 图形主栏未来工具目录、UI3 禁用与六行布局

- Query: 将既有 11 类图形扩为拟定 36 类；新增 25 类仅展示并禁用；定位公共 UI3 绘制、动画、输入、SVG、i18n 与离屏测试的最小接线。
- Scope: internal；只读业务源码，仅写本任务 research/。用户已批准建任务与规划，尚未批准实施。
- Date: 2026-10-17
- Baseline: 主会话记录前置修正已提交 384c5838；本研究未执行任何 Git 操作、构建、GUI 或业务改动。

## Findings

### 1. 已有文件与模块边界

| 路径 | 用途及直接证据 |
| --- | --- |
| `.trellis/workflow.md` | 规划和正式实施的审核门；研究只持久化结论，不激活实施任务。 |
| `.trellis/spec/native-desktop/index.md` | 原生桌面层总入口、最小改动/线程和平台约束。 |
| `.trellis/spec/native-desktop/rendering-and-ui.md:1961` | P2 图形子树合同：私有类别 list、独立属性 list、5 DIP、成功呈现命中及隐藏休眠。 |
| `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:270` | zh-CN 基准、sync/check、生成头、嵌入资源生命周期。 |
| `.trellis/spec/native-desktop/cpp-conventions.md` | C++ module 边界、快照及 revision、编码/换行。 |
| `.trellis/spec/native-desktop/pen-width.md` | 图形仅无数字斜向三档，不借用精调/色板子树。 |
| `.trellis/spec/native-desktop/shapes.md` | P2 的 11 类真实 PresetKind/ToolSpec 与构造、历史/UInk 边界。 |
| `Inkeys/Inkeys/UI/Bar/Bar.GeometryAttribute.cpp` | 私有类别/属性拓扑、参数、父面板、轮播、绘制、输入和动作分派。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm:352` | GeometryAttributePresentation、面板按钮与缓存数组声明。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:1883` | 公共禁用/按压/选中视觉；共享绘制、动画、命中和列表解析。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm:6` | 5 DIP 间隙、32.5/70 DIP 按钮、13 DIP 类别文字、0.30 禁用内容透明度。 |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttributeMotion.cppm:31` | 现有 FitEraserSurfacePose 仅平移约束，缩放由调用方负责。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:6913` | 生产帧推进 Geometry 面板，并复用现有 dirty 路径。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:10389`、`:11863` | 生产 Draw、成功呈现后 CommitPresented。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:3967`、`:7061` | 键盘及指针生产分发入口。 |
| `Inkeys/Inkeys/UI/Bar/Bar.GeometryAttribute.Test.cpp` | 无 HWND 独立 raster 和完整生产 ULW 场景验证。 |
| `Inkeys/IdtMain.cpp:1242` | `--ui3-geometry-attribute-test` 早退无窗口测试入口。 |
| `Inkeys/Inkeys.rc:318` | 当前图形 SVG 以名称型 UI 资源嵌入。 |
| `Inkeys/src/UI/barShapeEllipse.svg`、`barShapePolygon5.svg` | 24×24、2.6 主描边、圆角端点、语义颜色占位的现有实例。 |
| `Inkeys/Inkeys/UI/Bar/Bar.UI.cppm:334`、`Bar.UI.cpp:516` | rgba(10,0,7,0)/rgba(9,0,2,0) 分别映射 color1/color2。 |
| `Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc:121` | GeometryAttributes 文案节点。 |
| `Scripts/i18n.ps1:4`、`:486`、`:719` | check/sync、生成 IdtI18nKeys.g.h 与 snapshot；本研究不运行会写入的 sync。 |

### 2. 目录应与业务工具分离，不扩充假业务枚举

现有类别数组只有 11 个真实 `PresetKind`（GeometryAttribute.cpp:24）；资源和标签分别由 ShapeToolIconResource (:83) 与 ShapeToolLabel (:103) 接收真实业务类型。类别当前 0–10，圆模式 11–12，边数 13–22，样式 23–25，线型 26–29，粗细 30–32，关闭 33。

最小方案是在 Bar 层建立只读 UI 目录，项包含稳定 UI 标识、标签 key/资源名和 optional PresetKind。11 类有真实映射；新增 25 类无映射。不要给双曲线等补空业务枚举、改 Shape 核心或映射成直线。既有 ShapeToolIconResource/ShapeToolLabel 可保持真实工具接口，由目录入口只对已实现项调用；圆双模式与正多边形轮播继续特殊化既有目录项。

拟定 25 项为：折线、多边形、圆弧、扇形、角度、平行线、垂直线、长方体、正方体、圆柱、圆锥、棱柱、棱锥、数轴、平面直角坐标系、网格、坐标点、抛物线、反比例函数、双曲线、正弦、余弦、正切、指数、对数。它们的 UI 目录键不进入文档、UInk、ToolSpec 或 StateMode。

用类别数量派生索引：36 类时圆模式 36–37、边数 38–47、样式 48–50、线型 51–54、粗细 55–57、关闭 58，总 59 控件。同步 GeometryAttributePresentation 的 items/hits、buttons_、resourceTargets_/labelTargets_、categoryFontSizes_ 以及本地 slots/AttributeItems；不得只改 CategoryCount 而保留旧 34/11 数组。建议用同一组局部/模块常量表达这些现有索引，而不是另建控件框架。

### 3. 公共 UI3 禁用机制确实存在，但面板当前有三处会覆盖它

直接确认的共享机制：

- Bar.Button.cpp:1883 `RetargetBarFixedButtonVisual`，:1913 以 enabled=false 设置 `BarButtonDisabledContentOpacity`，统一值为 0.30；图标、文字都消费同一当前透明度。
- :1928 `RetargetBarButtonInteractionVisual` 是 BarButtonClass 的公共包装，可直接复用，无须自绘灰色图标或新 opacity 时钟。
- :2327 `ResolveBarButtonHitSnapshot` 由 `button.state->state != Disable` 派生 hit.enabled；:2335 `HitTestBarButton` 拒绝 disabled/hidden。
- :2138、:2291、:2056 分别为公共 Configure、Advance、Draw；禁用类仍应通过相同标准 twoTwo 布局、绘制和设备缓存路径。

Geometry 面板目前全项可用，因此以下三处必须一并改为目录资格，而非只设 Disable：

1. GeometryAttribute.cpp:405 当前把每项 state 覆写为 Selected/None；新增无映射类别应为 Disable 且永不 selected。
2. :407 当前将 `RetargetBarButtonInteractionVisual(..., true, ...)` 的 enabled 固定为 true；应传目录可执行性。初始化 :408 附近又将 icon/name pct 强制写为 wanted?1:0，也要采用相同禁用透明度，避免首帧禁用项闪成完整亮度。
3. :443 在公共 hit 解析后直接重写 enabled，覆盖 Disable 结果；应保留公共资格，再与父/本行门禁、wanted 条件相与。禁用图标 0.30 大于当前 0.01 阈值，不能拿“有可见透明度”当可执行资格。

同时将 pressed 与 Advance 的 hoverAllowed 与可用性相与，避免残留按压/悬停动画；仍然渲染占位，不将 hide=true。没有业务资格的按下既不产生 press，也不改变当前真实图形。面板 Pointer 返回 inside 阻挡底层输入属于正确行为，不能因为内部 hit=false 让禁用图标区域穿透成画布落笔。

### 4. 鼠标、键盘及动作入口需分别封口

- Pointer (:590) 消费 PresentationSnapshot，内部经 HitTestBarButton，只要上面公共 enabled 没被重写即可自然跳过禁用类别。鼠标/笔/触摸由现有同一入口处理。
- Keyboard (:622) 的 Tab/左右箭头已跳过 !visible/!enabled，Enter/Space 已检查当前已呈现资格，可继续复用；新增测试确保多次导航永远不会停在新增 25 项。
- Execute (:577) 目前对 `item < CategoryCount` 直接取 Categories[item] 并调用 setter。必须自身验证非负索引、目录映射和可用性，无映射立即返回；不能仅相信 UI hit，不能映射到默认 Line。拒绝分支不调用业务 setter，也不触发成功动作反馈。
- 成功绘制预设仍用 SetShapeToolPresetIfRevision；Down 抓 revision/Up 校验既有行为保留。未来类别点击不得改变 tool、style、width、color、revision；禁用项不能保存成选中态。

### 5. 独立 list、动画与底部 5 DIP 保持现有合同

- 私有 categoryList_ 在 Initialize (:154) 一次 Replace，GeometryGrid (:155) 解析；公共 parser Bar.Button.cpp:2367 已支持任意数量可见 twoTwo 项、最多六列，36 项自然为六行。不需要改公共 parser 或 More/GroupedGrid。
- attributeList_ (:180)、SingleRow、两枚 divider 和 attributeX_ 是独立拓扑。保留样式/分隔/线型/分隔/粗细的顺序与现有局部重排、退场透明度，不把类别扩容和属性列表合并。
- GeometryAttribute.cpp:320 用 `attributeTop = panel.bottom - g - smallSide`；5 DIP 在当前 pose/fit 后成为 5 × scale，不能又从旧两行 grid 常量计算尾部坐标。
- :444 起属性与参数行在位置/显隐未稳时单独禁用；类别能继续反向重定向动画。保留此策略，不为占位类别再加动画时钟。
- :245 左右的文本度量缓存按文本/字体/语言/设备代次失效，SVG 仅真实内容变更才 TransitionContent。扩成 59 项后同样不能逐帧测字、读 SVG 或重复换图。
- :481 轮播条件保持只针对真实正多边形；没有必要让未来工具加入轮播唤醒。

### 6. 六列六行的实际尺寸与现有 fit 能力边界

由当前 Metrics 与 GeometryAttribute.cpp:248–252 直接代入：

| 状态 | 类别网格 | 完整面板未缩放大小 |
| --- | --- | --- |
| 11 类，无参数 | 445×145 DIP | 492.5×198.5 DIP |
| 11 类，圆/正多边形参数 | 445×145 DIP | 492.5×242 DIP |
| 36 类，无参数 | 445×445 DIP | 492.5×498.5 DIP |
| 36 类，圆/正多边形参数 | 445×445 DIP | 492.5×542 DIP |

现有 fit (:291) 为 min(1, wholeWorkWidth/currentWidth, wholeWorkHeight/currentHeight)，其后 (:297) 仍以整个 work 调 FitEraserSurfacePose。它只保证稳态落在完整工作区，**没有为主栏或上/下半区预留空间**；当大面板被工作区钳位时可能覆盖主栏。不能宣称沿用旧 fit 已解决主栏避让。

460×420 DIP 工作区中，36 类无参数 fit≈0.8425、带参数 fit≈0.7749；70 DIP 类别实际约58.98/54.24 DIP，32.5 DIP 属性约27.38/25.18 DIP，13 DIP 字号上限约10.95/10.07 DIP。长英文还会按 66 DIP 可用槽缩小；75% 设备/栏倍率下物理像素更少。现有文本宽度断言仅证明未裁剪，不能证明可读。正常 1200×800 DIP 可保持原尺寸。

因此保留当前 fit 是改动最小的基线，可形成全目录与正确坐标，但小屏可读性、完整像素及主栏可达必须列为实际离屏/人工验收项，不能把“未超工作区”当成“未遮挡主栏”。Back 回弹过程中姿态还可能短暂超出稳态包络；不要通过裁死公共曲线假装解决。

### 7. 可选局部避让方案与明确取舍（待审核，不属于已批准实现）

只在 Geometry 的布局调用处构造两个候选区域：above=[work.top, main.top−10 DIP]、below=[main.bottom+10 DIP, work.bottom]，横向仍为完整工作区。优先现有展开侧，放不下时选空间更大的侧，再把所选区域用于现有 fit 和 FitEraserSurfacePose。沿用现有 motion_/sideSwitch_ 过中点换边；相等保留当前侧、dragPlacementLocked 时冻结选择，避免拖动与临界点左右抖动。公共算法无需变更，也不增加滚动/分类系统。

但这种避让会进一步缩小所有控件。例如 460×420 DIP、80 DIP 高主栏中心距边80 DIP时，更大半区约290 DIP；542 DIP 面板缩成 fit≈0.535，类别约37.45 DIP、13号文字上限约6.96 DIP、属性约17.39 DIP。主栏处于屏幕中央时半区约160 DIP，类别约20.66 DIP、文字上限约3.84 DIP、属性约9.59 DIP。长英文/禁用 0.30 透明度使可读性风险更明显。

需要用户/主会话在审核中明确：沿用全工作区 fit 保较大的控件并接受小屏局部遮挡；或局部半区 fit 严格避让但接受极端缩小并定义可读下限。若要求“36 类全显、固定六列、极小工作区仍清楚、严格避让主栏”同时成立，简单等比 fit 无法保证，需另行批准分页/滚动等交互范围变化。研究不替用户选取，也不暗自实现新框架。

### 8. SVG、资源与国际化的最小接线

- 每个新增类别一份 `Inkeys/src/UI/barShape*.svg`，沿用 24×24 viewBox、2.6 主描边与 round linecap/linejoin；坐标轴/渐近线/空间隐藏边可用更细辅助线，不能改成程序自绘类别图标。
- 主题主色用 `rgba(10,0,7,0)`，次色用 `rgba(9,0,2,0)`。禁用效果由公共 pct 得到，SVG 内不要硬编码灰色/禁用 alpha。
- 当前 P2 SVG 已由 `Inkeys.rc:318–372` 的名称型 `UI` 资源登记；不会使用数值 shader ID。现有 shape SVG 没有单独 vcxproj None 项，可沿其同一路径只登记 rc；不因便利而扩大项目文件变更。
- 标签加在三语 GeometryAttributes 同名路径，以短英文避免过度缩字；zh-CN 为基准，实施时运行现有 `pwsh ./Scripts/i18n.ps1 sync` 后补翻译并 `check`。IdtI18nKeys.g.h 及 snapshot 由工具生成，不手工改头。
- `Bar.GeometryAttribute.cpp:489` 在 renderer device generation 变化时重置私有按钮 SVG cache；扩容后的所有项仍走此循环。成功资源读取和实际 raster bitmap 要在测试中确认，而非只做 XML 解析。

### 9. 已有离屏测试需扩展的精确点

沿用 `Inkeys.exe --ui3-geometry-attribute-test`，不新建测试系统或 HWND：

1. Test.cpp:165–184 的 list.size()==11、totalRows==4、items.size()==34、关闭索引33和 subset 1/6/7/11，改为目录/派生索引，新增 12/35/36 wrap；确认36项=12 subColumns/12 totalRows，6×6 row-major。
2. :277、:759 的 SVG cache 检查扩为完整36目录，确认25新增资源正式嵌入并能 raster；所有三语/明暗/设备重建都保持可见。
3. :500 的 checkStableScene 当前对所有 shown 项都断言 HitTest==true (:529)，须区分“可见”和“可用”：未来25项 rect/hit.visible 为真，但 enabled=false、中心命中=false、state=Disable、共享内容终值0.30。不能为了通过测试将灰色当隐藏，也不能要求禁用项有完整前景像素。
4. :595 起原1584配置矩阵（11真实预设×3样式×3语×4scale×2workspace×2side）仍保留11真实业务选择；每种场景另检查全部36目录。不要向 StateMode setter 传不存在的第12–36 PresetKind。
5. 新增逐个未来25项的 mouse/contact Down/Up、move、双击、取消；核对完整业务快照/revision均不变，pressScale保持1、无hover成功反馈、当前可用类别仍选中；直接 Execute 测试也必须拒绝。面板应消费内部背景避免画布穿透。
6. TestAccess 已在 :34 提供私有状态读取，可最小增加只供测试的动作/聚焦入口。Tab/左右/ShiftTab 多轮跳过禁用，Enter/Space无法激活强制旧焦点；原11工具和关闭仍可执行。
7. :481 的 rowGeometry 已逐成功帧断言底部5×pose.scale和真实 ULW crop。扩索引后完整保留；:644 以后七组参数/属性重排、失败提交旧快照、旧revision/capture取消、资源重建、idle/hidden无唤醒继续跑。
8. 小工作区全场景检查全部目录实际 ULW源crop、主栏可见/可达和字号/可读性证据；如审批选择局部避让则新增面板与主栏不相交、两侧候选、换边/拖动锁/高度动画中布局门禁测试。现有 :507 只检查整体面板不超工作区，不能证明这些新性质。
9. :775 起主栏标签切换仍仅11真实工具；未来项不可成为主栏当前标签。保留共享动画/dirty像素覆盖/失败事务证据，不能以源码字符串或复制公式替代生产调用。

## External References

无。此次为本地已给仓库代码的布局/接口规划，不涉及新库/API或远程内容，不需要网络调查。未推断 Win7/Win10/实体笔兼容验证已经通过。

## Related Specs

`.trellis/spec/native-desktop/rendering-and-ui.md` 的公共按钮、多目标呈现、P2末节；`configuration-i18n-and-assets.md` 的国际化生成链与资源生命周期；`cpp-conventions.md` 的最小变更及快照/revision；`pen-width.md` 的斜向三预设；`shapes.md` 的真实工具与持久化边界。

## Caveats / Not Found

- 本研究只确认源码和数学尺寸，未运行构建、测试或读取本轮尚不存在的36项截图，不能当作验收通过。
- 当前公共UI3禁用能力足够，缺口在 Geometry 面板覆盖其资格；不需要新 disabled 框架。
- 当前 fit 不避让主栏，六行后的极小工作区可读性与避让存在取舍，必须在设计审核/验收中明确。
- 本地查找未发现独立 trellis-start/SKILL.md；研究使用完整注入的 Active task 与 research role，并已读取 workflow、spec index 及相关规范，没有自行读取其他会话 active pointer 或 implement/check jsonl。
- 36项按钮目录是待审核清单；本研究没有批准进入实施，也没有改动业务、其他任务或spec。
