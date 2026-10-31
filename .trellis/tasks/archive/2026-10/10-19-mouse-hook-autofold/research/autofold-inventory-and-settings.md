# 自动收起旧功能清单与 Inkeys3 设置方案（只读规划）

本报告仅调查并提出实施建议；未修改源码、配置、工程、生成资源或规格，未构建、测试、启动 GUI、提交。读取了任务 `planning-boundary.md`。下列后续用户决定由主会话转达，优先于该文件内较早的需求快照：

- 四项配置全部放在 Inkeys3 Config；**不继承、不迁移 Inkeys2 的旧配置值**。
- 新独立默认值已确认：绘制为“非底栏模式时”；擦除、拖动主栏、选择/穿透下点栏外为“关闭”。
- **合并式和分离式均显示全部选项，均生效**；删除原先仅分离式显示/生效的提案。
- 底栏包括未居中的底栏和居中底栏。

## 1. Inkeys2 完整旧设置清单

通过当前保留的旧源码、三语资源和本地历史 `41535eaf^:Inkeys/Inkeys/UI/Setting/Setting.cpp` 核对，自动收起配置共四项；没有找到独立的自动展开/悬停展开设置。“Recover”在这里实际表示收起主栏，并非自动恢复展开。

| 旧中文标签及 i18n key | 旧内存字段 | 旧 deploy.json 键 | 旧默认 | 真实触发语义 |
| --- | --- | --- | --- | --- |
| 画笔绘制时收起主栏；`SettingsUI/Regular/Behavior/DrawingRetract` | `setlist.BrushRecover` | `BrushRecover` | true | 绘制线程消费新笔迹，实际本笔工具为 Pen，且主栏已展开时，写 `target_status = 0`；不是点击画笔按钮立即收起。 |
| 橡皮擦除时收起主栏；`.../ErasingRetract` | `setlist.RubberRecover` | `RubberRecover` | false | 同一新笔迹边界，实际本笔为 Eraser 时收起；旧代码先将倒转笔尾和鼠标右键投影为橡皮，之后判断该项。 |
| 拖动主栏时收起主栏；`.../DraggingRetract` | `setlist.regularSetting.moveRecover` | 实际写入 `Regular.MoveRecover1` | false | 旧 `SeekBar` 累计鼠标路径长度大于 20 后收起；不是按下立即收起。 |
| 点击时收起主栏；`.../CheckingRetract`；说明 `.../CheckingRetractE` | `setlist.regularSetting.clickRecover` | `Regular.ClickRecover` | false | 选择或穿透状态的全局左/中/右键 Down，延后 100ms 确认；主栏自身收到同次 Down 会撤销候选，从而实现“点非主栏区域”。 |

证据：

- 四项字段：`Inkeys/IdtConfiguration.h:31-35`；旧默认：`Inkeys/IdtMain.cpp:2439-2442`。
- 旧配置读取：`Inkeys/IdtConfiguration.cpp:266-277`；输出：`546-551`。其中旧拖动 reader 检查 `MoveRecover` 是否存在，却读取并检查 `MoveRecover1`，二者不一致。新功能不迁移旧键，因而不能照抄该逻辑，也无须为了迁移去修历史 reader。
- 旧笔迹开始：`Inkeys/IdtDrawpad.cpp:2216-2233`，其中 `2225-2229` 先确定实际橡皮，再判断收起；原有“收次级面板”分支与主栏折叠不是同一个设置。
- 旧拖动：`Inkeys/IdtFloating.cpp:239-277`；`ret` 是逐点欧氏路径累计值，`ret > 20`，不是与 Down 的直线距离。
- 旧点击触发：`Inkeys/IdtFloating.cpp:491-499`；100ms deadline 和消费：`294-309`、`361-371`；栏内取消：`822-829`。
- 原中文标签/说明：`Inkeys/src/i18n/zh-CN.jsonc:390-394`；繁中、英文同位置。旧设置 UI 的四个 toggle 可在上述本地历史文件中直接查看。

## 2. 当前 Inkeys3 的缺口与已迁移路径

### 2.1 设置 UI 的四行已移除，不是简单注释

`Setting.cpp:1262-1267` 还保留四个旧字段的局部快照，但全文件不存在四个标签的绘制引用。当前常规行为区 `Setting.cpp:3653-3797` 在置顶间隔、右键关闭卡片后直接进入实验选项，没有旧自动收起卡片。恢复应增加新的三态控件，而不是将历史 UI 的四个 bool toggle 原样粘回去。

当前合并/分离设置位于 `Setting.cpp:3056-3128`，由 `GetSelectAndClickThroughMode()`、`SetSelectAndClickThroughMode()` 管理，持久化 `UI.Bar.SelectAndClickThroughMode`。后续要求已取消这里的门控：新增自动收起块不嵌在 `mode == 1` 分支下，运行时也不得检查该值后才收起。

### 2.2 两种样式共享同一个 fold 状态

- 样式投影集中在 `Bar.Button.cpp:927-960`：`separate` 用来投影 A2/选择/穿透等按钮，同时读取的 `fold` 来自共享 `barUISet.barState.fold`。
- 手动主按钮折叠/展开：`Bar.Interaction.cpp:4159-4178`，不区分合并/分离。
- 呈现使用同一个 `state.barState.fold` 动画：`Bar.RenderLoop.cpp:1480-1504`；主栏及其按钮布局/命中也统一受 fold 控制。

因此无需实现两套折叠机制。策略矩阵必须同时验证 Combined/Separate，按钮工具身份和选择/穿透合同仍沿原路径保持。

### 2.3 Draw3 已有落笔通知，但只有 bool

- 真实 Draw3 接口链：`IdtMain.cpp:3242-3247` 的 `HostRuntimeCallbacks.drawingActivityChanged` → `NotifyCanvasDrawingStarted/Ended`。
- Bar 对并发活跃笔迹计数，仅首个 0→1 PostMessage：`Bar.Interaction.cpp:7706-7724`；窗口消息入口：`1091-1094`；处理：`6688-6701`。
- 当前处理固定调用 `ClearRecallForDrawing()`、`CollapseAuxiliaryPanels(true)`，并明确“主栏本身保持用户当前展开状态”。这是新主栏策略需要接入的位置之一，不能因为主栏选“关闭”就连原有次级面板关闭规则一起撤销。
- 次级面板关闭涵盖 Eraser/Recall、drawAttribute、geometryAttribute、More、笔型菜单、tooltip、粗细滑条和颜色面板：`Bar.Interaction.cpp:1427-1438`。
- 但该 bool 的 controller 含义是 `HasPhysicalContact(active) || input_.HasQuarantinedContacts()`：`Draw3.DrawingController.cpp:8068-8075`，不携带实际工具，也可能代表隔离中的 contact。不可仅在收到 bool 后读取当下全局选中工具，便当作“本次画笔/橡皮开始”。
- 实际工具的确可能被临时覆写：`Draw3.DrawingController.cpp:8718-8730` 将倒转笔或鼠标右键覆写为 Eraser，和全局所选工具可能不同；新橡皮策略必须识别这类真实擦除。

可复用的最小元数据路线：controller 已有 `CompletedStrokeKindForTool`（`Draw3.DrawingController.cpp:206-213`）将本笔实际 `DrawingTool` 分类成 Drawing/Eraser/Shape；`DrawingControllerRuntimeObserver`（`.cppm:66-87`）及 Host 已经是统一跨层通知链。建议复用这种 value-only 分类，在真实笔迹入场边界添加窄的“已接受本笔开始”观察/转发，沿现有 observer→HostRuntimeCallbacks→Bar 命令路线走，不建立第二套输入 producer，不把 Raw/Hook 接入 Draw3。现有完成通知不能用于这次功能，因为它在笔迹结束后发布，时机过晚。

每个已接受笔迹最多一次通知；多指重叠下后来的实际橡皮仍应有机会触发 Erasing 策略，不能只靠全局 0→1 丢掉后来的工具类别。拒绝/隔离 contact、纯 hover、尾迹消散和重放不得触发收主栏。原有 bool activity 可保留用于次级面板和边框光照生命周期。

该窄扩展预计仅涉及 `Draw3.DrawingController.cppm/.cpp`（observer 字段与已接受初始化边界）、`Draw3.Host.h/.cpp`（RuntimeCallbacks 转发）、`IdtMain.cpp`（产品回调接线）、`Bar.Main.cppm` 与 `Bar.Interaction.cpp`（通知接口、交互 owner 消费）。沿用实际 runtime tool 的现成分类，属于元数据边沿扩展，不修改 ink 算法、接触过滤、采样/拟合、RTS producer 或现有聚合 activity 的定义。若不能直接复用名称为 Completed 的 enum，可用同构的小型事件 kind，仍从现成分类函数提取，不复制工具判断树。

### 2.4 当前鼠标钩子仍有旧点击收起路径

- 活动工程编译 `Inkeys/Input/MouseHook.cpp/.cppm`：`Inkeys.vcxproj:865-866`；旧 `IdtFloating.cpp` 和 `IdtDrawpad.cpp` 均是 `None`：`1206`、`919`，不可为本任务恢复为编译源。
- 当前鼠标 Hook 的选择/穿透 Down 分支仍读旧 `clickRecover`：`MouseHook.cpp:132-137`；100ms 定时及 callback：`45-49`、`60-75`。
- Bar 初始化给 callback 直接写 `fold=true`：`Bar.Initialization.cpp:204-208`。这既绕开新的 Config 策略，又没有复用手动折叠的 dock/面板收尾步骤。
- 当前 Bar WndProc 清的是 `ConfirmaNoMouMsgSignal`（`Bar.Interaction.cpp:934-937`），活动 `MouseHook` 的 pending 则是自己的 `clickCollapsePending`，`CancelPendingCollapse()`（`MouseHook.cpp:238-241`）目前没有活动调用者。不能凭栏内清旧标志就宣称当前点栏外判定正确。

Hook worker 具体性能和轮询问题由本任务其他研究负责；本报告要求 Hook 只产生小型、非阻塞的“栏外点击候选”，Bar owner 在消费时验证配置/工具/会话/点位，实际折叠不在低级 Hook 回调执行。

## 3. 统一三态策略、默认和真实触发

建议共享枚举的持久化值固定为：

| 值 | 内部建议名 | 三语显示示例 |
| --- | --- | --- |
| 0 | Off | 关闭 / 關閉 / Off |
| 1 | OnlyWhenNotBottomDocked | 非底栏模式时 / 非底欄模式時 / When not bottom-docked |
| 2 | Always | 任何时候 / 任何時候 / Always |

四项均适用这个 enum，无需给任何一项伪造不同含义。“任何时候”只去掉底栏限制，仍需对应真实触发、主栏当前展开、有效生命周期和工具条件；不是持续循环强制折叠，也不替代手动折叠/场景显隐。

已确认的新独立默认与建议命名：

| Inkeys3 Config 建议路径 | 独立默认 | 触发 |
| --- | --- | --- |
| `UI.Bar.AutoCollapse.Drawing` | 1 | 实际画笔族开始绘制；普通软笔/硬笔/荧光笔/激光笔均属于画笔族。 |
| `UI.Bar.AutoCollapse.Erasing` | 0 | 实际 Eraser 笔迹开始；包括右键临时橡皮和支持的倒转笔尾。 |
| `UI.Bar.AutoCollapse.Dragging` | 0 | 主按钮拖动跨过现有真实拖动阈值，达到连续移动状态后。 |
| `UI.Bar.AutoCollapse.OutsideClick` | 0 | 电脑操作状态（Selection 或 ClickThrough）下点击 Bar 可交互区域之外。 |

形状是否计入“画笔绘制”是旧功能无法唯一回答的产品细节：旧代码只判断 IdtPen，没有当前独立 IdtShape 对应自动收起项。最小恢复建议 Shape 不触发 Drawing；若主会话决定“绘制”泛指全部标注，则沿复用的 Shape 分类显式纳入，必须写入 PRD，不能靠 `tool != Eraser` 偷偷扩大。激光笔为当前 Pen 家族（`IdtState.h:176`、`IdtState.cpp:1272` 等），可纳入 Drawing。

不新增“自动展开”，松手/抬笔时保持已收起状态。关闭设置仅影响后续触发，不立即展开。切换 Combined/Separate 不清空这四个偏好，不重复触发；两种样式均显示和生效。

## 4. 底栏判定与拖动中折叠边界

`Bar.BottomDock.h:29-38` 将竖向模式 `Floating/BottomDocked` 与横向 `Free/Centered` 分成两维。策略 1 仅当实际竖向模式为 Floating 才允许，所以 BottomDocked+Free 和 BottomDocked+Centered 均不收起。

不能用屏幕 Y 距离、StartupPlacement、Centered 与否来代替真实底栏状态。启动位置只是下次启动偏好；策略判定应沿 Bar owner 当前一致的状态快照，不能让 Hook 把不一致的多个原子值拼起来决策。`Bar.Main.cppm:984-996` 明确目标由交互线程发布，render 只回写视觉偏移/恢复状态；呈现快照由 `BottomDockPresentedSnapshot()` 统一读取（`614-644`）。

主按钮拖动有专门合同：

1. 复用当前 `ShouldAllowBarBottomDockClick` / `BeginBarMouseDrag` 判定（`Bar.Interaction.cpp:7488-7492`），不照搬旧 20px 累计阈值到新 DPI/dock 语义。
2. 若策略 1 的一次拖动从 BottomDocked 真实脱离到 Floating，再在后续已接受样本上可收起；仅有底栏弹性形变、横向由居中变自由仍不算非底栏。若初始 Floating，则越过真实拖动阈值即可。每次手势最多折叠一次，不在穿越模式时反复展开/收起。
3. **不能直接随手写 fold**：`TryReadBarPointerMessage` 在 `Bar.Interaction.cpp:681-688` 会因 fold 相对 Down 时变化而取消当前手势。受控自动折叠应由同一个 Bar 交互 owner 作为本次会话自己的动作处理，仅更新该会话的 fold 基线/允许一次该来源折叠；外部手动折叠、工具变化、来源替换、桌面失效仍照常取消。
4. 同时处理折叠带来的宽高/dock-center 布局屏障、`ClearBottomDockCenterForFold()`、面板关闭等既有步骤，但不得重置绝对指针抓点，不将视觉限位结果回写抓取偏移。必要时发布当前既有 presentation barrier，让新 fold 布局上屏再继续 direct move。
5. 折叠不能触发主按钮 click pulse、不能把真实拖动重新解释成点击；松手仅完成一次拖动持久化。Mouse HiMsg Down/Up 与 Raw drag supplement、WM_TOUCH→HiMsg、source/contact/generation 合同保持不变。

## 5. 配置与设置 UI 实施边界

### Canonical owner

建议在 `INKEYS_CONFIG_SCHEMA` 现有 `GROUP(UI, GROUP(Bar,...))` 中增加 `AutoCollapse` 小组，四字段均 `IdtAtomic<int>`、`ConfigUploadMode::NoUpload`；真实持久化只在 `Inkeys/Config/main.json`。现有 UI.Bar 字段可参考 `Other.Config.cppm:323-333`，配置文件路径 `Other.Config.cpp:813-815`。

**不读取旧值来初始化新字段，不设 bool→enum 迁移，不向 deploy.json 双写新策略，不把新策略绑定到 setlist bool 地址。** 旧配置无论 true/false/损坏都不影响新默认；旧类型成员可作为未编译兼容源码占位保留，活动 MouseHook/Bar/Setting 不再把它们当自动收起业务所有者。是否顺便退休 deploy reader/writer 是主会话的明确范围选择，不能借此重构旧配置或旧源码。

新增字段读盘必须参考现有 `UI.Bar.IconOrder/ExpandDirectionAdjustment` 的严格验证（`Other.Config.cpp:576-589`）：仅 JSON int/uint、可表示为 int、范围 0..2；bool、float、string、null、负值、3 以上、超宽整数全部回该字段独立默认，避免窄化溢出误判为合法值。缺失字段/ReadMini/ResetToDefaults/复制冻结副本采用统一默认。四值彼此独立，不因一个坏值重置其他三个。

### UI

在常规页增加有标题“自动收起”的块，四行标签 + 同一三态下拉；两种模式始终显示。可放在选择/穿透及栏布局设置附近，不必回塞旧行为区的固定高度大卡片。长英文、设置缩放下标签与下拉不能覆盖，说明按真实字体宽度换行测高，避免旧 250 DIP 固定高度导致截断。

现有紧凑 combo 可参考 `Setting.cpp:3131-3189`：直接每帧读 Config，不保留旧布尔快照；选择未变化不写盘；选中后更新单个原子值并 `QueueConfigWrite()`。配置快照已由 `QueueBusiness` 在 `Setting.cpp:422-425` 冻结，再由 FIFO 异步写，不在 UI 持锁路径执行磁盘操作或同步等待 HWND。

配置变更本身不触发主栏折叠/展开，不切工具、不清墨迹，不取消当前拖动。延后候选消费时再检验当前策略/工具，避免旧点击候选在关闭设置后仍收起。若实现选择一手势开始时冻结策略，需在 PRD 明确例外并保证安全关闭不遗留候选。

### i18n

中文基准 `Inkeys/src/i18n/zh-CN.jsonc`、繁中 `zh-TW.jsonc`、英文 `en-US.jsonc`。可复用现有四项标签，但新增区块标题与三个选项，以及“底栏包含居中底栏”说明；`CheckingRetractE` 的“非主栏区域”应准确覆盖当前可交互的菜单/属性面板，不把操作 Bar 自己当成栏外点击。

实施授权后运行 `Scripts/i18n.ps1 sync` 生成 `IdtI18nKeys.g.h` 和基准快照、补全三语，再运行 check；不得手改生成头。本轮只读，不运行 sync/check 或生成资源。

## 6. 待实施验收清单

- 配置：每字段 0/1/2 round trip、缺失默认、坏类型/越界/超宽 int、ReadMini 不影响旁项、ResetToDefaults、Config copy/FIFO snapshot、不上传；特设旧 deploy 四值各种组合，新 Config 缺失时仍独立默认。
- 策略矩阵：两种样式 × 四项 × 三态 × Floating/BottomDocked-Free/BottomDocked-Centered；关闭不收、1 只 Floating、2 两种底栏也收；已折叠幂等。
- 真实落笔：软/硬/荧光/激光、橡皮、右键临时橡皮、倒转笔尾、多指重叠、拒绝/隔离 contact、模式切换和抬笔后无自动展开；不改变固定次级面板规则。
- 拖动：底栏弹性区不误判；脱离后恰当收起；受控 fold 不取消本手势；抓手不偏移、Raw/HiMsg 不双消费；触摸拖动同样按既有会话路径生效；松手持久化一次，普通点击原行为不变。
- 栏外点击：Selection/ClickThrough 生效、画笔/橡皮/Shape 不冒充栏外点击；Bar 主按钮/主栏/属性/颜色/滑条/弹出面板内点击不收；延迟期间关闭或改变模式不执行迟到候选；两种样式都测。
- 回归：现有 pointer-session、BottomDock、中心锁/折叠、UI 布局命中、次级面板取消、Draw3 唯一输入 producer、PPT 场景规则全部保持。审批后才进行 headless、i18n 和 solution Debug|ARM64 构建；现场 GUI 验收仍需要明确授权或用户人工完成。

## 7. 仍需 PRD 明确的窄决策

1. 新 Drawing 项是否包括独立 Shape：最小恢复建议不包含；若泛指全部绘制，应明确包含并调整用户标签。
2. 栏外点击是否将触摸/笔产生的 promoted mouse Down 等同鼠标点击：旧 hook 没做注入来源区分，不能当成有意的跨设备产品合同；应由 Hook 研究结合现有 source 标记策略给出唯一解释，避免同次触摸重复触发。
3. 待退休旧 deploy read/write 是否本任务一起退出（本次不迁移已定），不影响四项新 Config 的 canonical ownership。

默认值、双样式适用、三态选项、底栏含居中、无旧值迁移均已由用户确定，不再作为待确认项。
