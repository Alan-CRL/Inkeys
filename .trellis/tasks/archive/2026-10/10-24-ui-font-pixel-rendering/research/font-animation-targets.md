# Research: 字号动画目标量化与连续性

- Query: 仅限制静止字号，动画中允许连续小数；将截断约束放到目标层并避免最后一帧跳变。
- Scope: internal；仅读取产品源码并写本任务 research。
- Date: 2026-10-24

## Findings

### 新要求与失败做法

用户修订的行为要求：静止最终字号按实际 px 向下截断；字号/缩放动画的中间值连续，动画结束不再额外 floor。不能使用 `if (animating) original; else floor(original)`，因为最后一个动画帧和第一个 idle 帧会产生小于 1px 的跳变；也不能继续在 Word 中逐帧 floor 当前尺寸。

只观察 D2D 当前矩阵不够：它不包含目标矩阵、动画进度或目标是否即将变化。任何能正确处理 press、popup、displayZoom 的方案都必须从现有动画 owner 获得最终目标尺度或相关 timeline，而不能通过全局“窗口正在动画”判断推断。

### 现有 size 动画链

| 位置 | 现有契约 |
| --- | --- |
| `Bar.UI.cppm:402–430` | BarUiWordClass.size 是普通 BarUiValueClass；contentScale/contentPct 是独立原子值 |
| `Bar.UI.cpp:789–812` | 构造与 Initialization 以 size.Initialization(rawDesignDip) 初始化 |
| `Bar.Animation.cppm:772–883` | BarUiValueClass.SetTar 保留 val 为起点，设置 tar、startV、progress=0、dur、middleV、activeCurve 和 phase；相同 tar 且非 forceRestart 不重启 |
| `Bar.Animation.cppm:843–864` | SetDirect 同时改 val/tar/startV、清时长/中点/phase；Initialization 也走 SetDirect |
| `Bar.Animation.cpp:119–179` | BarUiAdvanceAnimation 在 transaction 中按 startV/tar/middleV、curve/progress/phase 采样；结束 FinishValue 将 val=tar，清 progress/dur |
| `Bar.Button.cpp:2272–2288` | AdvanceBarWordVisual 用公共 visitor 推进 x/y/w/h/size，再推进文字/颜色/透明度 |
| `Bar.Rendering.cpp:3054–3072` | Word 读取 size.val，乘 frameZoom、contentScale 后交给字体解析；第一轮实现此处逐帧 floor，需要修订 |

`SetTar` 的 source target identity 有意义：RenderLoop:4216、4364、4741 会调用 `size.SetTar(size.tar, ..., forceRestart=true, curve)` 来加入布局批次或中点轨迹。直接将公开 `.tar` 写成截断值会失去原设计字号，也会影响这些代码以 tar 推导 compact middle 的行为。公开 `.val` 也被比较和临时保存，不能偷偷改变为物理 px。

### 不宜只用派生字号类型拦截 SetTar

`ConfigureBarButtonVisualTargets`（Bar.Button.cpp:2168）内 Set lambda 明确接收 `BarUiValueClass&`；若 size 换成派生类，该处会按静态类型调用基类 SetTar。`AdvanceBarWordVisual:2280` 的 `{&x,&y,&w,&h,&size}` 同类型指针列表也会受影响。只重载 SetTar 不能覆盖全部产品提交路径。

如在 BarUiValueClass 本身增加字号专用 opt-in 投影，应默认关闭且仅 Word.size 开启；所有形状、位置、缩放和透明度继续原样。但即使如此，也必须维持 raw val/tar 与投影输出两套数值，不能让通用动画直接持有量化后的业务 tar。

### 独立动画/缩放来源

- 内容切换（Bar.UI.cpp:814–940）：TransitionToString 捕获当前 contentScale，按自有 KeyframeTimeline 缩到 middleScale，透明中点换字，再 EaseOutBack 回到 1；最后明确 contentScale=1。这个缩放应作为连续过渡因子，不逐帧 floor 中点或 overshoot；重复相同文字不会重启 timeline。
- 按钮按压（Bar.Button.cpp:2055–2079、2291–2310）：pressScale 有自己的 val/tar/start/progress，先于 name 推进，绘制时以 DC 变换施加；按住时 tar 可以是 0.95 等小数。若要求 pressed hold 的静止终点也为整数，必须将 pressScale.tar 纳入最终目标像素计算，不能永远仅 floor(base)*pressScale.val。
- 主栏跨屏缩放（Bar.RenderLoop.cpp:1200–1203、1286、1419、1440–1445）：最终 zoom = displayUiBaseScale.tar * barStyle.configZoom；当前 zoom 用 displayUiBaseScale.val。将当前 frameZoom 当网格会在跨屏动画每帧跨 px 档位。最终目标在现有值 tar 中，不需猜测。
- 分页缩放（PageControl.cpp:201–214、978–1010）：AnimatedBounds.values[4] 为现有缩放动画值，targetScale 是最终目标。Scene.SetBounds 当前只接当前 scale；需要将目标尺度/源 animation metadata 显式交接给 Scene/renderer，不能从 SetBounds 当前 scale 再倒推出 target。
- 弹窗 pose（Bar.EraserAttributeMotion.cppm:56–93）：BarAttributePanelMotion.geometry_ 是既有动画值，公开 Geometry() 当前值和 Timeline()，但当前没有公开 GeometryTarget/geometry value snapshot；visible/open 稳定终点为 1，closed 为 0（通常不可见）。需要可预测终点时可增加小型只读 target/snapshot 接口，不重写 motion。
- 底栏 body 的非均匀弹性缩放（RenderLoop:9452–9483）：当前 DC scale 存在独立 X/Y；字号目标使用 Y 方向尺度，动态拖拽/回弹需要沿已有目标状态交接，不能只因“动画结束”才截断当前矩阵。

### 每帧 SetDirect 的动态字号路径

这些路径传入的 size 当前值已经乘 pose/panel/picker 的连续缩放，直接在 SetDirect 内 floor 会把旧问题移到提交层：

- `Bar.EraserAttribute.cpp:308–310,365`：字号为 metrics.primaryFontSizeDip * panelPose.scale；title 同样乘 menuPose。
- `Bar.RecallAttribute.cpp:259–261`：字号为 metrics.primaryFontSizeDip * panelPose.scale。
- `Bar.RenderLoop.cpp:6207,6241,6321,6505–6518,6802,6857`：各固定标签 size.SetDirect(base * current panelScale/pickerScale)。
- `Bar.RenderLoop.cpp:11386–11390,11461–11464`：DrawThicknessPopup 临时把 title/body size 设置为完整基准以复用字体资源，绘制后恢复原 size；不能将这类临时 draw 设置解释为新的业务目标命令。

最小改法是显式区分“名义字号目标”与“连续 pose 因子”：名义字号目标只在类型/配置/最终静止尺度变化时重投影，pose/currentScale 继续从现有动画取样。对不同性质的 SetDirect 不能依靠统一启发式猜测。

## 最小可行方案比较

### A. 投影端点，沿原 size 进度采样

保留原 BarUiValueClass 的 raw val/tar/startV/middleV。为 Word 的字号投影存固定字段：raw 命令 generation、目标最终像素尺度、projectedStartPx、projectedTargetPx（及需要时 projectedMiddlePx）、上次实际输出。目标提交时仅对 raw target 的最终 px 执行 floor。中间帧复用 BarUiAdvanceAnimation 当前的曲线、progress 和 phase，在线性/双段插值中替换投影端点，不 floor 当前 val。

最重要的重定向合同：新的 projectedStartPx 必须是前一时刻已经显示的投影输出，不能是 `floor(new raw startV * new grid)`。后者在新目标网格改变时会立即跳变。建议在原 SetTar/SetDirect 成功提交的 transaction 内递增只读 generation，投影按 generation 判断一次新的轨迹；重复目标不会递增。

优点：单一 timeline、精确继承已有曲线和结束时刻，raw 业务值完全不变。适用 raw size 自身动画。限制：当 size 本身不动而 zoom/press/popup 在动时，需要显式选择这些现有驱动或组合上下文；仅实现 raw size 会遗漏它们。不要把 raw size 拷贝为另一个重新计时的动画而不传 phase/remaining duration，否则中途加入布局批次会错相。

### B. 每 Word 固定一个连续的像素修正系数状态

该方案更容易覆盖多重同步缩放，并避免复制完整动画采样算法。对离散目标提交，计算：

```text
rawTargetPx = rawDesignTarget * finalZoom * finalPoseY * finalPressY * effectiveDpiFactor
correctionTarget = allowFractional ? 1 : floor(rawTargetPx) / rawTargetPx
renderedCurrentPx = rawCurrentEffectivePx * correctionCurrent
```

每 Word 内固定持有一个现有 BarUiValueClass 的 correction（或同等有界字段），从旧 correction.val 向 correctionTarget 连续插值；所有推进使用现有 frame dt/speed/forceReplace，没有新增计时器或线程。目标不变时 SetTar 幂等，不因每个 draw 重启。插值时长/curve/phase来自实际改变目标的现有 size/zoom/press/popup驱动；目标 rawSize 和最终尺度按帧冻结。

性质：rawCurrentEffectivePx 连续，correctionCurrent 连续，因此其乘积连续；retarget 时 correction.startV=旧 correction.val，首帧不跳；全部动画到达 rawTargetPx 且 correction 达到 correctionTarget 后，最终恰为 floor(rawTargetPx)，idle 不再执行额外截断。它允许同时进行的多个 raw 动画，中间仍为小数。

例如 13DIP→15DIP，zoom=0.6：raw 当前像素从7.8→9，correction从7/7.8→1，实际字号从7连续到9。中间值曲线是两条连续量的乘积，和直接 7→9 插值略有差异；若要求严格沿同一字号曲线，选 A 的像素端点采样。

必须防止两个错误：

1. 不可直接把 correctionTarget 应用到当前值而不插值，否则每次新目标发生时会跳变。
2. rawTargetPx 必须来自最终 target（包括按压 hold 的 press.tar），而不是当前 frameZoom/pose，因此 floor 仅出现在目标变更处；不能每帧对动态 SetDirect 的当前值生成新目标。

此方案不需要 renderer 按 word 地址维护 map；字段属于现有 Word，对直接文字采用现有固定 owner 字段或既有 Word 即可。无固定 owner 的细刻度 layout 用已有有界64槽缓存，不引入无界对象缓存。

### 推荐方案与最少统一 API 接点

主会话选择 **B：每 Word 一个固定有界的 correction 动画状态**。保留 A 的比较作为不选用它的依据，不先实施 A。具体签名由 implement 代理结合现有 frame 结构确定，以下是最少职责划分：

1. `BarUIRendering::BeginFontFrame(frameSerial, dt, speed, allowFractional, targetZoom, animationEnabled)`：每客户端每帧一次，冻结字体策略、最终zoom和共享frame时钟，重置本帧字体 changed/active汇总。`SetFrameZoom(currentZoom)`继续只表示实际绘制倍率，不覆盖 targetZoom。主栏targetZoom取displayUiBaseScale.tar×configZoom；分页从AnimatedBounds.targetScale交接。
2. `BarFontTargetScope`或等价栈上作用域：在已有 ButtonDrawTransform、SetPose和主栏body/popup变换旁同步提交**最终**字体Y倍率/相关duration与curve信息，作用域退出恢复父值。它只管理字体上下文，不拥有D2D资源，不维护word map。按压scope使用pressScale.tar，popup/menu使用最终pose；当前物理比例仍由实际绘制矩阵获得。
3. `BarUiWordClass`固定字段：correction（现有BarUiValueClass）、initialized、lastAdvancedFrameSerial，以及最后名义目标/目标像素值或必要的策略签名。首次无历史绘制直接将correction置为目标，避免从无意义默认值进入动画。已有word从当前correction.val重定向，目标重复时SetTar幂等，不重启。每Word每frameSerial最多推进一次，多个pass或同frameTime重复Render不加速。
4. `ResolveWordFontSize(word, rawCurrentSize, nominalTargetDipOverride?)`：计算 `T=nominalTargetDip×targetZoom×最终targetScopes×有效DPI因子`，只有该离散目标变化才计算floor(T)/T；推进correction一次，实际输出为rawCurrentEffectivePx×correction.val，再按实际DC单位反算给DWrite。原raw size.val/tar均不变。当前Word接const引用，可使用明确的mutable render状态或调整为非const引用；不要const_cast原动画字段。
5. pose-scaled SetDirect 的特殊调用显式传原始nominalTargetDip，或为Word单独记录基准target：例如橡皮/Recall为metrics.primaryFontSizeDip，当前size.val仍为该基准乘currentPose。DrawThicknessPopup的临时资源复用设置不作为新业务目标。**不能把SetDirect(currentPose×base)的tar视为静止字号**。
6. contentScale始终是连续current乘数，不纳入T：它是1→middle→1的换字动画，最终静止比例为1；透明中点换字、overshoot和取消逻辑不改。不要对mid contentScale量化。
7. `FontAnimationChanged/Active`或等价结果并入既有damage/continueRendering：correction变化即使raw字号无变化也要重画；active直到correction完成，最后active和首idle帧消费相同最终值。隐藏/不可绘制word不应造成无意义持续唤醒；显示恢复首帧按同一owner frame规则同步目标和dt。按frameSerial保护推进位置应在重复绘制/裁剪的共同入口，避免只在某一绘制pass推进。
8. FPS/dock等直接文字采用已有固定owner字段或现有Word来存correction；细刻度保留固定容量缓存，避免按字符串/地址新建无界字体map。绘制端清除逐帧floor以及“动画结束后补floor”。静止目标T<1时correctionTarget=0，按原策略最终不画，不反向放大。

重定向连续性可直接由乘积证明：设旧rawCurrent为R、旧correction为C，提交新目标时保持raw当前与correction当前，则提交前后都为R×C；后续两个量连续，结束时rawCurrent=T且C=floor(T)/T，所以输出恰为floor(T)。若相关原动画直接SetDirect跳变，则保持其既有立即替换语义，不额外制造反向补跳。

## 必要的连续性回归断言

- 用生产 BarUiValueClass 从 13→15DIP、zoom=.6 运行真实曲线：静止端点7→9px；至少几个中间值具有小数，不能出现逐整数平台。
- 在 p=0.4 处反向/改目标，目标提交前后 dt=0 的实际输出必须相等（epsilon）；后一帧从旧输出继续，不从floor(rawStart*newScale)起跳。
- 在 p接近1和p=1采样，差值应随dt趋零；最后active帧与首idle帧不得再出现余数跳变。
- 连续每帧重复相同原始设计目标，progress单调推进且最终完成；raw tar保持原设计值，开关 true 可恢复原目标。
- SetDirect/Initialization首次显示的静止目标直接整数；无动画/forceReplace跳到同一投影最终目标，不产生待恢复小数。
- 双段middle/continueTimelinePhase和forceRestart到相同tar：保留中点与剩余截止时刻，不擅自清掉 hasMiddleV。
- contentScale 0.8→1和EaseOutBack overshoot保持连续；中点透明换字身份不变，结束基准整数，无末帧额外floor。
- zoom跨屏 .6→.9 与反向重定向；分页目标scale通过真实AnimatedBounds.values[4]；不能每帧使用currentScale作为投影网格。
- press hold末态（如 .95）必须按最终目标获得整数；release重定向首帧连续。与popup/body叠加时仍按最终目标收敛。
- 隐藏/暂停/同frameTime重复Advance：不重复推进，恢复首帧 dt=0 不改变输出；投影 active纳入现有damage和续帧结果，不能在到达目标前idle。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`：统一frameTime、禁止同帧多次推进、当前/target与dirty/committed合同。
- `.trellis/spec/native-desktop/cpp-conventions.md`：原子快照、最小diff及原文件编码/换行。

## Caveats / Not Found

- 仅凭 Word 当前矩阵无法恢复所有静止目标，不能承诺只改一个floor helper就正确实现修订。
- 调查未执行GUI、构建、运行测试或git操作。行号可能随并行实现漂移。
- 初版设计文档仍描述逐帧最终字号floor，需由主会话按用户新修订更新。
