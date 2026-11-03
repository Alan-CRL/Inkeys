# Research: Bar 已呈现 Touch 占用区域库存

- Query: Bar统一Touch目标Probe必须覆盖哪些真实控件/遮挡区域、使用何种坐标与状态门。
- Scope: internal
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar

## Findings

### 坐标与可复用快照

- `Bar.Interaction.cpp:187` IsPresentedBarVisualHit：传入**逆映射后的client像素**，取BottomDockPresentedSnapshot.zoom后除zoom，在已呈现Hit map中测试DIP。不能将screen像素直接除zoom，也不能读取目标动画几何代替已提交几何。
- screen→client必须使用同代已呈现origin（monitorOrigin+directTranslation；实际window移动须核对snapshot serial），再 `BottomDockRigidHitTestPoint` 得到Rigid。`Bar.Main.cppm:855` / `:949` / `:963` 分别提供Rigid、BodyFromRigid、GripFromRigid点映射。
- **Grip**：主按钮超级椭圆单独用GripFromRigid；**Body**：MainBar主体和普通主栏按钮用BodyFromRigid；**Rigid**：More、Draw/Geometry及各种属性/浮层。`Bar.Interaction.cpp:6072–6098` 的实际业务正是先主按钮Grip、普通控件Body；固定属性在前后其他stage以Rigid处理。不要把所有控件都塞进Body或Grip。
- `Bar.Button.cpp:2477–2495` HitTestPresentedButton/Shape从mutex保护的已呈现map读取；`:2498` Prepare候选，`:2531` Publish交换。button map已有IsVisible、enable、非Divider、非Disable、!fold及More展开门。
- **visualHits不是业务白名单**：`:2513` 注释明确透明hit area的pct=0是有意设计；所有shape只按几何登记，是否业务可用由调用方门控。因此不能遍历整个shapeMap当接受区，也不能统一 `pct>0` 排除合法透明Hit；应登记下表的真实占用region与业务flags。
- 最稳妥做法是在render准备/成功提交时发布同代curated region+availability快照，owner Probe只读该快照。不要在WndProc跨线程读写动画 `.val` 或调用Pointer()/Execute()来探测命中。

### 完整region/flags清单

| 组 | 坐标 | 区域与门（当前业务证据） |
| --- | --- | --- |
| 主按钮、折叠主栏 | Grip | `superellipseMap[MainButton]`，真实超椭圆命中；`Bar.Interaction.cpp:6098` 无!fold门，折叠后仍须接收。不能只登记MainBar shape而漏主按钮。 |
| 主栏主体/普通注册按钮 | Body | `MainBar`圆角背景可作为表面占用；按钮来自buttonList.Snapshot，按已呈现button map，非Divider、IsVisible、state!=Disable（`:6187–6199`）。fold时map不接普通按钮，主按钮独立保留。 |
| More | Rigid | `moreExpanded`时，`MorePanel`整个背景、`MorePanelCloseHit`、explicitMore/forcedOverflow中可见且可用按钮；`GetMoreButtonSnapshot`与HitPresentedButton（`:5954–6011`）。panel内部空白也consume；外部Down收起后继续传给下层，不扩大为全屏占用。 |
| Draw固定属性 | Rigid | `drawAttribute && !fold`且Pen有效工具语义；`DrawAttributeBar`实际圆角面板、ColorSelect1..11、ColorSelect12入口，ThicknessFine/Medium/Coarse/Adjust、笔型按钮/扩展入口。色块真实Down即改色并拖选（`:7815–7865`）；粗细按钮表`:7883–7909`受PenModeUsesThicknessPresets/IsLaserThicknessPresetMode约束，Laser无Adjust入口。 |
| Geometry | Rigid | `geometryAttribute && StateModeSelect==IdtShape`（`:6316`），`GeometryAttributeBar`实际背景、StraightLine/Rectangle、ThicknessFine/Medium/Coarse、Close。按钮业务表`:6328`起；不能只登记Shape工具图标。 |
| Eraser panel/menu | Rigid | `eraserAttribute.PresentationSnapshot()`，不是owner当前open布尔。`panelVisible`圆角geometry.panel，`menuVisible`圆角geometry.menu（`:510–529`）；menu优先于panel。radius=`BarMainBarCornerRadiusDip * panelPose/menuPose.scale`，坐标以snapshot.zoom解释。items交clip、尺寸预览circle有特殊命中，但整个panel/menu背景均consume。 |
| Recall/Redo panel | Rigid | `recallAttribute.PresentationSnapshot()`的panelVisible圆角geometry.panel；只有item0做Redo，但面板背景也consume（`Bar.RecallAttribute.cpp:334–376`）。command disabled不应让可见面板穿透。 |
| ColorPicker最上层 | Rigid | `ColorPickerAvailable = StateMode==IdtPen && drawAttribute && !fold`，另colorPickerOpen（`:4757`、`:5564`）。`ColorPickerPanel`整个背景、`ColorPickerPalette`、`ColorPickerToneToggle`、`ColorPickerCloseHit`、入口ColorSelect12；panel内部阻止点击穿透（`:5587–5678`），外部先收起后继续下层。PreviewBubble为展示装饰，不可仅因绘制/alpha存在就Accept。 |
| PenType菜单 | Rigid | `penTypeMenuOpen`的`PenTypeMenu`整个面板、`PenTypeMenuFreeLine`、`PenTypeMenuAnnotationLine`；菜单外Down关闭但不吞下层（`:5876–5902`）。扩展入口`PenTypeExtensionHit`另受drawAttribute&&!fold&&!laserActive&&PenModeSupportsAnnotationLine（`:8027`）。 |
| Annotation提示 | Rigid | `AnnotationTooltipAvailable = drawAttribute&&!fold&&!laserActive&&penTypeMenuOpen&&PenModeSupportsAnnotationLine`（`:4741`）；InfoHit始终是此状态下入口；Popup在pinned||hover时正文consume；PopupCloseHit仅pinned时动作有效（`:5775`、`:5821`、`:5862`）。HoverGrace只是延迟状态，不应无条件让早已退场的旧rect接管。 |
| Overflow提示/数值浮窗 | Rigid | `OverflowTooltipAvailable = drawAttribute&&!fold&&viewMode!=FineDial&&thicknessOverflowHintPresent`（`:4749`）；InfoHit、Popup、CloseHit与annotation相同pinned/hover规则。保留实际可见圆角正文，不只X。 |
| Slider/PreviewPopup | Rigid | `ThicknessSliderAvailable = Pen&&!laserActive&&drawAttribute&&!fold&&range.supported`（`:4210`）。`ThicknessSliderHit`、`ThicknessPreviewPopupSurface`（viewMode Slider或FineDial、实际opacity>epsilon、非零尺寸，`:200`）；Thumb/Track显示与实际Hit区域勿混。 |
| FineDial特殊占用走廊 | Rigid | `HitTestBarThicknessFineDialFreshActivation :241` 先排除PreviewPopup；`ownershipCorridor`内返回Consumed也代表占用，Drag只是可启动区。动画thumb未ready、rangeTransitionActive可consume但不改值（`:6421–6447`）。Probe若只测sliderHit或dragZone会漏数值/过渡走廊。 |
| BottomDock指示器 | visual client/已呈现pixels | `IsBottomDockIndicatorPresentedAt`（`:5102`）在其他控件前consume；只使用成功呈现像素，而非其shadow/矩形包络。功能上是遮挡占用，不是按钮动作。 |

### 实际处理优先级与退场

- `Bar.Interaction.cpp:8319–8349`：BottomDockIndicator → Eraser → Recall → common hover/occlusion → pointer stages。
- `:8258` DispatchPointerStages顺序：ColorPicker → overlay(tooltip/pen menu) → More → MainButton/Bar → Geometry → Thickness/FineDial → Draw。Probe需区分“真实动作接受”和“可见表面占用/阻止穿透”，二者都能使该point归Bar，但不能因此产生业务点击。
- Eraser/Recall `PresentationSnapshot()`分别在 `Bar.EraserAttribute.cpp:464` 和 `Bar.RecallAttribute.cpp:304`；CommitPresented只在成功呈现后复制frame。Pointer会在open flag已false但panelVisible仍true时consume内部，不允许退场背景穿透；只以新open布尔判断会漏这个遮挡窗口。
- 对旧shapeMap机制，presentedVisualHits本身不含alpha/visibility门。应以实际成功呈现的panel visible/可用态生成curated快照；完全关闭后不能让保留非零尺寸的旧shape继续Accept。合法透明Hit区域必须依其所属已呈现控件门处理，不能跟shadow/零alpha装饰一刀切。
- Draw/Geometry/More背景若在退场仍可见：至少保持可见背景的阻穿透；新动作准入仍按原业务条件。需要记录为占用但无动作的region，而不是重新打开旧业务。不要从“target open=false”直接让触点落到后方PageControl。

### 可复用但不能误用的工具

- `BarUISetClass::RefreshBorderCursorVisibleRegions`（`Bar.Interaction.cpp:8807`附近）可查Body/Rigid/Grip变换示例，**不能直接做Touch Probe**：它为灯光邻域使用带描边/光晕的rect，容量10，包含ColorPickerPreviewBubble，且未覆盖全部More/Tooltip/FineDial区域。
- `PresentedRegions()`可给Eraser/Recall粗包围盒，但精确Probe优先用PresentationSnapshot的圆角panel/menu，避免矩形角/阴影抢占。
- `IsPresentedBarVisualHit`/HitTestPresentedButton可复用已呈现圆角/超椭圆形状；但Availability必须从上表业务或同代render快照获得，不能仅用shape存在判断。
- `FreezeMainPresentedButtonHit`（`:1039`）已有More Rigid与main Body分别映射的正确先例，最终样本可沿现有队列freeze命中；Touch路由Probe不应替代后续业务复核。

### Files found / Related specs / External references

- `UI/Bar/Bar.Interaction.cpp`：全部真实业务条件、坐标变换和stage顺序。
- `UI/Bar/Bar.Button.cpp`、`Bar.Main.cppm`：已呈现Hit map、BottomDock快照/映射。
- `Bar.EraserAttribute.cpp`、`Bar.RecallAttribute.cpp`及各Motion.cppm：成功呈现panel geometry/visible/zoom。
- `.trellis/spec/native-desktop/rendering-and-ui.md`、`ui-pointer-session.md`：已提交像素/命中及同代输入合同。
- 无外部资料。

## Caveats / Not Found

- 只写本研究文件；未改源码/spec，未运行Git/build/GUI/commit。实现者正在并行修改Bar，行号以本次读取为准。
- 库存是业务/占用完整性审查，不是建议扩大整个窗口为Accept，也不是建议把所有视觉对象当控件。实时更新与线程归属由实现者在现有成功提交边界落地。
