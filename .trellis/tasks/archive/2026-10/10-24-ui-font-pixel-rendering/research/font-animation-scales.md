# Research: External font animation scales and stationary endpoints

- Query: 用户修订要求静止字号向下取整、动画连续且结尾无跳变；定位Word外部倍率、独立文字路径、当前/目标/时钟入口以及开关切换行为。
- Scope: internal
- Date: 2026-10-24

## Findings

### 1. 不能从当前DC推断动画目标

当前实现 `Bar.Rendering.cpp:238-250` 从当前DC变换Y轴求有效倍率，每帧floor最终像素值；`Word:3071`、细刻度 `1592-1602`、独立dock文字和帧率文字都调用它。它无法知道当前矩阵来自按压、Back过渡或稳定自定义变换，也无法知道目标矩阵。以“窗口任意动画active”临时放开小数会造成结尾跳变，还会使同窗无关文字因颜色/位置动画失去整数端点。

必须把**具体文字所依赖的字号和外层倍率的目标值**传到目标派生层。量化目标，不量化动画采样值；不能在IsSame/全窗idle边界再额外floor一次。

### 2. 当前值、目标值与时钟地图

| 路径 | 当前来源 | 静止目标来源 | 既有时钟/推进 |
| --- | --- | --- | --- |
| 通用/固定按钮按压 | `button.pressScale.val`；fixed通过parts.pressScale指针 | 同一个BarUiValueClass的`.tar`，按住是0.95，松开是1 | `AdvanceBarButtonVisual`的press visitor，或RenderLoop固定属性ChangeValue |
| 主栏显示/DPI缩放 | `frame.zoom=displayUiBaseScale.val*configZoom` | `displayUiBaseScale.tar*configZoom`；若拖动冻结显示过渡，当前冻结倍率需作为本手势目标 | ApplyDisplayTransition使用`frame.animationDtSeconds`和speedRate |
| 主栏body纵向果冻 | `state.bottomDockMapping.scaleY` | 恢复阶段把两个弹簧offset设0得scaleY=1；直接拖住时以当前抓手offset映射作为hold目标 | 独立spring，使用`animationDtSeconds`，恢复目标offset=0 |
| 主栏body横向果冻 | `state.bottomDockHorizontalMapping.scaleX` | 恢复offset=0；直接hold保持当前映射 | center/far-edge spring；纯X缩放不改变字号高度，保留几何 |
| 普通分页/白板菜单 | `scale*menuPose.scale`手乘进frameZoom | `scale*targetMenuPose.scale`，用motion目标geometry重新ResolvePose | Scene的FrameAnimationClock，同一now只Advance一次 |
| 分页主题子面板 | `scale*themePose.scale` | 父/子目标姿态Compose；不是只看子动画 | menuMotion/themeMotion，子timeline可Join父 |
| 橡皮/历史属性 | metrics font size乘`frame_.panelPose/menuPose.scale`，每帧SetDirect | 各panelMotion/menuMotion目标geometry生成目标pose后乘设计font常量 | 各panel `Update(...dt,speed...)`，同Bar frame时钟 |
| 注释/溢出tooltip | DrawPopup外层矩阵 `popup->rw.val/4` | 源 `drawAttributeAnnotationPopupProgress.tar` / OverflowPopupProgress.tar | RenderLoop ChangeValue，已有Back曲线 |
| 底栏指示器 | 指示器Progress.val×indicatorWord.contentScale | 指示器Progress.tar（0或1）×内容过渡最终scale=1 | RenderLoop同帧dt；指示器ChangeValue和Word内容过渡 |
| 细刻度标签 | `BarThicknessFineDialLabelFontSizeDip*uiZoom`，外层一般为rigid translation | 常量×目标frameZoom；不额外乘panelAnimationScale | 主栏帧；刻度位置/alpha另有动画但不改字号 |
| 调试帧率 | `12*frameZoom`，base transform | 12×目标frameZoom | 主栏帧；fps文本刷新不改变字号目标 |

### 3. 按压路径的具体改造点

- `Bar.Metrics.cppm:37`：`BarButtonPressScale=0.95`。
- `Bar.Button.cpp:1934-1936`：RetargetBarFixedButtonVisual把pressScale目标设0.95或1，曲线依pressed选择Press/Release。该目标稳定保持时即使鼠标仍按住也应整数像素。
- `Bar.Button.cpp:2053-2077`：ButtonDrawTransform RAII只接受`double scale`，给DC套当前按压倍率，结束恢复原矩阵。
- `Bar.Button.cpp:2080-2148`：DrawBarButtonVisual/DrawBarFixedButtonVisual是两个通用入口，当前均只传pressScale.val，内部Word读不到pressScale.tar。可把val/tar同时置于renderer文字倍率作用域，保证主次文字继承一致并在RAII退出恢复，不能改变SVG或Shape的scale。
- `Bar.Button.cpp:2299-2301`：press animation在标准ButtonAnimationVisitor中推进；应复用该同帧dt，避免Word绘制时单独读墙钟造成每个按钮不同步。
- `Bar.RenderLoop.cpp:10748`固定属性手工按压矩阵、`11511-11554` freeLineRow按压外层直接Word也要传对应pressScale.tar；close叉号只有SVG，字号逻辑不用扩散。

### 4. 主栏frameZoom和body缩放

- `Bar.RenderLoop.cpp:1186-1203`：ApplyDisplayTransition拿PendingDisplaySnapshot，targetZoom=targetDisplay.uiBaseScale*configZoom；`displayUiBaseScale`为BarUiValueClass，有val/tar及动画曲线。
- `RenderLoop.cpp:1415-1445`：同frame.animationDtSeconds推进displayUiBaseScale，再令barStyle.zoom=currentUiBaseScale*configZoom；`13048-13050`捕获frame.zoom并SetFrameZoom。
- 建议帧对象另外捕获目标zoom（或renderer有frameTargetZoom），不应在每次Word内重新读PendingDisplaySnapshot，以免同帧目标不一致。
- `RenderLoop.cpp:7244-7286`由spring position生成horizontal/vertical mapping；`9454-9469`把scaleX/scaleY套bodyTransform；SetBase/Body/Grip/Rigid四个lambda在`9502-9536`切换局部绘制域，适合把文字“当前/目标Y倍率”元数据与实际transform同步切换。
- `.BottomDock.h:1445-1515`表明vertical scaleY由base top/bottom和elastic/capture offsets决定；恢复目标都是0。父body几何的稳定倍率1不受fold布局改变影响。
- 但 `RenderLoop.cpp:6934-6955` 在dock拖动中直接令spring.positionDip=本帧指针求得的directOffsetDip，并不向0动画；按住停住时可以稳定非1，不能以dockDragActive永久豁免。应将direct-hold本帧的目标映射和松手恢复的目标映射区分。
- 纯X果冻不改变字体高度；原字号定义依Y轴长度，不能为了这次变更把横向字体宽度也另行取整。旋转/非均匀变换仍须按最终Y向量解析。

### 5. 分页Scene与固定属性的两种姿态表达

- `Bar.Scene.cpp:136-155`持有menuMotion/themeMotion、menuFrame/themeFrame、pose、scale、FrameAnimationClock和previousFrame。
- `Scene.cpp:485-487`通过ResolveBarAttributePanelFrame构造menu pose，再以其为parent构造theme pose。
- `Scene.cpp:565-579`先应用字体政策，再按now和FrameAnimationClock得到dt，同now避免二次推进；新增字体动画必须遵守该边界，并参与active/changed/full damage。
- `Scene.cpp:642` SetPose中`renderer.SetFrameZoom(scale*pose.scale)`，DC只做平移，故“DC倍率target=1”不足以捕捉这里的动画。
- `Bar.EraserAttributeMotion.cppm:56-94` 的BarAttributePanelMotion私有保存requested_、geometry_、opacity_、timeline_；当前公开Geometry()/Opacity()/Timeline()/Active()，**没有目标Geometry入口**。可新增只读TargetGeometry或Requested接口，再由ResolveEraserSurfacePose按目标geometry（0或1）生成pose；主题子面板仍用ComposeEraserPose组合父/子目标，退出compact目标不是1。
- `EraserAttributeMotion.cppm:21-29`：ResolveEraserSurfacePose的scale=compact+(1-compact)*geometry，Back可超出0..1；不可clamp动画geometry吞回弹。
- `Bar.EraserAttribute.cpp:298-315,365`与`Bar.RecallAttribute.cpp:244-260`采用**预先把几何/字号按pose缩放**的另一种表达，每帧对name.size/title.size SetDirect，导致word.size.tar等于当前派生字号。即使把Word.size改为目标层，仍会误把每帧派生值当新静止目标。需要源motion目标pose与未缩放metrics作为显式目标，并保留当前派生值连续运行。
- 固定属性的显示/左右翻转过程会先收拢、透明后换侧，再展开；源motion可能分段重定向且携带剩余dt（EraserAttribute.cpp:217-249、Recall对应Update）。字体只跟实际文字所属motion，不能以全局panel active触发绕过。

### 6. Tooltip、细刻度和dock独立文字

**注释/溢出tooltip**

- `RenderLoop.cpp:6451-6464`用源Progress.val派生PopupDerivedLayout；ApplyPopupLayout在`6470-6532`每帧SetDirect字号、radius等几何。
- 绘制的DrawThicknessPopup `11335-11426`反向解缩放布局，并临时将title/body.size.SetDirect回完整设计常量，再套popup->rw.val/4的矩阵调用Word，绘制后恢复原值。
- 因派生radius已SetDirect，不能从popup->rw.tar判断目标。必须把源annotation/overflow Progress.tar（0或1）传进DrawThicknessPopup的文字作用域；不必重构该资源复用策略。

**细刻度**

- `Bar.Rendering.cpp:1592-1602`字体来自BarThicknessFineDialLabelFontSizeDip×zoom；`RenderLoop.cpp:10582-10620`标签排版调用该cache并直接DrawTextLayout。
- panelAnimationScale影响刻度线长、文字的位置和透明度，未乘进标签字号。不要重复把面板scale加进fontsize目标，造成动画端点比原设计更小。
- cache现按value、zoom、fontSize查找；若改连续字体动画，key必须使用实际本帧解析字号，不能缓存旧目标格式。若选择固定完整格式+文字专用连续变换，布局测量也必须按最终文字专用scale保持一致。

**底栏指示器**

- `RenderLoop.cpp:7009-7036`ApplyFeedbackTarget把bottomDockTargetIndicatorProgress.tar设0/1并用Back曲线；`7070-7099`按同帧dt推进indicatorWord内容transition和progress。
- `7891-7895`取Progress.val为当前几何scale；`12045-12046`额外取indicatorWord.contentScale。
- `12096-12115`把两倍率乘入DC后直接DrawTextW（不走Word），当前ResolveFontSize是每帧floor。应接入同一目标解析/连续字体状态；其稳定可见目标为Progress.tar=1、content最终scale=1。退场可见时仍沿原连续scale走向0，不能每帧把小字体floor到0而突然消失。
- indicator自有stable Word对象，可以承载文字策略状态，但其DrawTextW调用必须显式使用该状态；不要只修Word函数。

### 7. 目标量化与平滑配置切换建议

数学上，最终静止原始物理字号 `T = 字号逻辑目标 × 目标frameZoom × 外层目标Y倍率 × 最终内容缩放`；关闭政策时目标 `Q=floor(T)`，开启时 `Q=T`。只在目标层求Q。

可选的最小兼容机制：保留现有全部raw字号与几何轨迹R(t)，对每段文字维护一个连续修正系数k。目标改变时令 `kTarget=Q/T`（允许小数为1），从当前k按既有帧dt/时长/曲线或对应timeline连续过渡；本帧DWrite字号乘kCurrent。由于R(t)和k(t)均连续，最后R=T且k=Q/T，自动落在Q，不需要结束时二次floor，也不必改变图标/按钮几何。主会话需和内部Word.size目标方案选定一种统一实现，避免双重修正。

实现时应注意：

- 配置变化应重定向当前字号/修正目标，不先清空它；首次展示可直接初始化到目标。保留旧format缓存不是问题，但清掉连续状态会首帧跳变。
- 目标签名至少含字号目标、目标frameZoom、所有外层目标scale、政策；只有签名变化才SetTar，重复绘制/重复目标不重启动画。
- 新文字动画需参与owner active与damage，不依赖其他颜色动画带着渲染；若政策在静止时切换，也应由自己的有限动画唤醒到完成。
- 同帧多次draw同Word（或主次Word）不能推进多次dt；采用Advance阶段统一推进或frame stamp保护。
- 真实present失败时不能把候选字体状态作为“已上屏起点”重定向，沿现有候选/已提交边界；无需改全局呈现模型，但不要读取未提交候选来产生可见跳变。
- 亚像素小于1的静止目标Q=0要安全跳过，0目标不允许计算Q/T造成除0；退场动画中允许连续小数，不能末帧前提前floor为0。
- 单个文字因颜色或位置变化动画时，字号目标仍是自身整数Q；其他文字有动画不是改变其字号政策的理由。

### 7.1 主会话选定方案与最少元数据接入点

主会话已选定修正系数c方案：每Word固定有界状态，`cTarget=floor(T)/T`（开启为1），最终raw轨迹×连续c；共用帧clock/active/damage，禁止结束时补floor。下列位置是外层稳定目标元数据的最少集合，不能只通过当前DC猜测：

1. **全局frameZoom目标**：BarRenderLoop帧快照增加目标zoom，取displayUiBaseScale.tar×configZoom；renderer同时接当前frameZoom和目标frameZoom。Scene.SetBounds的scale当前无独立动画，其目标即接收值；Scene.SetPose额外组合目标pose倍率。
2. **通用/固定button按压域**：ButtonDrawTransform或DrawBarButtonVisual/DrawBarFixedButtonVisual把pressScale.tar作为文字专用目标倍率push/pop。按住时tar=.95是稳定端点。背景/SVG继续只消费当前val；所有按钮主/次Word共用该域。
3. **RenderLoop手工fixed按压域**：绕过通用button的固定属性、freeLineRow等包含Word的手工矩阵处，同步传对应BarUiValueClass.tar；只有叉号SVG的close矩阵不接字号政策。
4. **body/base/rigid/grip绘制域**：在现有SetBodyTransform等lambda设置目标Y倍率。body recovering目标1；direct-hold用当前已准入抓手位置导出的目标mapping.scaleY；base/rigid目标1。纯X缩放不进字号高度。已按住拖动但指针静止仍须收敛到对应Q，不能将dockDragActive当豁免开关。
5. **Scene菜单/主题姿态**：BarAttributePanelMotion新增只读目标geometry获取接口（或frame同时含current/target pose）；RefreshGeometry以相同ResolvePose解析目标，theme目标Compose(parentTarget,childTarget)。SetPose传当前/目标frameZoom。headings与menu buttons都覆盖，无需每个按钮自算父链。
6. **Eraser/Recall预缩放字号**：各panel Update派生字号时给Word提供独立raw静止目标=设计metrics×targetPose.scale，保留现有size.SetDirect(metrics×currentPose.scale)为raw采样。标题同样处理。这里不能再次让renderer乘pose目标，避免双重计算；Word raw目标元数据要能覆盖SetDirect制造的伪tar。
7. **annotation/overflow tooltip临时完整字号**：DrawThicknessPopup增加源Progress目标参数；临时固定完整font时外层目标来自源Progress.tar。不要从派生popup.radius.tar推断；局部draw域结束恢复文字目标上下文。
8. **独立dock文字**：bottomDockIndicatorWord承载相同c state，原始target=设计font×targetFrameZoom×indicatorProgress.tar，内容过渡最终scale=1；DrawTextW显式应用cCurrent。内容退场/Back动画倍率保留，不floor当前采样。
9. **细刻度/帧率**：各自固定有界font c state（细刻度所有数字共享同一font state即可，不按tick创建增长容器）；target仅常量×targetFrameZoom×真实外层目标Y倍率。细刻度不乘panelAnimationScale。实际本帧字号参与cache key；相关owner只在cActive时继续有限唤醒。
10. **政策切换**：UpdateFontRenderingPolicy保留每Word/direct path当前c，变更只使目标无效后重定向；policy初始未见时可直设。每个字的c仅在帧advance阶段推进一次；复用已有changed/active聚合和owner dirty。不要在缓存clear时把c重新初始化。

### 8. 三语说明建议

标题仍使用用户要求的原文；说明应明确仅静止字号取整，避免新行为与文案不符。当前三语说明在 `Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc:543-547`。

- zh-CN：`关闭时，主栏和控件静止状态的字体像素尺寸向下取整；缩放和过渡动画仍连续变化。立即生效。`
- zh-TW：`關閉時，主列和控制項靜止狀態的字型像素尺寸向下取整；縮放及轉場動畫仍連續變化。立即生效。`
- en-US：`When off, stationary font sizes in the main bar and controls are truncated to whole pixels. Scaling and transition animations remain smooth. Applies immediately.`

“静止”指字号目标稳定，文字可以仍在平移或变色；测试和内部注释应以字号目标解释，避免误做全局idle门禁。

### 9. 必要回归案例

1. 普通按钮1→0.95按下，按住数秒，0.95→1释放：全过程连续，两个hold端点实际px为floor目标；无父几何结束一帧跳变。
2. 同窗另一个按钮只变色或滑动时，未变字号文字继续整数目标。
3. 48DIP缩放0.6，显示倍率跨DPI动画：以目标zoom解析最终字号，最后采样与稳定重复帧一致。
4. Scene父菜单+主题子菜单打开/退出、Back上溢、反向中断；目标pose组合正确，隐藏后恢复不读旧政策。
5. 橡皮/Recall派生SetDirect字号与tooltip临时SetDirect常量：不能误判“当前就是目标”而每帧floor。
6. dock指示器DrawTextW与内容缩放；细刻度DrawTextLayout；debug帧率在显示缩放时；覆盖全部独立文字。
7. 开关在静止时/动画中/pressed hold中双向切换，从当前可见字号平滑重定向；关闭端点向下取整，开启端点原始字号。
8. 同frameTime重复Advance/Draw、不相关长时动画、present失败重试、暂停后首帧dt=0，均不重启或多推进字号动画。

## Related specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`：共用帧时钟、当前/目标分离、同帧只推进一次、稳定帧idle。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：UI3偏好、保存队列、i18n生成。
- `.trellis/spec/native-desktop/cpp-conventions.md`：最小变更、格式、所有权。

## External references

无；本调查仅记录当前仓库的动画所有权和调用链，不需要外部API行为推断。

## Caveats / Not Found

- 本文件是研究和可选方案，不是已完成实现；当前每帧floor方案仍须按用户新要求修订。
- BarAttributePanelMotion现无目标getter，renderer现无外层目标矩阵/倍率元数据；仅修改ResolveFontSize函数无法完整实现新合同。
- 设置ImGui字体和启动预览仍明确排除。未启动GUI、未build、未执行git操作。
