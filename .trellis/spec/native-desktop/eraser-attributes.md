# 橡皮属性与全局尺寸合同

## 1. Scope / Trigger
修改UI3橡皮属性、Drawing.Eraser配置、SpeedEraser尺寸或Clear边界时读取。主栏直拖越过工作区边界时，展开的橡皮属性也必须遵守绘制属性的松手后换边合同。普通擦除为ConfiguredEraser → ByEntry；设备入口的真实身份、penResponse与右键/笔尾触发门保持独立。

## 2. Signatures
- `SpeedEraser::InputSettings`: 五个entries、`automaticEnabled`、automaticPenSupported、BaseSize baseSize、Sensitivity sensitivity。
- `BaseSize`: Small=24 / Medium=32 / Large=40；`Sensitivity`: Low=0 / Medium=1 / High=2。
- `RestoreBaseSize(int)`, `RestoreSensitivity(int)`, `ResolveSizes(BaseSize)`, `GetAutomaticState(InputSettings)`, `SetGlobalAutomatic(InputSettings&, bool)`。
- `EraserPreferencesSnapshot()`返回整表；`SetGlobalEraserPreference(baseDiameterDip=-1, sensitivity=-1, automatic=-1)`中-1代表保留该属性。
- 持久化键：`Drawing.Eraser.Automatic`（缺失默认true）、`Drawing.Eraser.BaseDiameterDip`、`Drawing.Eraser.Sensitivity`。
- `ResolveEraserAttributeLayout(input)`返回面板、菜单、槽位、预览直径和分割线几何；`BarEraserAttributePanel::CommitPresented()`只在完整呈现成功后更新命中快照。
- `RebaseEraserAttributeLayoutInput(input, anchor)`按新旧锚点中心差统一平移锁存的`main / anchor / work`，不改变尺寸或方向字段。
- `BarEraserAttributePanel::Advance(..., parentTimeline, dragPlacementLocked)`: RenderLoop 以 `bottomDockDragActive` 传入直拖布局锁；锁存只影响面板位置/方向解析，不改变输入命中或配置状态。

## 3. Contracts
### 配置与输入
BaseDiameterDip旧16→24、64→40，32/24/40幂等；缺失/非法新键分别回到32/中。初始化、开合、总开关、选择大小或灵敏度都不得统一五入口kind；总开关只在eraserPreferencesMutex保护下修改独立bool并发布一次。详细设置每帧使用一份整表快照。写盘复用Setting的已启动业务FIFO，不打开设置窗口。对实际未变化的设置不发布、不写盘。

自动按钮只显示独立总开关的On/Off。关闭时普通入口和显式SpeedEraser策略都按Fixed解析，五入口保存值继续允许编辑；重新开启后恢复各入口配置。显式FixedEraser始终为Fixed；普通产品工具和属性操作通过IdtState发布ConfiguredEraser。

活动接触用锁存InputSettings，非Touch会话由SessionConfigCompatible检查有效尺寸/灵敏度；面板开合不得Reset控制器。

### 尺寸与灵敏度
| B | minimum / standard / maximum | Touch新接触 | Fixed |
|---|---|---|---|
|24|12 / 24 / 120 DIP|12 DIP|24 DIP|
|32|16 / 32 / 160 DIP|16 DIP|32 DIP|
|40|20 / 40 / 200 DIP|20 DIP|40 DIP|

清扫增益为0.85/1/1.15。`SweepActionSpeed`只放大fineToStandard以上的动作余量，供清扫资格和清扫目标共用；中档直接返回原速度。精细门槛、时间常数、Touch防点擦、面积报告宽高与单位转换均不缩放。面积floor仍受当前maximum夹取。

### UI与资源
使用同一Bar HWND、D2D context、共享RenderPipeline线程。标准大按钮/主题/边缘光/分割线复用Bar现有实现。`Assets/EraserGripVisual.h`是C++预览/HLSL的无副作用比例来源，FXC临时ASCII副本必须同步。

方案B排列为圆组 | 清空 | 自动整体。预览逻辑直径=`DiameterToCanvasPx(B,display)/frameZoom`，稳定pose=1，自定义UI缩放不改变实际预览直径。圆组从面板边缘到分割线的四段留白均为16逻辑单位。自动侧为5+90+5，清空与两侧分割线也各留5；清空中心锚定主栏橡皮入口，默认总宽342逻辑单位，倒转只交换完整侧组。所有工作区均保持5/16 DIP正常留白和自然宽度；窄区仅裁剪溢出，不压缩留白、圆或按钮。命中由相邻中界和可见区域限制。

预览使用Contact白色实体alpha=1，灰色来自EraserGripVisual；共享ERASER_GRIP_OPACITY仍为0.5，画布Hover不变。尺寸选项没有矩形背景/胶囊或悬停内容提示框；选中态是在橡皮外缘外留3 DIP间隙的1 DIP Theme Accent圆环，以独立可逆进度渐显/渐隐并绘制PointLight。Pressed复用普通按钮的缩放值与Press/Release曲线，围绕圆心缩放整个圆形视觉；稳定态不缩放。命中必须先裁当前可见区域，再以点到圆心距离判断真实圆形（当前允许圆外扩5 DIP），外接矩形四角不得响应。键盘焦点为独立内虚线。

自动按钮只绘制一个90×70 BarButtonClass，body/arrow命中宽为70/20并共享Hover、按压及整体缩放；内部1 DIP分割线与主栏Divider等高并垂直居中，复用当前外框色和PointLight，选中时与外框同步Accent。右侧箭头复用绘制属性笔类型入口的`barThicknessAdjust` SVG、菜单方向和0/180度目标角，展开、收起及中途反向均通过同一动画值收敛。菜单打开不改自动选中态。专用barAutoEraser.svg使用主题占位色和路径A，不依赖字体或修改通用barEraser.svg。

菜单182.5×90逻辑单位，两行结构：标题/禁用齿轮、三段等宽选项。标题保持既有左对齐X，其30 DIP标题行在浮窗顶部至三个灵敏度按钮上沿之间垂直居中。文本来自UI/Bar/EraserAttributes生成键，三种语言用i18n.ps1 sync/check维护。

正常空间clear.centerX=主栏擦除centerX，panel按非对称侧组分别延伸，menu.centerX=整个automatic.centerX。定位使用MainBar实际高度、上下状态和工作区；直拖扣除同帧直接位移。直拖锁存期间使用上一份已稳定局部布局输入、panel/menu方向和工作区，不能因临时越界重新避让或翻边；松手吸收HWND位移时，必须按当前锚点把锁存的`main / anchor / work`整体重基准，旧侧收拢到透明中点前继续使用该工作区，不能先被新工作区的Resolve/Fit夹取。之后才消费当前目标方向，并沿收拢→紧凑态→换边→展开动画交接。倒转仅交换完整侧组，在透明紧凑态交接。菜单方向锁定，父方向切换才重选。橡皮主面板和菜单均在Main Bar之前绘制，重叠像素由主栏覆盖。

EraserSurfaceMotion复用BarUiValueClass/BarUiPctClass/BarUiTimelineClass及DrawAttribute的EaseOutBack/EaseInBack、EaseOutSine/EaseInSine；紧凑宽度取BarDrawAttributeCompactWidth。状态变更才Retarget。上下倒转与绘制属性共用一个默认操作时长和同一`speedRate`：父时间线进度不超过50%时，收拢只补到父批次中点，展开占父完整后半段并与其同时截止；父时间线超过50%时创建一个完整独立批次。RenderLoop先推进父时间线的一帧，加入计算必须补偿这一帧；单帧跨过透明中点时还必须把剩余`dt`交给展开段，不能令橡皮抢跑或落后一帧。geometry允许overshoot，alpha独立有界；零时间采样保留当前姿态（动画禁用或force replace除外）。子pose复合父pose，锚点跟随当前完整自动按钮。开合期间允许整组缩放，稳定精确为1；日常Hover/Selected不缩放圆。

Bounds使用实际已变换shell及阴影外扩，不能只拿最终layout矩形。内容在当前shell内裁剪，窗口容量容纳shell与光影。PresentationSnapshot/CommitPresented只在成功呈现后发布，Pointer消费同一实际几何。ResolveEraserAttributeRelease以新Down票据为依据，body/arrow跨区Up仍返回Down所属动作；无Down的旧Up不得清空。沿用Message::IsPointerGeneratedMouseMessage过滤兼容事件；leave/cancel清理反馈，Pen/Touch Up不保留鼠标式Hover。

### 清空
UI先查真实ProductRuntimeSnapshot.currentPageHasContent；不得用白板主栏强制显示标志代替。只发既有`PublishProductCommand(Clear)`，Accepted后关闭面板，保持工具和配置。

绘制线程原有`active.empty()`边界不变：接受命令不等于活动接触尚未结束时已经执行。Clear等待Up/提交，然后清当前页批注，保留viewport和其他页。不得直接清GPU纹理代替业务事务。

每页`clearRedoAvailable`仅在Clear边界快照恢复时建立（含磁盘旧区间恢复），新笔迹提交清除此标记；Redo回到原Clear分支，复用快照/保存/回撤。页面与场景迁移保持自己的标记。Whiteboard使用同页boundaryFallback，Desktop/PPT保留原异步持久化链。

## 4. Validation & Error Matrix
|场景|结果|
|总开关关闭且五入口混合|按钮显示Off；解析全Fixed，入口保存值不变|
|只选B/灵敏度|kind、设备身份和面积开关不变|
|预览高于面板|仅裁可见区域，不改圆直径或增加透明命中|
|窄工作区|四段16 DIP圆组留白及四段5 DIP中央/自动留白不变；342 DIP自然布局溢出仅裁剪|
|指针位于尺寸入口外接矩形角|圆距判断失败，不产生Hover、Pressed或选择|
|选中尺寸切换|旧圆环渐隐、新圆环渐显；本体直径与灰色轮廓不变|
|自动菜单展开/收起或反向|箭头沿笔类型同款0/180度动画连续收敛，70/20动作区不变|
|主栏直拖跨越工作区边界|拖动中保持已呈现位置/方向；松手首帧仅随锚点平移、无工作区夹取闪烁，并在绘制属性同一批次截止时间落到新侧|
|无内容Clear|禁用，不产生空撤销记录|
|Clear接受但接触仍活动|等原Up/提交边界执行|
|Clear撤销后Redo|复用一次Clear事务|
|撤销Clear后新笔迹再Redo|旧Clear重做失效，不能删新笔迹|
|同为有内容的翻页|验证页号/命令/呈现；Host内容修订只在空/非空变化时更新|

## 5. Good / Base / Bad
Good：32/中逐字段等价现有解析器，默认控制器回归仍通过。
Base：40 DIP在35% UI缩放下高于面板，保持实际像素直径并裁上下；300 DIP工作区保持5/16 DIP正常留白，仅裁横向溢出；216组几何覆盖常规DPI/UI缩放。直拖跨界时锁存已呈现布局，松手首帧重基准且不夹取；父批次25%时加入共同截止，75%时使用完整独立批次。
Bad：把160做成第三个基础档、独立global bool遮盖五入口、用hover缩放真实圆、开合重置尺寸会话。

## 6. Tests Required
`InkeysHeadlessTests.exe --no-window`包含模型/面积/会话基准、三档/三灵敏度、DPI×UI缩放几何与真实配置读写。
`Inkeys.exe --bar-eraser-offscreen-test`生成生产组件的离屏PNG并验证圆形命中角、外侧选中环渐变/按压、无悬停提示框、箭头展开/收起中间帧、菜单/关闭/idle；直拖覆盖松手零时间首帧坐标连续、父批次25%加入后在中点提交方向并共同截止、父批次75%后创建完整独立批次；另验证300×620窄区保持正常5/16 DIP留白后仅裁剪溢出并输出`visuals/narrow-eraser-attribute.png`。这些不等同真实HWND窗口或硬件交互验收。
`Inkeys.exe --draw3-eraser-hidden-test`检查实际Host五入口/首点/活动锁存/Up和间隔。
`Inkeys.exe --draw3-hidden-test`检查Clear、Up边界、Undo/Redo、跨页、白板和场景事务。
当前方案B结果以任务scheme-b-validation.md为准；旧validation.md仅代表上一轮设计，不能据此推断本轮通过。offscreen还保存真实帧PNG/CSV及A图标三态；组合GIF只重放这些帧。

## 7. Wrong vs Correct
Wrong：固定Down继续取`EraserSizes{}.fixedDiameterDip`，而Hover已使用所选B。
Correct：Down读取`runtime.resolvedEraser.config.sizes.fixedDiameterDip`，与该接触光标及首点共享锁存值。
Wrong：对预览画普通SVG、在已乘UI缩放的坐标再乘一次DPI。
Correct：共享EraserGripVisual结构，先从真实画布像素直径除frameZoom，再按Bar坐标绘制与裁剪。
Wrong：用圆的外接矩形直接命中，或把选中Accent画进橡皮本体轮廓。
Correct：矩形只作为dirty/裁剪包络；业务命中使用圆距，选中由圆外独立可逆Accent环表达。
Wrong：直拖每帧按临时工作区重新解析橡皮面板，越界时立即翻边或闪现。
Correct：直拖沿用已呈现布局；松手将锁存的`main / anchor / work`整体重基准，透明中点前不使用新工作区夹取，再按绘制属性同一批次时长吸收新方向。
Wrong：窄工作区为塞入面板压缩5 DIP或16 DIP留白。
Correct：保持342 DIP自然布局、正常5/16 DIP留白和真实控件尺寸，仅裁剪横向溢出。
