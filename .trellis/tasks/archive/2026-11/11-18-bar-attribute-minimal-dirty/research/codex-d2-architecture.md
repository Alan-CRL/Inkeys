**结论【推断】：统一架构可行，但需要补上“绘制前准备帧状态”和“按呈现候选提交摘要”两项合同。**仅扫描现有对象字段仍会漏掉绘制阶段派生的状态；仅在提交成功时保存“最新摘要”，则会破坏分页的异步回执语义。

本轮只读文件，未修改文件，未运行 git、构建、测试或 GUI。以下路径简写：`Bar/`、`UI3/`、`PageControl/` 均位于 `Inkeys/Inkeys/UI/`。D1 指 `.trellis/tasks/11-18-bar-attribute-minimal-dirty/research/codex-d1-investigate.md`；其已确认的按钮交互清单、包络基础、局部合成结论直接引用，不重复展开。D1 §7 的局部迁移建议已被新 PRD R1–R8 取代，撤回属性现已明确纳入。

## 1. 两个宿主的帧流程

| 宿主 | 【事实】当前顺序与证据 | 最早可扫描位置 |
|---|---|---|
| 主栏 | 显示转换、`BeginFontFrame` → `SubmitTargetsAndLayout` → `AdvanceAnimationsAndDeriveLayout` → `PrepareLightingAndDemand` → `CalculateDirtyAndDrawPresent`。`Bar/Bar.RenderLoop.cpp:12480–12567` | 动画结束仍不够：后者内部还同步根继承、反馈几何及按钮继承。见 `:9365–9370、9597–9641、9996–10019` |
| 主栏脏区与呈现 | `BeginFrame` → Observe/包络与容量计算 → `ResolveDamage` → 调试合并/裁剪 → `FrameTransaction` → 绘制 → 提交 → 成功后 `CommitPresented`。`:9648、10216–10448、11129、11304、11556、11642–11647` | 【建议】将准备逻辑移到呈现需求判断之前；在所有继承、派生视觉和坐标映射完成后扫描 |
| 分页 | 配置内容与 bounds → `scene.Advance` → `PresentationBounds`/backing → `PendingDamage` → `FrameTransaction` → `scene.Render` → 提交 → 原生回执核对 → `CommitPresentationCandidate`。`PageControl/PageControl.cpp:2674–2709、1590–1643、2445–2476` | 【建议】最后一次 bounds/scale 更新并 `RefreshGeometry` 后、读取 damage 前；先消除下表的晚绑定 |

**当前不存在统一的“绘制状态全部确定”边界：**

| 【事实】绘制阶段仍做的事情 | 证据 | 架构要求 |
|---|---|---|
| 按钮重新解析继承；badge 改 SVG 几何/颜色；PNG 从 SVG 控制器复制字段 | `UI3/UI3.Button.cpp:385–398、442–445、460–471` | 摘要直接使用最终派生结果，或提前准备后让绘制复用 |
| 主栏三档数字修改内容、颜色、`contentPct`；popup 临时修改文字尺寸后恢复 | `Bar/Bar.RenderLoop.cpp:8424–8443、8807–8879` | 不能扫描临时修改前的 Word 就认为已覆盖 |
| FineDial 绘制时计算几何，并写 `thicknessFineDialVisualWidth` | `Bar/Bar.RenderLoop.cpp:7982–8015` | 抽出同源准备结果；避免扫描与绘制分别推导 |
| 分页 `Render()` 再调 `Advance(now)`，绘制时摆拖柄、设置 idle/content opacity | `Bar/Bar.Scene.cpp:1042、1062–1075` | 同时间戳通常短路，但短路前仍检查字号策略；需明确一次准备、只读绘制合同。`:627–629` |
| 字号修正在绘制中读取 DC 变换/DPI并推进 correction | `UI3/UI3.Renderer.cpp:192–225` | 准备阶段解析实际字号，或纳入等价的 correction 状态并确保每帧只推进一次 |
| `pct`、尺寸、enable、内容透明度决定跳过绘制 | `UI3/UI3.Renderer.cpp:1866–1873、2330–2335` | 可见性必须采用真实绘制门槛；Shape 的 `pct==0` 仍可能有 `frameLightPct` |

底栏映射不是统一平移：`Body`、`Grip`、`Rigid`、`None` 有不同包络映射，绘制还改变字体目标尺度。证据：`Bar/Bar.RenderLoop.cpp:7286–7314、9730–9754`。**必须保留变换类别，不能只登记一个面板原点。**

## 2. 图元状态清单

以下是摘要必须覆盖的**有效绘制输入**；目标值通常不入摘要，但参与字号拟合、继承和条件分支的目标值例外。

| 类型 | 【事实】影响像素的状态 | 证据 |
|---|---|---|
| 公共几何 | 传入继承原点、`w/h.val`、`pct.val`、`enable.val`；`x/y` 经继承解析后生效。无参 `Inherit()` 使用 `w/h.tar`，不能一概忽略目标值 | `UI3/UI3.Primitive.cppm:184–201` |
| `UiShapeClass` | `rw/rh/ft` 的存在性和值，fill/frame、framePct、**frameLightPct**；`lightMaterial`、`themeSurface`、`lightFillOpacityScale`、`residentSurface`；frameRendering、frameLightColor、frameLightOpacitySource、framePrimaryLightEnabled、两种 cursor intensity scale | `UI3/UI3.Primitive.cppm:243–280`；实际消费 `UI3/UI3.Renderer.cpp:1881–1978` |
| `UiSuperellipseClass` | 公共项、`n/ft`、fill/frame/framePct，以及上述材质和边缘光配置；**该类型没有 `frameLightPct` 字段** | `UI3/UI3.Primitive.cppm:283–320` |
| `UiSVGClass` | 当前 svg 内容、color1/2 存在性和值、angle、contentScale/contentPct、appearanceKey、documentPreprocess 的输出版本 | `UI3/UI3.Primitive.cppm:350–401` |
| `UiPNGClass` | 当前图像载荷、目标几何、angle、pct/enable；原始尺寸。`cacheBitmap` 地址不足以表示源图内容版本 | `UI3/UI3.Primitive.cppm:405–436` |
| `UiWordClass` | 当前 content、size/color、contentScale/contentPct、fontTargetSize、fontCorrection；调用参数 fontWeight、textAlign、fitButtonLabel；拟合还用 `w/h.tar`、`size.tar` | `UI3/UI3.Primitive.cppm:454–480`；`UI3/UI3.Renderer.cpp:2327–2370` |
| `ButtonClass` | 背景/图标/文字、iconKind、pressScale；badge 的 progress/below/angle/scale及 SVG 内容；secondary word；separator、drawIcon、字体参数、underlay/content 输入及包络 | `UI3/UI3.Button.cppm:64–128、192–225`；`UI3/UI3.Button.cpp:436–485` |
| 组/绘制上下文 | 最终 transform、zoom、fontTargetZoom/Scale、diffuseReferenceZoom/GeometryScale、继承原点、opacity、clip、绘制顺序 | `UI3/UI3.Panel.cppm:56–79`；分页组透明层 `Bar/Bar.Scene.cpp:792–812` |

**【建议】对象自身的光效响应参数仍入摘要。**R5 排除的是主光/鼠标光的位置、颜色、强度等动态光源状态；若连 `frameLightPct` 或 lightMaterial 都排除，悬停边光变化可能漏刷。

| 绘制时环境输入 | 处理建议 |
|---|---|
| 用户背景透明度覆盖：Shape 绘制中调用 `GetThemeResidentBackgroundOpacityOverride()` | 每帧冻结一次，挂到 resident surface 环境组。`UI3/UI3.Renderer.cpp:1919–1931` |
| 常驻内容透明度：主栏、Logo、分页绘制时读取 | 单独的 resident content 环境组；不要让浮层一起失效。`Bar/Bar.RenderLoop.cpp:8534、8610`；`Bar/Bar.Scene.cpp:1075–1078` |
| PPT idleFade、材质权重 | 作为受影响组的有效参数；不能只比较图元 pct。`UI3/UI3.Renderer.cpp:2178–2180、2369–2370` |
| 主题色 | 标准图元已解析成颜色的比较最终颜色；自绘直接调用 `GetThemeColor` 的，冻结其实际色值/主题版本 |
| 字体族、locale、字号策略、DPI/单位模式 | 字体环境版本与有效字号参与摘要。`UI3/UI3.Renderer.cpp:205–237` |
| 动态光照 | 沿用原通道；登记表须向它提供完整、正确映射的响应对象范围 |

**风险：**不能 hash 整个类内存：mutex、动画内部状态、缓存、padding、日志闩锁都不是视觉摘要。SVG/PNG 建议增加内容版本；现有字段不能假定已有通用版本机制。

## 3. 自绘内容清单

“自绘对象”应覆盖完整视觉单元，也包括内部调用标准图元、但依赖临时状态的复合绘制。

| 内容 | 【事实】位置 | 摘要输入来源 |
|---|---|---|
| 粗细预览、激光内外芯、slider 轨道 | `Bar/Bar.RenderLoop.cpp:7574–7941` | previewGeometry、笔宽/morph、laser 状态与 shell/core/outer 动画、slider progress、颜色/透明度、面板尺度 |
| FineDial、刻度与标签、选择器 | `Bar/Bar.RenderLoop.cpp:7950–8343` | progress、识别/驻留/选择动画、候选宽度、旧新量程及 opacity、面板几何、字体/主题 |
| 三档粗细圆点与数字 | `Bar/Bar.RenderLoop.cpp:8420–8479` | presetIndex、锁存数值、circle diameter、number progress、颜色/opacity、笔型 |
| thumb、预览数字浮窗 | `Bar/Bar.RenderLoop.cpp:9054–9126` | popupScale/anchor/number rect；thumb 几何、accent opacity、center diameter、thumb scale、材质 |
| 橡皮三档位图预览 | `Bar/Bar.EraserAttribute.cpp:395–435` | shape、pattern、theme、diameter/DPI、preview clip、circlePress、hover/selection ring |
| 形状粗细斜胶囊、类别长按环 | `Bar/Bar.GeometryAttribute.cpp:901–932` | thicknessPreviewWidths、pressed、按钮颜色/opacity、pose scale；ringItem/progress/opacity、失败闩锁 |
| 状态指示器 | `Bar/Bar.StatusIndicator.cpp:450–541` | layout entries及顺序、遮挡关系、model/spec文字、hover/press、accent、文本滚动/渐隐/clip、材质和常驻透明度 |
| 主 Logo | `Bar/Bar.RenderLoop.cpp:4733–4744、8606–8612` | **主要是标准 SVG＋appearanceKey/documentPreprocess**，不是独立裸 D2D 图形；另有笔色、drawingPct、Grip 变换 |
| 底栏吸附指示 | `Bar/Bar.RenderLoop.cpp:9432–9466、9652–9656、9135` | target/progress、几何、峰值预留包络、首次显现状态；应整体登记 |
| 取色器色盘、明暗覆盖、marker、关闭符号、保持环 | `UI3/UI3.ColorPicker.cpp:608–695、745–747、783–788` | palette 几何、tone/display颜色、marker坐标、opacity/scale、hold进度和主题色 |
| 粗细保持环、构造提示环 | `Bar/Bar.RenderLoop.cpp:8968–9027`；`Bar/Bar.ConstructionPrompt.cpp:273–282` | hold progress/lock opacity/group scale；ringProgress、内容opacity、主题色 |
| 分页主题色样、网格/点阵 | `Bar/Bar.Scene.cpp:765–775` | previewBackgrounds、backgroundRgb、pattern类型、按钮按压和组pose |
| 分页加载环 | `Bar/Bar.Scene.cpp:779–787` | indicator phase/Ring、动画开关、按钮圆角/几何、主题色、ringFailed |
| 页码、总页码、标题、未保存文字、拖柄 | `Bar/Bar.Scene.cpp:763、807、1070–1077` | 前四者主要走标准 Word；拖柄走 Shape但几何在 Render 中派生，均应提前准备 |
| 分页背景扩展回调 | `Bar/Bar.Scene.cpp:1064–1066` | `background.drawContent` 无法自动推断输入；调用方须提供内容版本/摘要及包络 |

**风险：**状态指示器临时创建 Shape、复用图标对象、按 key 创建 Word。不能把这些临时/共享绘制载体的地址当作每个可见实例身份；建议按稳定 entry 对象或稳定登记 token 注册复合项。

## 4. 手工脏区清单与可删除性

D1 §1–4 已确认的对象/面板标脏位置沿用。迁移应按**责任**删除，不能按 `MarkChanged` 字样批量清理。

| 现有代码类别与范围 | 迁移判断 |
|---|---|
| 动画 helper 的带键 `ChangeValue/ChangePct/ChangeColor`、固定图元 observe：`Bar/Bar.RenderLoop.cpp:4650–4872` | 【可删】标脏参数和副作用；保留动画推进、changed/active、调度判断 |
| map 图元、按钮逐对象 `MarkChanged`：`:4888–5165` | 【可删】由最终状态比较接管；保留动画和布局 |
| 底栏映射、popup、材质相关组键：`:6580–6600、6815–6888` | 【条件可删】组状态必须覆盖真实映射与材质变化 |
| 形状/橡皮/撤回/构造提示/状态指示组键：`:6929–7013`；整组 Observe `:10412–10448` | 【可删】换为成员/自绘登记；光照所需范围计算仍保留 |
| sustain 拖动的四组键：`:7121–7131` | 【条件可删】直接写坐标与变换已被扫描；sustain/唤醒功能保留 |
| 逐图元/按钮 Observe：`:10216–10390` | 【可删】宿主重复登记；迁入统一适配器。不要顺带删掉光照范围累加 |
| Primary/Cursor 键：`:7133–7141`，影响范围 `:10021–10090、10415–10439` | 【保留】R5 |
| `IncludeDamage` 首次指示峰值：`:9652–9656` | 【先保留】普通当前包络在零缩放帧无法替代峰值要求；日后可收进自绘对象专用合同 |
| 全脏、未知分类：`:7143–7148、9648–9651` | 【保留并调整时序】已覆盖的变化不能继续触发“未知”全脏 |
| 坐标重基准、失败重试：`:12262、11019、11591、11932` | 【保留】登记表摘要/包络坐标也须同步重基准 |
| 分页 `Include/IncludeButton`、内容变化及动画累计：`Bar/Bar.Scene.cpp:267–302、378–426、621–622、650–656、730–736` | 【可替换】标准对象、组和自绘对象接管 |
| 分页光照 damage：`Bar/Bar.Scene.cpp:578–607` | 【保留】当前分页不是两条 DirtyVisualKey，不能假称已有相同键机制 |
| 分页 `PendingDamage/ConsumeDamage`：`:918–919` | 【可保留接口】内部转接统一机制；消费语义必须绑定候选 |
| 分页 full replacement、fallback、调试、窗口回执：`PageControl/PageControl.cpp:1590–1634、2450–2481` | 【保留】属于目标与呈现事务 |

## 5. 翻页控件的呈现结构

| 项目 | 【事实】 |
|---|---|
| 窗口/目标 | 四组 Client/WindowRole：PptBottomLeft、PptBottomRight、PptMiddleLeft、PptMiddleRight；各自 `SurfaceState` 持有 Scene、稳定地址的 FrameTarget。`PageControl/PageControl.cpp:225–264` |
| Tracker | Scene 使用自己的 `RECT damage`、revision及 Include；引用 `DirtyRegionTracker` 的矩形工具，**不等于持有 tracker 实例**。`Bar/Bar.Scene.cpp:263–274、918–919、949–973` |
| 坐标 | Include 将逻辑像素范围偏移 `logical.left/top - presentation.left/top`，裁到 presentation 客户区。`:267–271`；绘制使用同源位移。`:752–758` |
| 提交 | 每目标单 RECT 脏区裁剪、透明清空、提交；forceFullReplacement 可覆盖整个 presentation。`PageControl/PageControl.cpp:1590–1643` |
| 成功门槛 | 原生窗口身份、revision、bounds等回执被接受，再确认 sceneSerial；匹配候选才消费相应 damage。`:2445–2476`；`Bar/Bar.Scene.cpp:962–973` |

主栏则先在布局像素空间追踪，再映射 capacity/source/viewport；分页主要直接追踪 presentation 客户区像素。证据：`Bar/Bar.RenderLoop.cpp:9643–9648、11129–11142`。

**风险：**presentation 原点变化会重解释旧像素；四目标必须各有成功摘要，不能共享一个对象地址对应的全局提交记录。旧 ACK、隐藏/显示、target 重建、scale变化均须保留原来的 full replacement 与候选核对路径。

## 6. 窗口容量与预留包络

| 【事实】现有责任 | 证据 | R7 要求 |
|---|---|---|
| `collectVisibleContentBoundsForWindowSizing` 由布局、动画、面板 Changed 等决定是否重算范围 | `Bar/Bar.RenderLoop.cpp:9668–9711` | 不以“本帧摘要变化”替代 |
| visibleContentBounds 与 Body/Grip/Rigid 映射同时收集 | `:9756–9763、9796–9913` | 保持既有可见性、padding与变换 |
| predictedEnvelope 预留未来转场范围 | `:10582–10974` | 不改成当前脏区或当前对象包络 |
| viewport 使用 currentContentBounds＋predictedEnvelope＋布局/拖动约束 | `:10976–11013` | 原控制器、capacity及固定拖动表面保持 |
| 状态小图标允许被窗口裁切，不决定扩窗 | 父任务 `implement.md:164`；实际 overflow 处理 `Bar/Bar.RenderLoop.cpp:9957–9958` | 登记进 damage不代表登记进 sizing |

【建议】保留两条独立消费者：**damage 比较当前与成功状态；sizing 继续使用原范围与预测策略。**删除旧标脏时不能删除供 sizing/调度使用的 `Changed()/Active()`。

## 7. 测试影响

| 测试 | 【事实】现有断言 | 迁移处理 |
|---|---|---|
| `InkeysHeadlessTests/dirty_region_tests.cpp:102–324` | 旧新并集、重基准、出现消失、重试、选择性观察、组损伤 | tracker 不变则原预期应保持；新增 registry 测试，不用改旧测试掩盖回归 |
| 同文件 `:329–394` | 光照边带、缩放、debug范围 | 应保持 |
| `Bar/Bar.Presentation.Test.cpp` | 本轮全文检索未发现 dirty/MarkChanged 相关断言；D1 §6 亦无逐按钮上限证据 | 保留现有呈现测试，另加真实提交矩形覆盖 |
| `Bar/Bar.MultiTarget.Test.cpp:1168–1171、1669–1670、1826–1830` | full replacement、prcDirty捕获、dirty须在提交source内 | 保持语义；新增逐交互上限，不能固定要求新旧矩形完全相等 |
| `Bar/Bar.EraserAttribute.Test.cpp:2862–2916` | 独立构造 dirty，partial/full BGRA一致及实际剔除 | 原测试保持；它尚未验证自动摘要是否选对 dirty |
| `PageControl/PageControl.cpp:3992–4005` | stale/duplicate ACK、捕获后失效保留 | 必须保持，并扩展到摘要快照 |
| 同文件 `:4593–4610` | 失败保留、同批不重复推进、页码 damage 上限 | 上限保持；`:4607` 在 Configure 后立即取 damage，自动扫描后应调整采样时点 |
| 同文件 `:4252–4266、4714–4715、4860–4886` | partial保留、资源重建前局部damage、Advance后改bounds及晚订阅 | 重点回归 |
| golden | 入口 `Bar/Bar.Golden.Test.cpp:537`；新 PRD `:31` 要求35场景逐字节一致 | **不改像素预期**；本轮未执行 |

新增必要场景：直接写值但不启动动画、等值重复布局、重叠遮挡/层序变化、clip变化、地址复用、注销消失、失败后反向、旧 ACK 到达、字号 correction、自绘载荷更新。像素差集应独立于包络算法验证，沿用 D1 §6 的方法。

## 8. 摘要性能与正确性

| 问题 | 判断与建议 |
|---|---|
| 对象数量 | 【事实】形状控件索引公式得到70个控件槽，分类35个；`Bar/Bar.Main.cppm:323–332`。分页循环17个按钮；`Bar/Bar.Scene.cpp:711–731`。tracker预留256条只是容量提示，非实测总数；`UI3/UI3.DirtyRegion.cppm:35–38` |
| 扫描成本 | 【推断】主栏完整登记为数百图元量级，分页每目标数十项；运行时数量与耗时待计数，不能据此承诺预算 |
| 比较方式 | 优先固定大小、逐字段有效状态比较；内容用版本＋必要时值比较。哈希可加速，但不能仅靠有碰撞可能的哈希判定绝对相同 |
| 包络成本 | 每帧先准备摘要；仅变化项、组映射变化项及全量校准帧算包络。首次/全脏须观察全部有效可见项 |
| 浮点 | 相同输入的确定性重算应相等；先规范无效值、`-0`。不要对所有字段统一 epsilon：亚像素位置/alpha/字号变化也可能改像素 |
| 动画 | 实际状态每帧变就应每帧脏；`active==true` 本身不等于像素变。但 active仍决定续帧 |
| 可见性 | 必须检测已呈现→隐藏，不能只遍历当前可见对象；隐藏对象的旧包络要被擦除 |
| 线程 | 原子量逐个安全读取不保证一组状态一致。应在 render owner冻结输入，摘要和绘制消费同一快照 |

**额外关键风险：**当前主栏先 `ShouldPresent()`，才进入继承及脏区计算，且未知分类兜底更早执行。证据：`Bar/Bar.RenderLoop.cpp:7143–7154、9290、9597`。新扫描必须能反向产生呈现需求，否则直接写值仍可能被 idle 门控吞掉；同时不要增加静置轮询。

## 9. API 草案

以下名字全部是**建议新增**，并非现有符号。

```cpp
// 独立模块 Inkeys.UI.UI3.VisualDirty：无图元、D2D设备或产品依赖
struct VisualToken;    // 地址身份＋登记代次，防止地址复用
struct VisualGroup;
struct VisualSample;   // 有效可见性、可比较状态
struct VisualAttempt;  // 不可变候选摘要及观察集合

class VisualDirtyRegistry {
public:
    VisualGroup RegisterGroup(/* parent, state provider */);
    VisualToken Register(/* identity, group, mapping, sample, envelope */);
    void Unregister(VisualToken); // 保留成功旧包络，排队擦除

    VisualAttempt Prepare(/* frozen frame inputs, tracker */);
    void CommitPresented(/* attempt identity */);
    void RetainForRetry(/* attempt identity */);
    void TranslateCommitted(POINT);
};
```

| 合同 | 建议 |
|---|---|
| 模块划分 | 纯比较/生命周期/候选状态在独立模块，供 Headless 导入；Shape/Button等适配器放新增 `:VisualDirty` 分区，依赖现有 Primitive/Envelope/Button |
| 身份 | 每目标 registry；地址作为登记身份，但内部 token带代次。禁止对象移动后沿用旧登记；重复绘制同一载体须有实例身份 |
| 组 | 树形、禁止环；比较有效pose/opacity/clip/顺序。成员变化不传播到整个组；组自身背景也作为对象登记 |
| 坐标回调 | 接收冻结帧、局部包络，返回 tracker坐标包络；同一映射参数参与摘要，不能只提供无法比较的函数 |
| tracker衔接 | Prepare比较 committed摘要；变化项 `MarkChanged`，可见项 `Observe`；消失只标旧项。首次/全脏校准全部可见项 |
| 成功提交 | 摘要与tracker包络必须提交同一个 attempt；分页需捕获/确认候选扩展，不能仅调用现有“提交最新状态”的方法 |
| 失败与注销 | 不推进成功摘要；注销后留 tombstone至旧像素擦除成功，再释放记录 |
| 线程 | 登记/准备/确认均由 render owner执行；跨线程只发布输入或取消消息 |

宿主接入示意：

| 例子 | 一次登记及每帧准备 |
|---|---|
| 主栏按钮 | `RegisterButton(button, mainGroup, bodyMapping, options)`；UI3适配器覆盖背景、icon、word、badge、press及扩展内容 |
| 橡皮面板 | `RegisterGroup(panelIdentity, root, panelPose/opacity/clip)`；成员挂组，不再用 `EraserAttributeGroup`汇总按钮反馈 |
| 粗细预览 | `RegisterCustom(stablePreviewIdentity, drawGroup, rigidMapping, preparedPreviewState, previewEnvelope)`；绘制也消费该 preparedPreviewState |
| 分页按钮 | 注册到对应 Scene的registry；secondary为pageTotal；映射包含scale及logical−presentation位移；候选摘要随sceneSerial确认 |

【风险】现有 `DirtyRegionTracker::CommitPresented()`操作当前观察集合；见 `UI3/UI3.DirtyRegion.cppm:138–181`。要支持分页延迟 ACK，必须增加候选捕获语义，或保证 ACK等待期 tracker不被下一次准备覆盖。前者更明确。

## 10. 分阶段迁移建议

| 阶段 | 可验证结果与回退点 | 主要风险 |
|---|---|---|
| 1. 登记表及 Headless | 等值不脏、直接写值、组传播、消失/注销、失败、候选ACK、地址复用；不接管宿主 | 生命周期与候选语义 |
| 2. UI3准备适配器 | 先覆盖Button、badge/PNG、文字字号；同状态全幅BGRA保持一致 | 将绘制时派生提前后改变计算顺序 |
| 3. 形状/橡皮/撤回/构造提示 | 先并行计算新旧damage作诊断，再逐面板切换；逐按钮上限＋partial/full对照 | 自绘、clip、嵌套opacity、旧选中项擦除 |
| 4. 绘制属性与取色器 | 分别迁移预设按钮、预览/slider、FineDial、popup/hold、ColorPicker | 漏派生输入、共享载体、量程交叉淡入 |
| 5. 主栏/More/Logo/状态/吸附 | 验证Body/Grip/Rigid、拖动/反向/重基准；容量与viewport序列保持 | 自动扫描位于需求门控之后；状态图标误扩窗 |
| 6. 分页四目标 | 每目标独立registry，候选摘要随原生ACK提交；覆盖晚bounds、隐藏恢复、旧ACK和失败 | presentation原点、候选串代、错误消费新变化 |
| 7. 删除旧代码 | 删除已接管对象键/Include；设计列出光照、全脏、首次峰值、重基准、重试等例外 | 误删Changed/Active、sizing或调度用途 |

每阶段保留明确的宿主/面板切换点，避免同时修改两套绘制公式。最终验证仍须包含PRD规定的35 golden、真实提交矩形、partial/full逐像素对照及用户实机检查；**本轮只证明读码上的可行性与约束，未证明像素一致或性能通过。**