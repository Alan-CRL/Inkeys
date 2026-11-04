# Bar 已呈现 Touch Probe 实现记录

- 日期：2026-11-03
- 接口：`BarUISetClass::ProbeTouchScreen(POINT screen) const -> Inkeys::Input::UiPointer::Touch::Probe`
- 对外注册声明：`Inkeys::UI::Bar::RegisterNativeTouchInput(HWND)`；接线由主实施者负责。

## 提交与线程边界

`PreparePresentedButtonHits(const StateModeClass&)` 在既有成功呈现分支准备 curated 小值区域；RenderLoop 仅修改现有一行调用以传入该次绘制锁存的 `state.stateModeSnapshot`，不重读当前配置/工具目标。橡皮与撤回面板通过 `friend BarUISetClass` 让 render owner 读取同次绘制的 `frame_`，避免本次 Prepare 先于面板 CommitPresented 时混入上一帧 `presented_`。

新快照只包含区域圆角/超级椭圆几何、Rigid/Body/Grip坐标类别、Accept/Blocked处置与准入语义哈希，不保存控件业务对象。候选的全部分配在 seqlock 奇数提交段之外完成；`PublishPresentedButtonHits` 在原 mutex 下与两个既有 hit map 同次交换。native probe 短锁复制 `shared_ptr<const snapshot>` 以及直移吸收的 DIP 偏移，然后释放锁；它不读取 `.val`、当前 BarState/工具配置、面板 Pointer，不查询 WindowFromPoint，不申请 capture/lease。

Probe 前后复核同一 `BottomDockPresentedSnapshot.serial`，全部 screen→layout、Rigid、BodyFromRigid、GripFromRigid 计算均使用该次快照；不把屏幕点直接除 zoom，不混读各 helper 的独立快照。直移吸收时，现有 map 平移与 Touch snapshot 的累计 rebase 在同一 mutex/serial 事务内推进。语义值只含工具、面板准入/锁定 flags 和 display serial，不包含每帧动画、画笔颜色/粗细，Down 准备与 capture 后复核不会因普通重绘改变。

## 区域覆盖与顺序

| 优先级/组 | 已呈现区域 | 坐标/处置 |
| --- | --- | --- |
| BottomDock 指示器 | 既有成功呈现的无阴影 hit bounds 与 indicatorVisible/occluding | visual layout pixels；沿原 helper 仅阻挡 |
| Eraser | 当前 frame 的圆角 menu、panel；同 frame scale/radius | Rigid；开放时Accept，仍可见退场时Blocked |
| Recall | 当前 frame 的圆角 panel | Rigid；开放时Accept，退场Blocked |
| ColorPicker | 圆角 panel、palette、tone toggle、close hit | Rigid；预览气泡/hold装饰不登记 |
| 提示与笔型菜单 | Annotation/Overflow正文与X、InfoHit；PenTypeMenu及两项 | Rigid；hover/pinned与可用门，退场正文Blocked；HoverGrace不单独复活旧区域 |
| More | 实际圆角panel、close、explicitMore与forcedOverflow的已呈现button hit | Rigid；退场背景Blocked |
| Main | 实际 MainButton 超级椭圆、主栏按钮、MainBar圆角背景 | 主按钮Grip（折叠仍可用），按钮/背景Body；已禁用按钮Blocked |
| Geometry | 圆角属性背景、直线/矩形/三档粗细/close | Rigid；形状工具门，退场背景Blocked |
| Thickness | PreviewPopup真实圆角surface、SliderHit、FineDial ownershipCorridor及dragZone | Rigid；Popup优先；未ready走廊阻挡，ready dragZone可接收 |
| Draw | 圆角背景、12色块、硬笔/软笔/荧光笔/激光等笔型、粗细预设/Adjust/扩展入口 | Rigid；沿工具/laser/预设能力门 |

透明 hit area 不使用其自身 pct 判断（原本就允许为0）；所属真实背景须已呈现，避免 logical open 早于首帧时旧透明几何提前接管。背景消退仍有像素时登记 Blocked，完全退场不登记。未泛化窗口rect、shapeMap全量或shadow为接受区。

## FineDial 原生候选边界

FineDial 的 FreshActivation 确实支持**新的 Down**：Interaction 的 HandleThicknessAndFineDialPointerStage 在 `FreshActivation==Drag && WM_LBUTTONDOWN` 开始手势。

透明的是 ThicknessSelect/走廊自身；Layout 的 ownershipCorridor 横向沿 preview.trackLeft/right，纵向从 thumb外缘夹到 panelTop/panelBottom；RenderLoop 的 ThicknessSelect 正常展开时 x=5、宽240，位于宽370的 DrawAttributeBar 内，后者以非零 SurfaceOpacity 填充圆角背景。主体走廊有真实背景像素，可满足严格原生候选，不需要 canvas fallback。

面板最外边界的圆角裁掉的小角（例如 x=5、y恰在panelTop）仍可能是 alpha0；严格 WindowFromPoint 只承认实际返回的五窗之一，不把落到Drawpad的点强行映回Bar。本实现没有改alpha、布局或普通Mouse策略；未以纯几何Probe宣称所有alpha0点都能通过native候选。需要实机像素/窗口测试才能确认的驱动行为仍交整体测试。

## 验证

- 子任务仅静态检查：调用签名/唯一调用同步、curated库存逐项核对、commit/serial锁顺序、原BOM与CRLF、`git diff --check`。
- 构建与无窗口生产探针由主会话统一执行；本子任务未build、未GUI、未commit/push。

## 追加：真实 Prepare/Publish/Probe 无窗口生产自测

`RunBarTouchProbeProductionTest()` 在 Button.cpp 内用局部派生 fixture 调用真实生产 Prepare/Publish/Probe，Main.cppm 导出声明；主会话在既有 `--ui3-input-worker-test` 的 running guard 后加一行接入。fixture 不创建 HWND、D2D device 或渲染线程，不修改全局 Bar/工具状态，也不替换 Probe 或复制命中算法。

覆盖：首次未提交Miss、Prepare未Publish仍Miss；非单位zoom、负monitor origin与direct translation下不同的Rigid/Body/Grip；真实标准主栏button及disabled优先阻挡；MainBar/Draw圆角和MainButton超椭圆外Miss；关闭透明hit与preview装饰排除；逻辑open但背景未呈现时不复活旧色块；颜色/粗细变化不扰动准入semantic；实时形状/flag变化及模拟失败候选不覆盖已提交snapshot；退场背景Blocked/完全退场Miss；折叠时Grip仍Accept而Body退场Blocked。

这是生产快照发布边界与真实命中函数的无GUI回归：手工固定屏幕样本与分离的映射尺度使错误地统一使用某一坐标空间会失败。模拟呈现失败通过“不调用Publish”覆盖快照合同，未执行ULW API；它不证明WindowFromPoint、alpha像素与真实驱动原生路由。构建/执行交主会话，子任务仍不自行build。
