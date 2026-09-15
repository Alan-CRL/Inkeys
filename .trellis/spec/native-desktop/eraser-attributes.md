# 橡皮属性与全局尺寸合同

## 1. Scope / Trigger
修改UI3橡皮属性、Drawing.Eraser配置、SpeedEraser尺寸或Clear边界时读取。普通擦除为ConfiguredEraser → ByEntry；设备入口的真实身份、penResponse与右键/笔尾触发门保持独立。

## 2. Signatures
- `SpeedEraser::InputSettings`: 五个entries、automaticPenSupported、BaseSize baseSize、Sensitivity sensitivity。
- `BaseSize`: Small=16 / Medium=32 / Large=64；`Sensitivity`: Low=0 / Medium=1 / High=2。
- `RestoreBaseSize(int)`, `RestoreSensitivity(int)`, `ResolveSizes(BaseSize)`, `GetAutomaticState(InputSettings)`, `SetGlobalAutomatic(InputSettings&, bool)`。
- `EraserPreferencesSnapshot()`返回整表；`SetGlobalEraserPreference(baseDiameterDip=-1, sensitivity=-1, automatic=-1)`中-1代表保留该属性。
- 持久化键：`Drawing.Eraser.BaseDiameterDip`、`Drawing.Eraser.Sensitivity`；自动开关由五入口实际kind推导，没有独立bool。
- `ResolveEraserAttributeLayout(input)`返回面板、菜单、槽位、预览直径和分割线几何；`BarEraserAttributePanel::CommitPresented()`只在完整呈现成功后更新命中快照。

## 3. Contracts
### 配置与输入
缺失/非法新键分别回到32/中。初始化、开合、选择大小或灵敏度不得统一五入口kind；只有全局开关会在eraserPreferencesMutex保护下统一写五个kind并发布一次。详细设置每帧使用一份整表快照。写盘复用Setting的已启动业务FIFO，不打开设置窗口。对实际未变化的设置不发布、不写盘。

全Speed显示开，全Fixed显示关，混合显示横杠；混合点击全开。显式FixedEraser/SpeedEraser API旁路仍优先于entries；普通产品工具和属性操作通过IdtState发布ConfiguredEraser，不能被旧固定策略遮盖。

活动接触用锁存InputSettings，非Touch会话由SessionConfigCompatible检查有效尺寸/灵敏度；面板开合不得Reset控制器。

### 尺寸与灵敏度
| B | minimum / standard / maximum | Touch新接触 | Fixed |
|---|---|---|---|
|16|8 / 16 / 80 DIP|8 DIP|16 DIP|
|32|16 / 32 / 160 DIP|16 DIP|32 DIP|
|64|32 / 64 / 320 DIP|32 DIP|64 DIP|

清扫增益为0.85/1/1.15。`SweepActionSpeed`只放大fineToStandard以上的动作余量，供清扫资格和清扫目标共用；中档直接返回原速度。精细门槛、时间常数、Touch防点擦、面积报告宽高与单位转换均不缩放。面积floor仍受当前maximum夹取。

### UI与资源
使用同一Bar HWND、D2D context、共享RenderPipeline线程。标准大按钮/主题/边缘光/分割线复用Bar现有实现。`Assets/EraserGripVisual.h`是C++预览/HLSL的无副作用比例来源，FXC临时ASCII副本必须同步。

预览逻辑直径=`DiameterToCanvasPx(B,display)/frameZoom`，同一父坐标再乘frameZoom绘制，因此自定义UI缩放不改变预览像素直径。宽度以真实预览宽度与标准按钮命中宽度的较大值计算；窄工作区先减留白，再仅裁中段。圆心垂直居中、等圆半径，选中胶囊留在面板内。

定位使用MainBar实际高度、当前擦除锚点、主栏左右/上下状态和工作区；直拖时工作区转换扣除同帧直接位移。倒转组在淡出中点换向，文字不旋转。菜单方向在本次打开中锁定，父方向切换才重选。脏区/容量/光源/命中都消费同源几何，只有呈现成功后才提交快照。

### 清空
UI先查真实ProductRuntimeSnapshot.currentPageHasContent；不得用白板主栏强制显示标志代替。只发既有`PublishProductCommand(Clear)`，Accepted后关闭面板，保持工具和配置。

绘制线程原有`active.empty()`边界不变：接受命令不等于活动接触尚未结束时已经执行。Clear等待Up/提交，然后清当前页批注，保留viewport和其他页。不得直接清GPU纹理代替业务事务。

每页`clearRedoAvailable`仅在Clear边界快照恢复时建立（含磁盘旧区间恢复），新笔迹提交清除此标记；Redo回到原Clear分支，复用快照/保存/回撤。页面与场景迁移保持自己的标记。Whiteboard使用同页boundaryFallback，Desktop/PPT保留原异步持久化链。

## 4. Validation & Error Matrix
|场景|结果|
|全局类型混合|横杠，点击五项全Speed|
|只选B/灵敏度|kind、设备身份和面积开关不变|
|预览高于面板|仅裁可见区域，不改圆直径或增加透明命中|
|无内容Clear|禁用，不产生空撤销记录|
|Clear接受但接触仍活动|等原Up/提交边界执行|
|Clear撤销后Redo|复用一次Clear事务|
|撤销Clear后新笔迹再Redo|旧Clear重做失效，不能删新笔迹|
|同为有内容的翻页|验证页号/命令/呈现；Host内容修订只在空/非空变化时更新|

## 5. Good / Base / Bad
Good：32/中逐字段等价现有解析器，默认控制器回归仍通过。
Base：64 DIP在65% UI缩放下高于面板，保持实际像素直径并裁上下。
Bad：把160做成第三个基础档、独立global bool遮盖五入口、用hover缩放真实圆、开合重置尺寸会话。

## 6. Tests Required
`InkeysHeadlessTests.exe --no-window`包含模型/面积/会话基准、三档/三灵敏度、DPI×UI缩放几何与真实配置读写。
`Inkeys.exe --bar-eraser-offscreen-test`生成生产组件的离屏PNG并验证按钮/菜单/关闭/idle；不等同真实HWND窗口或硬件交互验收。
`Inkeys.exe --draw3-eraser-hidden-test`检查实际Host五入口/首点/活动锁存/Up和间隔。
`Inkeys.exe --draw3-hidden-test`检查Clear、Up边界、Undo/Redo、跨页、白板和场景事务。
实际执行结果以任务validation.md为准，不能从命令存在推断通过。

## 7. Wrong vs Correct
Wrong：固定Down继续取`EraserSizes{}.fixedDiameterDip`，而Hover已使用所选B。
Correct：Down读取`runtime.resolvedEraser.config.sizes.fixedDiameterDip`，与该接触光标及首点共享锁存值。
Wrong：对预览画普通SVG、在已乘UI缩放的坐标再乘一次DPI。
Correct：共享EraserGripVisual结构，先从真实画布像素直径除frameZoom，再按Bar坐标绘制与裁剪。
