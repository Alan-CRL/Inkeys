# 设计:画布调试可视化模式

> 路径均相对 `Inkeys/Inkeys/`(源码根)。行号来自规划期调研,实现前需复核。

## 1. 总体划分

```
设置页开关 ──► Inkeys::Config (持久化)
     │                  │
     ▼                  ▼
IdtMain 启动同步 / Setting 热更新 ──► Host::SetCanvasDebugOptions ──► WindowControl 原子量
                                                                     │ (绘制线程 apply)
主栏 A1 按钮 ◄── 状态位 canvasDebugVisualActive (IdtState) ──► Host 原子量 ─┤
                                                                     ▼
                                   DrawingController (绘制线程): CanvasDebugOverlay
                                   ├ 采样: 帧时间环形缓冲(每帧 O(1)) + 0.5s 快照
                                   ├ 背景板: 合成阶段在 L2 之下
                                   └ 可视化标记: cursor 之后、Present 之前
```

三块职责:
1. **配置/UI**:开关、按钮、i18n(UI 线程侧)。
2. **状态传递**:`canvasDebugEnabled`、`canvasDebugVisual` 两个原子位 + 手掌橡皮/显示比例快照,沿现有 `Host::Set*Options → WindowControl → apply loop → DrawingController` 通路下发。
3. **渲染**:全部在 Draw3 绘制线程内,新建 `CanvasDebugOverlay` 单元(建议独立 `Draw3.CanvasDebug.cppm/.cpp`),避免继续膨胀 `Draw3.DrawingController.cpp`(已超 18k 行)。

## 2. 配置与设置页(R1)

- 字段归属 `Inkeys::Config`(现有调试字段均在此,符合 native-desktop 规范“Config ownership”门禁),放在 `Other.Config.cppm` 的 `Experimental.Inkeys3.UI3.Debug` 附近或 `Draw3.*` 组:`X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, CanvasDebug, false)`。一行即覆盖读/写/重置(`INKEYS_CONFIG_SCHEMA`)。**实现时需确认最终分组名,并向用户确认一次(规范要求)。**
- 设置页:`Setting.cpp` tab9(`settingTabEnum::tab9`,“调试软件”)渲染区加 `Design::ToggleRow`,模板为“脏区调试”开关(`:4833-4839`):改值 → 写 config → 推运行态 → `QueueConfigWrite()`;**不使用** `consoleToggle`(带“需重启”提示)。页内本地副本加到 `Experimental.Inkeys3` 结构(`:2601-2625`)。
- 启动同步:`IdtMain.cpp` `ReadAll` 之后(~`:3363`)推送初始值。
- i18n:先改 `src/i18n/zh-CN.jsonc`(`SettingsUI.DebugSoftware.*` 新增键 + 主栏按钮名/提示),再跑 `Scripts/i18n.ps1 sync/check`,**不手改** `IdtI18nKeys.g.h`。

## 3. 主栏按钮与“互斥”(R2)

**不新增 `StateModeSelectEnum` 值**(会波及 `ApplyExitSelectionLocked`、`IsComputerInteractionMode`、Draw3 桥工具映射,风险大)。改为:

- 新增独立状态位 `canvasDebugVisualActive`(`IdtState`,受 `stateModeTransitionMutex` 保护),语义“可视化模式是否开启”。
- 按钮:参照**穿透(Pierce)**按钮的运行时按钮模式(`Bar.Button.cpp:604-622`,`mainPierceButton`,默认隐藏、不进配置布局,`Load()` 里按条件插入 `activeButtons`),放在 A1 区。状态由 `ApplyMainPresentationLocked`(`:963-1085`)按 `canvasDebugVisualActive` 写 Selected/None,参照 Freeze 的 toggle 写法(`:1018`,`ToggleFreezeFromButton`)。
- 互斥由状态机统一保证:所有 `ChangeStateModeTo*`(`IdtState.cpp:2105-2261` 一带,及其他改变 `StateModeSelect` 的入口)在成功切换到非调试工具时清零 `canvasDebugVisualActive`。进入可视化模式的入口 `ToggleCanvasDebugVisualFromButton` 则:置位 + 把 Draw3 工具通知为“输入探测”(不出墨)。
- 画布调试开关关闭 → 清零该位并隐藏按钮。
- 若 A1 按钮需受 `IsFixedButtonsA1Id`/`NormalizeFixedZone` 白名单约束(`Bar.Button.cpp:1315`),则作为不入配置的运行时按钮绕开白名单;**实现时核对**这一点,避免被 normalize 丢弃。

### Draw3 侧“不出墨”
- 可视化模式下 Draw3 收到“调试探测”工具/标志后:接触仍照常入队 `ContactInputCoordinator`,但**不创建笔画、不橡皮、不触发手掌橡皮动作、不产生撤销历史**。具体做法(实现时选其一并在 implement 中记录):在工具映射层新增 `DrawingTool::Inspect`(仅 Draw3 内部),或在 apply 层对接触生命周期做短路。优先复用现有“穿透/选择时忽略接触”的短路点,减少新增分支。

## 4. 状态传递(热路径外)

- `Host::SetCanvasDebugOptions({enabled, visual})`,风格同 `SetPalmEraserOptions`(`Draw3.Host.h:419-425`);`WindowControl` 存原子;`Host.cpp` apply 循环(~`:1593/:1707`)比较后推给 `DrawingController`。
- 手掌橡皮阈值:复用 `PalmEraser::ThresholdAreaPixels()`(`Draw3.PalmEraser.cpp:61-79`)与 `PalmEraserOptions()`/`EraserDisplayScaleSnapshot()`,在 overlay 里只在选项变化时重算阈值并缓存,逐帧只做面积比较。
- cm 换算:`SpeedEraser::DisplayScale.cmPerPixelX/Y` + `physicalAvailable`(“可信 EDID”判定),只在 `physicalAvailable` 为真时显示 cm。

## 5. 采样与快照

| 数据 | 来源 | 更新方式 |
|---|---|---|
| 帧率 / 无限制帧率 | 绘制线程每帧已有的 `frameStartMs`、`workMs`(`:16009`、`:17967`);无限制 = 帧数 ÷ 活动工作时间(同 Bar 的 `OneSecondFrameRate` 语义,`Bar.FramePacing.cppm:148-210`) | 每帧 O(1) 累加;每 0.5s 结算为快照 |
| 帧负载 | `workMs ÷ 目标帧间隔`(目标 = 配置 `Drawing.FrameRate`) | 同上 |
| 进程 CPU | `GetProcessTimes` 差分 | 0.5s 采样一次(绘制线程在结算时调用即可,开销可忽略;不新增线程) |
| 帧率曲线 | 每 0.5s 结算的 FPS 压入 120 点环形缓冲(=60s) | 与快照同频 |
| 接触表 | `ContactSnapshot`(`Draw3.ContactInput.cppm:55-71`),绘制线程在读取接触处顺手更新“活动接触槽”表(固定容量数组,如 10 个) | 每次读到接触即更新槽;抬起后记录时间,~1s 后清槽 |

规则:快照结算时才做字符串格式化(落到预分配缓冲),逐帧绘制只读取缓存的格式化结果;**不在 RTS 回调内做任何处理**(规范禁止分配/日志),只在绘制线程消费侧采集。注意 Move 快照是 latest-wins,表里展示的是“绘制线程最近一次读到的值”,够用。

## 6. 渲染与脏区(R4、R5)

### 6.1 背景板在 L2 之下
现状:`CompositeLayersToBackBuffer`(`Draw3.DrawingController.cpp:10240`)用 `CopyResource(backBuffer ← L2, dirty)` 覆盖式复制,随后 `ApplyOperatorLayers` 叠 L1/L0;pan 回退路径用 `CompositeTrustedL2SnapshotToBackBuffer`(`:17788`)。L2 底为透明,呈现层是透明窗口。

方案:在合成阶段对 `dirty ∩ plateRect` 区域**改为“先画板、再 alpha 叠 L2”**,其余区域仍走原 `CopyResource` 快路径:
1. `dirtyOutside = dirty − plateRect`(拆成最多 4 个矩形)→ 原 `CopyResource`;
2. `dirtyInside = dirty ∩ plateRect` → 先渲染板子底(矩形圆角/半透明底色、文字、曲线、表格;可先画到缓存纹理 `plateTexture`,仅在快照/内容变化时重绘),再用 over 混合把 L2 对应区域叠上,最后照旧叠 L1/L0。
3. pan 回退路径同样处理(或在调试开启时强制走通用路径,以简化;实现时评估)。

板子内容缓存:`plateTexture` 仅在 0.5s 快照结算或接触表变化时重绘;其余帧只是“把缓存板子 blit 进 dirtyInside”。这样热路径代价 ≈ 一次小矩形 blit + 一次 over 混合。

### 6.2 脏区
- 记录 `plateRectPrev` 与 `plateRectCur`。板子内容/尺寸变化(快照结算、接触表增删行、尺寸变化)的那一帧,`frameDirty |= plateRectPrev ∪ plateRectCur`;其余帧不触碰板子区域,除非 L2/L1/L0 自身脏区与之相交(此时由 6.1 处理)。
- 最终提交脏区 = 板子脏区 ∪ 其他脏区(`RECT` 外接并集;两者相交/包含时自然覆盖)。规范里“overlay 的当前与上一帧 bounds 并入 frameDirty”同样适用于标记(`runtime-and-rendering.md:300`)。
- 板子尺寸变化不做动画,只在内容变更时重算 `plateRectCur`;使用固定列宽 + 等宽数字字形,避免宽度因数字位数抖动,减少无谓尺寸变化。
- 板子尺寸在屏幕坐标内取:左对齐 x=0,y=(屏高−板高)/2;超出屏宽部分裁掉(不换行)。

### 6.3 可视化标记
- 位置:在 `DrawTransientDrawingCursor` 同阶段(`:17833-17834`),`PresentFrame` 之前,画在所有笔迹/激光之上;不得写入 L0/L1/L2(规范 `runtime-and-rendering.md:285,326`)。
- 图元:实心小圆、描边圆、虚/实线矩形、文字。新增 `InkRenderer` 原语(`Draw3.RendererPrimitives.cpp`,仿 `DrawTransientDrawingCursor`),虚线用预设 dash 图案(或四条线段 + 间隔着色)避免逐帧几何分配。
- 每个标记的 bounds(含文字)当前帧与上一帧并入 `frameDirty`。
- 压感:`r = lerp(rMin, rMax, pressure)`,压力未知(`-1`)用固定基准半径;面积未知(`contactSize` 无效)不画红框。
- 红框实/虚:`area = w*h(px) > palmThresholdAreaPx && palmEraserEnabled`。

## 7. 信息表

- 固定列、等宽数字字体、每列固定宽度(按最大可能字符数预留)、右对齐数值,保证不对齐问题(PRD R4)。
- 初始列:类型(Touch/Pen/Mouse…,取 `InputSource.kind`)、X、Y、压力(未知显示 `--`)、面积 px、面积 cm(无可信 EDID 显示 `--`)、倾斜(tilt/orientation)。预留列注册表以便后期加“速度”。
- 行高/字号取紧凑值;表头固定,一行不换行。

## 8. 兼容性与回退

- 开关默认关闭;关闭时 `CanvasDebugOverlay` 不创建资源,合成路径与现状字节级一致(仅多一次原子位读取)。
- 回退:移除 `Host::SetCanvasDebugOptions` 与合成分支即可;config 键多余字段读入时被忽略,兼容旧版本配置文件。
- 构建:原生层文件 UTF-8 BOM + CRLF;新增 `.cppm` 需加入 vcxproj;全量 `InkeysRepo.sln` Debug 构建。

## 9. 风险

| 风险 | 缓解 |
|---|---|
| 修改合成路径影响正常画布 | 调试关闭时完全走原路径;调试开启时仅板子矩形内走新路径;加合成对拍测试 |
| 板子区域脏区/残影 | prev ∪ cur 规则 + 专项手测(书写/擦除/翻页/pan) |
| 状态位漏清导致可视化模式残留 | 清零集中在状态机的统一切换点,并加断言/回归用例 |
| 不出墨短路遗漏(手掌橡皮、撤销历史) | 在 Draw3 接触生命周期入口统一判断,而不是分散在各工具 |
| 文字渲染在热路径分配 | 板子缓存纹理 + 预格式化缓冲;仅 0.5s 重绘 |
| A1 白名单丢弃运行时按钮 | 实现时核对 `IsFixedButtonsA1Id` 与 `Load()` 插入逻辑 |

## 实现记录(步骤 1-3,已落地)

路径相对 `Inkeys/`(项目目录,源码根为 `Inkeys/Inkeys/`)。

### 配置与设置
- 配置键 `Experimental.Inkeys3.Draw3.CanvasDebug`(`IdtAtomic<bool>`,默认 false,NoUpload),在 `Inkeys/Inkeys/Other/Other.Config.cppm`。放在 Draw3 组而非 UI3.Debug(UI3.Debug 语义是 UI3 脏区调试)。
- 设置页 tab9 新增 `CanvasSection` 小节 + `##experimental-canvas-debug` ToggleRow(`Setting.cpp`),热生效:`SetCanvasDebugEnabled()` + `QueueConfigWrite()`,页内镜像 `Experimental.Inkeys3.CanvasDebug`。
- i18n:`SettingsUI.DebugSoftware.CanvasSection / CanvasDebug.{N,E}`、`UI.Bar.MainButtons.CanvasDebugLabel`;zh-CN 先改,`i18n.ps1 sync` 后其余 19 语言补全译文,`check` 全部 100%。主栏按钮无 tooltip 机制,只有 Label。
- 启动同步:`IdtMain.cpp` 在 `SetPalmEraserOptions` 之后调用 `PublishCanvasDebugOptions()`。

### 状态位(IdtState)
- 可视化位不新增 `StateModeSelectEnum`,也不放进 `StateModeClass`,而是 `canvasDebugVisualRevision`(原子):进入时记录当时的 `stateModeTransitionRevision`,`IsCanvasDebugVisualActive()` = 开关开启 && 记录值 == 当前修订。**任何后续工具/模式修订变化(含重复点击同一工具)都会自动使该位失效**,因此无需在每个 `ChangeStateModeTo*` 中手动清零,也不会漏清。开关关闭时显式清零。
- `ToggleCanvasDebugVisualFromButton()`:持模式锁;若当前在选择/穿透,先恢复 `lastAnnotationMode`(Pen/Shape/Eraser,否则 Pen),使 Draw3 收到接触;再 bump 修订并记录。再次点击=退出,工具保持不变。白板切换中/事务中拒绝。
- 无修订 bump 却不走 `SyncDraw3State` 的两条路径(重复点击同一笔型、重复点击同一形状)补了 `PublishCanvasDebugOptions()`。其余路径经 `PublishDraw3StateLocked` 发布。

### A1 按钮(Bar)
- 新增 `BarButtonPresetEnum::CanvasDebug`、`mainCanvasDebugButton`(运行时按钮,仿 Pierce/Redo,默认 hide)。不在 `FixedButtonsA1`,因此不受 `NormalizeFixedZone`/`IsFixedButtonsA1Id` 影响;`Load()` 在固定区工具之后(Select→Draw→Eraser→Geometry 中最靠后者)插入,A1 被精简时退到最前。
- `ApplyMainPresentationLocked`:`hide = !IsCanvasDebugEnabled()`,`state = Selected` 当可视化位激活(两者都是无锁读取,避免在表现锁内嵌套模式锁)。
- `Bar.RenderLoop.cpp` 增加 `AnchorHiddenButton(CanvasDebug, CanvasDebug)`;`Bar.Interaction.cpp` 将其加入点击后还焦点的列表。
- 图标 `barCanvasDebug.svg`(准星:圆环+中心点+四向刻度,只用 M/L/Z),已登记 `Inkeys.rc` / `.vcxproj` / `.filters`。

### Host → Draw3
- `Host::SetCanvasDebugOptions(enabled, visual)`/`CanvasDebugEnabled()`/`CanvasDebugVisual()`:`Impl::canvasDebugOptions` 位掩码原子(bit0 开关,bit1 可视化;enabled=false 时规范化为 0),变化时 `PublishControlWake()`;`PumpBridgeState` 在绘制 owner 应用到 `DrawingController::SetCanvasDebugOptions`,Controller 重建时重置已应用缓存。
- `DrawingController`:`canvasDebugOptions_`(原子)、`GetCanvasDebugEnabled()`、`GetCanvasDebugVisual()`(== 3)。未使用 WindowControl 原子(无需:只有绘制线程消费)。

### 不出墨的选择(重要)
- 没有复用 `DiscardSelectionDown`:它调用 `DiscardUntilTerminal`,contact 进入 Quarantined 后 `TryReadSnapshot` 读不到,覆盖层无法获得 Move/压力/面积。也没有新增 `DrawingTool::Inspect`(会波及桥接/断触续接/测试)。
- 做法:`processCommand` 在 `ContactAdmitted`/选择/加载门禁之后、`initializeStroke` 之前,若 `canvasDebugOptions_ == 3`,调用 `registerCanvasDebugProbe(handle)` 并 return。探测接触只持有 `ContactHandle` + 最新 `ContactSnapshot`(`CanvasDebugProbeContact`,局部 `debugProbeContacts`,与 `gestureContacts` 同级),**不创建 RuntimeStroke**,因此不建模、不绘制、不擦除、不触发手掌橡皮、不写历史/撤销、不触发 haptics、不进入 Touch 平移手势。路由仍为 Producing,Move 继续覆盖最新快照。
- `pumpCanvasDebugProbes()` 在每帧 `updateCanvasNavigation` 之后调用:刷新快照,终态(Up/Cancelled)读到后 `input_.Recycle`。**步骤 4-6 的采集/标记渲染挂在这个函数里**(注释处),读取 `probe.snapshot` 与 `probe.handle.record->DeviceType()`。
- 探测接触存活时 `navigationActive` 为真,绘制线程按帧率节拍运行(不阻塞在空闲等待);`debugProbeContacts` 为空时与原路径行为一致(多一次原子读取 + 空 vector 判断)。`sealPresentationContacts` 会对探测接触 `DiscardUntilTerminal` 并清空。
- 已在进入可视化前建笔的 contact 按原路径正常收尾。

## 实现记录(步骤 4-6,已落地)

路径相对 `Inkeys/Inkeys/Drawing/Draw3/`。

### 新增单元
- `Draw3.CanvasDebug.cppm/.cpp`(模块 `Inkeys.Drawing.Draw3.canvas_debug`,纯 CPU,无 D3D 依赖):`Overlay` 持有采样、0.5s 快照、120 点帧率环、10 槽接触表、标记几何,以及板子/标签位图的栅格化。文字用 GDI 一次性烘焙 ASCII 灰度字形图集(`BuildAtlas`,Consolas,按 DPI),绘制阶段只做内存拷贝;每个字符按固定字格定位,数字长短不会使其他列错位。列注册表 `kColumns`(表头/宽度/对齐/格式化函数),后期加“速度”列 = `ContactSample` 加字段 + 追加一项。
- `Draw3.RendererDebugOverlay.cpp`(`InkRenderer` 的实现单元):板子纹理、标签纹理、b3 常量缓冲、point 采样器、under 混合状态;`EnsureDebugOverlayResources/ReleaseDebugOverlayResources/UploadDebugPlate/UploadDebugLabel/SetDebugPlate/DrawDebugPlateUnder/DrawDebugLabel/DrawDebugMarkerShapes`。**没有新增 HLSL**:复用 shape 20 的矩形采样 pass(与可信 L2 快照相同的 b3/t14),形状标记复用 shape 16..19,圆点/圆框复用 `DrawTransientDrawingCursor`。`DrawShapePrimitives` 增加两个带默认值的参数(混合状态/圆角覆盖),默认行为不变。
- 离屏验证:`RunCanvasDebugOverlayValidation`(`Inkeys.exe --draw3-canvas-debug-test`,WARP,无 HWND/swapchain)与 `InkeysHeadlessTests --canvas-debug-only`(纯 CPU)。

### 与设计稿的偏差(均有意为之)
1. **板子在“全部墨迹层之下”,而不是“只在 L2 之下”**:合成顺序是 `CopyResource(L2) → ApplyOperatorLayers(L1/L0) → DrawDebugPlateUnder`,板子用 `INV_DEST_ALPHA / ONE` 的 under 混合补在已合成内容的透明度之下(与可信 L2 快照同一技巧)。这与设计稿“先画板、再 over L2”像素等价,但不需要把 dirty 拆成 4 个矩形;并且 L1 橡皮 live 预览擦的是墨迹而不是板子(板子先于 L1/L0 绘制会被实时橡皮擦掉,提交后才恢复,会闪)。已用离屏读回验证:橡皮擦除后露出的是板子,关闭时为透明。调试关闭时 `DrawDebugPlateUnder` 只检查一个布尔值,合成输出字节一致(已验证)。pan 回退路径同样调用。
2. **接触行数值刷新上限 10Hz(`kContactValueRefreshMs=100`)而不是 2Hz**:行的增删/抬起立即重绘;性能数字和曲线严格 0.5s 结算。若坚持 PRD R5 的 ≤2Hz,改这一个常量即可。
3. **手掌阈值**:只对 `InputDeviceType::Touch` 且手掌橡皮总开关开启时才有阈值(与 `PalmEraser::State::Observe` 同源:`ResolveScale + ThresholdAreaPixels`,面积 `>=` 阈值、宽高比 ≤3.5);未按工具类别(`tools[7]`)细分,因为可视化模式下没有当前绘制工具。阈值按(display generation/revision、手掌设置、来源)缓存。
4. **帧率口径**:当前帧率 = 0.5s 窗口内“成功呈现的帧数”/窗口时长;无限制帧率 = 帧数 ÷ 帧工作时间(不含 pacing 等待);帧负载 = 平均帧工作时间 ÷ (1000/`timingProfile.target_fps`);进程 CPU = `GetProcessTimes` 差分 ÷ (墙钟 × 核数)。**仅由调试板自刷新触发的帧(脏区完全落在调试注入范围内、无 forceFullPresent)不计入帧率**,否则静止画布会显示成 2 FPS。
5. **空闲唤醒**:调试开启时,完全空闲的分支改为 `WaitForWake(最近一次需要更新的时刻)`(≤0.5s),沿用 Hold/重试分支已有的“tryConsumeOneIngress → WaitForWake → continue”模式;关闭时仍是原来的 `WaitDequeue` 零唤醒。性能数字/曲线在显示精度(0.1)内无变化、且没有接触行变化时不会重绘,所以静止且稳定后不再产生帧(曲线填满 120 点且全 0 之后)。
6. 所有板内文字为英文(Type/X/Y/Prs/Area px/Area cm/Tilt/Dir/FPS/Unlimited/Load/Proc CPU),不进 i18n:调试专用,且字形图集只烘焙 ASCII。

### 接线要点(`Draw3.DrawingController.cpp`,`Run`)
- `prepareCanvasDebug`(紧跟 `navigationActive` 计算、idle 判定之前):采集活动 runtime / 手势接触 / 探测接触 → `Overlay::Update` → 确保 GPU 资源并上传变化的位图 → 把板子/标记的 prev∪cur 并入 `frameDirty`。关闭开关的那一次迭代用 `VisibleBounds()` 清残影并释放 GPU 资源。
- 呈现帧中 `frameDirty` 非空时,再并入当前全部标记 bounds(标记每个呈现帧完整重绘,其区域必须先被重新合成,避免半透明像素叠加,与 cursor 同一规则);标记在 `DrawTransientDrawingCursor` 之后、`PresentFrame` 之前绘制,只写 backbuffer。
- prev bounds 在 `Update` 时就提交(不等 Present 成功):Present/栅格失败都会 `RequestFullPresent`/`activeLayerRebuildPending`,下一帧整屏重合成,不会遗留残影。
- 设备恢复:`ReleaseResources` 会释放调试资源;下一迭代 `EnsureDebugOverlayResources` 报告 `recreated`,调用 `MarkGpuStale` 重新上传并脏化板子/标记区域。创建失败只记录一次并在本会话禁用覆盖层 GPU 部分。

## 修订(用户反馈)
- 可视化是与所有工具互斥的模式:进入后选择/绘制/橡皮/几何/穿透按钮一律显示未选中,点击任一工具即退出(含点击底层借用身份对应的工具);按钮位于 A1 “清空”之后。
- 打开“画布调试”开关自动进入可视化;要测试绘制等需自行切换工具。启动时持久化为开启不自动进入。
- 画布调试开关不持久化(已移除 `Experimental.Inkeys3.Draw3.CanvasDebug` 配置键),改为 `IdtState.cpp` 内的进程内原子,每次启动默认关闭。
- 可视化标记存在的帧是真实画布帧,计入帧率;只有纯板子自刷新帧不计入(此前触摸时帧率显示为 0)。

## 修订:接触表扩展(用户反馈)
- 新增列:`Src`(设备来源分类 ExtPen/IntPen/Touch/Pad/Mouse)、`Path`(输入通路 RTS / RTS-XY / Win32)、`Id`(RTS `tabletContextId:contactId`,鼠标兼容通路为 `--`)、`Alt`(高度角 = 90° - tilt)、`Azi`(方位角 = 360° - orientation)、`Spd px/s`、`Spd cm/s`(无可信 EDID 为 `--`)。原 `Tilt`/`Dir` 列由 `Alt`/`Azi` 取代。
- 性能区新增 `Input` 行:整体输入通路(`HostInputMode` 数值:1 RTS 扩展、2 RTS 基础 X/Y、3 Win32 鼠标),由 `Host` 在 `PumpBridgeState` 推给 `DrawingController::SetCanvasDebugInputMode`。
- 速度由 Overlay 用相邻输入快照的坐标差与 QPC 差测得(≥4ms 结算 + 指数平滑,100ms 无新输入归零),不依赖橡皮的运动估计(后者只在速度橡皮模式存在)。因 Move 快照是 latest-wins,测得的是被绘制线程读到的采样之间的平均速度。

## 修订:阶段/序号/延迟与板子锚点(用户反馈)
- 接触表再增列:`Phase`(Down/Move/Up/Cancel,取自快照 `ContactPhase`)、`Seq`(记录发布序号/2,即已发布次数;Move 是 latest-wins,不等于 RTS 包数)、`Lat ms`(输入到上屏延迟)。
- 延迟 = 绘制线程读到的最新输入快照的 QPC 时间戳 → 承载它的那一帧 `Present` 返回;每个新快照只结算一次,指数平滑;调试板自刷新帧不结算。不含 DWM 扫描输出(至多再多一个刷新周期)。
- 板子位置改为悬浮:锚点 = 全部列、满行数的最大板子在画布内水平、竖直居中的位置(`(画布 - 容量) / 2`,下限 0)。内容变化时左端、上端不动,只向右、向下生长;紧凑状态因此位于水平居中偏左、竖直居中的上沿。四个圆角都圆(不再贴边)。脏区仍是 prev ∪ cur。
- `Id` 列改为 `cid:x`:cid 是 RTS cursor/stylus id(`record->ContactId()`);x 是断触续接前一个接触的 cid,不是续接为 0。不再显示 tablet context id(真机上它是很大的数,且对排查续接没有帮助)。x 由 `RuntimeStroke::debugPreviousContactId` 提供,在唯一的生产接管点 `AdoptReconnectedContact` 之后写入,链式续接显示紧邻的上一个 cid。可视化探测接触不会续接,x 恒为 0;要看续接需切到绘制工具。

## 顺带改动:断触接续适用设备
与画布调试无关,但同一分支提交:设置-绘制-触摸的“断触接续”开关下方内联“触摸”“笔（笔尾）”两个复选框(默认都开,持久化),总开关关闭时用控件动画收起。详见 `configuration-i18n-and-assets.md` 的“断触接续的适用设备”。
