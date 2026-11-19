## 1. 参照实现

**事实：主要问题是变化报告粒度过粗。形状、橡皮已有独立按钮视觉，但宿主把它们的变化统一转换为面板组脏区。绘制属性则混用了逐对象登记与整组登记。**

下文文件名均指 `Inkeys/Inkeys/UI/Bar/`；`UI3.*` 位于 `UI/UI3/`，`PageControl.cpp` 位于 `UI/PageControl/`。本轮只读文件，未修改、未运行 git、构建或测试。

| 环节 | 读码确认 |
|---|---|
| 键身份 | `GetBarDirtyVisualKey(const void*)` 把视觉对象地址转换为键；另一个重载把 `BarDirtyFixedVisual` 转为固定组键。`Bar.RenderLoop.cpp:414–438` |
| 主栏、More 变化登记 | `AdvanceButtonVisual` 的 `context.observe` 收到各视觉部分的变化；`changed || active` 时对传入的 `visual` 调用 `MarkChanged`。主栏、More 的显式项和溢出项走同一入口。`Bar.RenderLoop.cpp:5124–5165` |
| 标准图元登记 | `FixedVisualContext` 同样按对象地址标记；Shape/SVG/Word 等分别推进。`Bar.RenderLoop.cpp:4650–4665、4903、4966、4998` |
| 当前包络观察 | `ObserveRegisteredButton` 分别观察背景、图标、文字、badge；PNG 共用 SVG icon 控制器身份。主栏使用 Body 变换，More 使用 Rigid。`Bar.RenderLoop.cpp:10348–10390` |
| 外扩 | `GetWeigetRect` 使用描边宽度；PointLight 再加 `6 DIP`，theme surface 至少包含阴影范围，最后加 `3` 设备像素抗锯齿余量。图像考虑旋转和 `contentScale`。`UI3.Envelope.cppm:21–59、80–126` |
| 按压 | `pressScale` 变化以按钮背景为身份报告；绘制围绕按钮中心缩放整个按钮。`UI3.Button.cpp:312–329、718–720` |
| 跨帧擦除 | `ResolveDamage` 合并变化键的上次成功包络和本帧包络；首次、强制全脏及无分类兜底可扩大范围；仅成功呈现后推进快照。`UI3.DirtyRegion.cppm:55–76、118–184` |
| 提交 | business damage → 调试覆盖层合并 → viewport 裁剪 → `FrameTransaction` → client 坐标 `prcDirty`。`Bar.RenderLoop.cpp:11129、11214–11246、11304、11455、11528` |

**包络限制：**主栏观察段主要观察未按压缩放的图元矩形，再加保守外扩；它没有在这里逐项应用 `pressScale`。绘制剔除包络则在按钮变换作用域内计算。不能直接认定复制这段观察代码就覆盖了所有自定义回弹峰值，尤其是外侧选中环、扩展回调。证据：`Bar.RenderLoop.cpp:937–989、10352–10373`；`UI3.Button.cpp:443–451`。

**翻页控件采用另一条链路，不使用这些键：**

`BarPageSurface` 接收 pointer 状态 → `UpdateFeedback` → `AdvanceButtonVisual` → 对变化按钮调用 `Include` → `PendingDamage()` → PageControl 裁剪并提交。`Include` 按描边＋点光扩散＋抗锯齿外扩，并转换到 presentation 坐标。证据：`Bar.Scene.cpp:52–54、267–274、299–302、710–731、918、1010–1023`；`PageControl.cpp:1590–1643`。

光照另走 Primary/Cursor 键及影响范围计算，不能把它造成的大矩形归为按钮登记失败。`Bar.RenderLoop.cpp:7133–7141、10060–10090、10415–10439`。

## 2. 绘制属性清单

“逐对象”指普通 Shape/SVG/Word 动画；“整组”指额外标记 `DrawAttributeGroup`。一次交互可能同时走两者。

| 控件／交互 | 当前粒度、触发位置 | 实际变化元素 |
|---|---|---|
| 11 个预设色：悬停 | 反馈 Shape 逐对象。`Bar.Interaction.cpp:4108–4115`；`Bar.RenderLoop.cpp:5007–5014` | 当前与退出悬停的反馈背景 |
| 预设色：按下、跨色拖动、释放 | `pressScale` 整组：`Bar.RenderLoop.cpp:4853–4854`；输入：`Bar.Interaction.cpp:7680–7755` | 色块、勾和反馈一起缩放；按下即改色，拖入其他色继续改色；旧／新选中勾、使用笔色的预览及光效也可能改变 |
| 自定义颜色入口 | hover 逐对象；按压及取色器动画整组。`Bar.RenderLoop.cpp:4835–4839、5015–5017` | 彩盘、色芯、勾、反馈；打开取色器属于面板开合 |
| 硬笔、软笔、激光、荧光笔 | hover 逐对象；四个按压 scale 整组。`Bar.RenderLoop.cpp:4857–4860、5030–5046` | 当前反馈；切换时旧／新选中态、笔型内容、粗细预览／三档表示、扩展入口；可能关闭旧菜单或 FineDial。`Bar.Interaction.cpp:7647–7666` |
| 笔型扩展入口 | hover 逐对象，按压和扩展进度整组。`Bar.RenderLoop.cpp:4772–4774、4861–4862、5047–5056` | 箭头／入口反馈；点击开关笔型菜单，也可能收起粗细控件和溢出提示。`Bar.Interaction.cpp:7910–7998` |
| 菜单“自由线” | hover 逐对象，按压整组。`Bar.RenderLoop.cpp:4863–4864、5057–5065` | 按钮反馈；点击关闭菜单。`Bar.Interaction.cpp:5831–5881` |
| 菜单“标注线” | 当前禁用，仅阻挡穿透，没有可执行点击。`Bar.Interaction.cpp:5884–5887` | 不应作为可选按钮测试 |
| 粗／中／细三档 | hover 逐对象；按压整组。`Bar.RenderLoop.cpp:4865–4867、5066–5094` | 旧／新选中背景、预览、数字／圆点表示、可能变化的提示 |
| 粗细调整入口 | hover 逐对象；按压和滑块过渡整组。`Bar.RenderLoop.cpp:4793–4796、4868、5096–5105` | 入口反馈、滑块／预览切换。输入位于 `Bar.Interaction.cpp:7761–7902` |
| 粗细预览、slider、thumb、长按保持 | 整组。`Bar.RenderLoop.cpp:4747–4758、4777–4786、4811–4825` | 预览尺寸与数值、轨道／thumb、保持环、文字、锁定反馈；hover 本身也可能切换 Preview/Slider。`Bar.Interaction.cpp:2940–2978` |
| FineDial 拖动、惯性、驻留、选择／取消 | 整组。`Bar.RenderLoop.cpp:4797–4810` | 拨盘、候选数值、量程及旧／新量程交叉淡入；不能只登记抓点 |
| 标注说明、溢出信息入口 | hover 逐对象；按压与提示进度整组。`Bar.RenderLoop.cpp:4826–4831、4855–4856、5018–5027` | 信息按钮反馈、提示面板／badge；悬停可能触发提示开合 |
| 两种提示的关闭按钮 | hover 对象；按压整组。`Bar.Interaction.cpp:4177–4190`；`Bar.RenderLoop.cpp:4869–4872` | 关闭按钮反馈；点击后提示退场 |
| 取色器 Tone、Close、色盘拖动／保持 | `ColorPicker::Advance` 虽报告对象变化，宿主仍额外标记整组。`Bar.RenderLoop.cpp:4835–4850` | Tone 切换图标和色盘渐变；选色改变预览、入口色芯／勾；Close 关闭面板；保持提示与进度。`UI3.ColorPicker.cpp:183–203、306–320、447–449、608–665` |

此外，笔宽、预览 morph、激光内外芯、三档圆直径／数字进度、popup retarget 都直接使用组键：`Bar.RenderLoop.cpp:4758–4810`。因此只替换按压 scale 的键，仍不足以消除点击后的整组刷新。

## 3. 形状与橡皮清单

### 形状属性

**事实：**`buttons_` 是独立 `BarButtonClass`，已有 hover、pressed、selected、图标／文字过渡及 badge；但 `AdvanceButtonVisual` 只汇总到 `changed_/active_`，没有接宿主逐对象观察回调。宿主把 `Advance()` 返回值映射为 `GeometryPanelGroup`，再观察“形状面板＋construction prompt”的整块并集。证据：`Bar.GeometryAttribute.cpp:233–255、640–645、735–804、881`；`Bar.RenderLoop.cpp:6925–6937、10412–10418`。`Changed()` 还参与持续渲染判断，直接标记入口是 `Advance()` 的结果。

| 全部控件类别 | hover／按压／点击的视觉联动 |
|---|---|
| 常规类别：直线、箭头、矩形、圆、椭圆、三角形、平行四边形、菱形、梯形、正多边形、多边形、折线、圆弧、扇形 | 按钮反馈与旧／新选中态；换工具可能增减参数行、属性项和分隔线，改变面板高度；圆／正多边形图标也随参数改变 |
| 立体类别：长方体、正方体、圆柱、圆锥、棱柱、棱锥 | 同上；棱柱／棱锥还显示边数和“正”约束 |
| 禁用占位：角度、平行线、垂直线；数轴、直角坐标系、网格、坐标点、抛物线、反比例、双曲线、正弦、余弦、正切、指数、对数 | 无 preset，不执行工具切换；保留禁用视觉 |
| 常规／立体／坐标与函数三分类按钮 | 旧／新分类选中态、整批类别淡入退场、滚动位置、可见范围／滚动条；属于布局级变化 |
| 圆心绘制／外接绘制 | 两项选中态转移，圆类别图标改变 |
| 边数 3～14（按工具实际显示子集） | 旧／新边数选中态、正多边形类别预览图标 |
| 中心／焦点／对角线辅助项 | 各自开关选中态；支持清单及文字随工具变化 |
| 三角形自由／等腰／等边；梯形等腰；正棱柱／正棱锥 | 旧／新约束选中态；后两项复用同一控件身份、文字和图标 |
| 描边／填充 | 两个独立开关，组合态可两者都选中；关闭描边会改变线型、粗细及分隔项的显隐和位置 |
| 实线／虚线／点线／点划线；粗细三档 | 旧／新选中态；粗细为自绘斜向胶囊，按下额外改变透明度 |
| 关闭；类别滚动／滚动条拖动 | 关闭触发整面板退场；滚动改变类别及滚动条包络 |
| 类别长按固定 | 延时进度环、完成后的 pin badge，涉及旧／新固定对象；正多边形还有独立轮播期限 |

清单证据：`Bar.GeometryAttribute.cpp:123–168、401–407、580–623、687–717、901–932、1005–1038、1106–1158`；索引／数量：`Bar.Main.cppm:323–332`。

**没有发现独立的形状二级弹出菜单。**当前“展开”主要是面板内参数行和分类切换；`auxiliaryHintVisible_` 每帧明确设为 false。`Bar.GeometryAttribute.cpp:848`。

### 橡皮属性及自动粗细菜单

普通按钮已有 `buttons_`；三档尺寸是自绘预览＋hover／press／selection 动画。所有结果仍合并返回，宿主标记 `EraserAttributeGroup`；`Bounds()` 合并主面板与可见菜单。证据：`Bar.EraserAttribute.cpp:107–141、191、333–344、392、490–494`；`Bar.RenderLoop.cpp:6939–6947、10426–10432`。

| 控件 | 实际视觉联动 |
|---|---|
| 清空（0） | 标准反馈；成功执行后关闭面板，可能切回选择／绘制／形状模式 |
| 尺寸小／中／大（1～3） | 各自预览缩放与 hover 轮廓；旧／新外侧选中环变化；预览位图也在缩放变换内 |
| 自动粗细正文（4） | selected、图标／文字／边框；内部分隔线跟随边框颜色与光强，箭头随自动状态变色 |
| 自动粗细箭头区（5） | 与正文共用 `buttons_[4]` 的反馈和整按钮缩放；点击开合菜单，箭头旋转 |
| 灵敏度低／中／高（6～8） | 三项旧／新 selected 转移 |
| 设置图标（9） | 标准反馈；执行设置入口，不是面板内数值编辑器 |
| 手掌橡皮（10） | 标准反馈与 selected 开关 |

证据：`Bar.EraserAttribute.cpp:74–89、314–385、395–435、453–470、513–543`。

**额外连动：**自动粗细菜单打开时，在主面板非箭头区按下会先关闭菜单。因此这些点击帧合法包含菜单退场范围，不能一律要求只有目标按钮。`Bar.EraserAttribute.cpp:592–606`。

## 4. 撤回属性

**事实：机制相同，存在同类整组登记。**内部只有一个 `redoButton_`，根据历史状态显示 Undo 或 Redo；hover、按压和内容转换汇总为 `changed_/active_`，宿主标记 `RecallAttributeGroup` 并整块 Observe。证据：`Bar.RecallAttribute.cpp:232–269、280–294`；`Bar.RenderLoop.cpp:6948–6956、10433–10439`。

点击调用 `ActivateRecallPopupCommand()`，还可能改变主栏历史命令与保护期状态。`Bar.RecallAttribute.cpp:315–320`。**建议列为可选范围，由用户决定；当前 PRD 的 R1～R3 未明确要求它。**

## 5. 局部重绘可行性

| 路径 | 事实及判断 |
|---|---|
| 脏区清空 | `FrameTarget` 先 Push dirty clip，再透明 Clear；不是在旧半透明像素上直接叠画。`UI3.Host.cpp:202–206、235–237` |
| 绘制属性背景 | 每帧仍调用面板背景绘制；材质 surface 包含绘制属性及其 popup。`Bar.RenderLoop.cpp:7352–7353`；`Bar.Initialization.cpp:1221–1233` |
| 形状／橡皮／撤回 | 使用 `PushPanelOpacity` 整体合成，然后画背景、成员；形状参数行另有一层 opacity。`Bar.GeometryAttribute.cpp:892–972`；`Bar.EraserAttribute.cpp:448–470`；`Bar.RecallAttribute.cpp:280` |
| 图层及材质 | `PushPanelOpacity` 是有界 opacity layer；`Panel::DrawFrame` 先画外框柔光，再裁成员。静态阴影是有限轮廓带，不是读取整幅目标做背景模糊。`UI3.Renderer.cpp:332–343、1772–1813`；`UI3.Panel.cpp:67–73` |
| resident／超椭圆 | `residentSurface` 明确用于主栏和主按钮；新形状／橡皮背景初始化为圆角 Shape，不能把旧 `GeometryAttributeBar` 的 theme 标志套用到新面板。`Bar.Initialization.cpp:1235–1242`；`Bar.GeometryAttribute.cpp:225`；`Bar.EraserAttribute.cpp:98–105` |

**推断：稳定布局、稳定整体 opacity 下，可以局部清空并按原顺序重画相交背景和内容；未发现按钮反馈必须整面板重画的算法依赖。**但必须保留背景重画，不能仅调用变化按钮。

**待实测：**半透明层、参数行嵌套层、边缘柔光和相邻按钮重叠的 partial/full BGRA 一致性。整体 opacity、尺寸、布局、菜单开合仍保留组范围。

## 6. 测试

| 设施 | 能证明什么／缺口 |
|---|---|
| `Bar.MultiTarget.Test.cpp:1658–1670、1770–1794` | 已用 `CreateBarOffscreenRenderCallback` 截获真实 ULW 参数，记录 dirty、source、size、full replacement，并验证成功快照；最适合复用 |
| `Bar.GeometryAttribute.Test.cpp:243–266、817–819` | 已有真实按钮输入、Advance／CommitPresented、像素读回；尚非逐按钮宿主提交矩形断言 |
| `Bar.EraserAttribute.Test.cpp:2863–2916` | 有 retained partial 与独立 full 像素一致性、真实剔除检查；不是属性全按钮矩形矩阵 |
| `Bar.Presentation.Test.cpp:1441` | 受鉴权有限场景 fixture；本文件未见逐按钮 dirty 上限断言 |
| `InkeysHeadlessTests/dirty_region_tests.cpp:102–350` | 覆盖旧新并集、出现消失、失败重试、选择性观察、组脏区保留、光照和缩放包络；不能证明真实 D2D 合成 |
| CLI／golden | geometry、eraser offscreen、host offscreen、workspace centering、golden 入口已存在。`Inkeys/IdtMain.cpp:1908–1922、2063–2073`；golden 实现 `Bar.Golden.Test.cpp:537` |

**最省事的新增方法：**复用离屏 coordinator＋现有 `prcDirty` sink，逐按钮驱动 Move、Down、Up、Leave／Cancel，固定光源并先等待布局稳定；采集每个成功帧，转换到同一坐标系后断言：

1. dirty 包含旧／新视觉包络以及真实连动元素；
2. dirty 不超过这些包络的外接矩形上限；
3. retained partial 的整幅 BGRA 等于独立 full 重画；
4. 失败帧不消费包络，重试仍覆盖旧像素。

当前提交是**单个 RECT**，多个不相邻变化按钮会产生包含中间空白的外接矩形；不能按多个矩形的面积之和验收。覆盖性还应检查 full 前后像素差集，避免包络算法与测试共享同一个遗漏。

## 7. 方案与推荐

| 方案 | 改动面与取舍 |
|---|---|
| A．复用主栏逐视觉键 | 面板为 `ButtonAnimationContext.observe` 接回调，宿主观察按钮各部分及自绘对象；整体变化仍标组。复用最多，但自绘分隔线、箭头、环和直接写值需要单独登记，易漏清单 |
| B．面板内部按逻辑控件汇总包络 | 面板维护“哪个控件／装饰变化”，将该控件全部视觉及旧新包络交给宿主 tracker；自动粗细正文／箭头可共用一个复合控件包络。比 A 略保守，但更容易覆盖自绘联动 |
| C．面板自行累计 RECT | 类似 `BarPageSurface::Include`，宿主合并。实现直接，但需额外维护成功提交、失败保留、坐标重映射，重复事务逻辑较多 |

**推荐 B，底层复用 A 的动画变化回调与现有 `DirtyRegionTracker`。**标准按钮复用 UI3 包络能力；复合按钮的内容清单、联动与布局判定留 Bar。通用 UI3 不认识 Geometry/Eraser 枚举。

建议边界：修改三个目标面板／宿主登记及必要的通用包络能力；保留主栏、PageControl、More 的既有逻辑。先分离“整体变化”和“成员变化”，再替换绘制属性中的组键；不要直接删除 `changed_` 或所有组兜底。

## 8. 风险与待实测

- **事实：直接写值不会自动产生动画观察事件。**例如橡皮选中环、分隔线、预览宽度；必须显式比较／关联。`Bar.EraserAttribute.cpp:345–376`。
- **风险：只登记新选中项。**旧选中背景、环、勾、badge 和旧 popup 都需要擦除。
- **风险：包络在继承坐标同步前采集。**现有绘制属性已经专门先同步；新增登记必须保持同源坐标及 Rigid 映射。`Bar.RenderLoop.cpp:9597–9641`。
- **风险：按压回弹超过本体。**须覆盖实际 scale、外侧环、旋转箭头、长按环和自绘胶囊；不能只用命中框。
- **事实：未知变化仍会全脏兜底。**`needRendering` 但无登记会置 `unclassifiedDamagePending`，随后 `ForceFullDamage`。应补齐分类，不能关闭兜底。`Bar.RenderLoop.cpp:7143–7148、9648–9651`。
- **待实测：**100/150/200% 缩放、深浅主题、动画开关、快速反向／取消、菜单已展开时点击其他按钮、失败重试、35 golden 逐字节对照及用户脏区显示检查。本轮未执行，不能宣称通过。