# Research: 异步 End-before-action 的最小接入合同

- Query: 在不让 UI/Mag 回调等待 PrintWindow、Draw3 或 End 的前提下，如何保留 IdtState 的真实返回语义、工具重复意图、普通命令 FIFO、shortcut lease/expected scene，以及 Freeze、workspace 和退出顺序。
- Scope: internal；基于已有 product-integration-current.md 和已批准产品 design 的精确补充，不重复坐标、资源、工程入口调查。
- Date: 2026-10-21
- Status: 以下 API/屏障均为后续实现建议，当前生产代码尚无 follow Begin/End。

## Findings

### 1. 建议采用的唯一主线

采用「Business 非阻塞的两阶段 action ticket + 原 UI owner 保留动作 + Host 可靠流内的 End 屏障」。不要把每个 IdtState 函数改为自行排 lambda 并返回 true，也不要新建通用 UI 任务框架。

1. Business/Host 的短 publish/stop 门原子撤销发布并冻结最后**已经提交**的绝对目标。Request/Prepare 返回只表示请求/凭据已接受。
2. 工具及 Freeze 的原动作留在现有 Bar coroutine/shortcut owner，等 ticket Ready 后调用原函数，才能取得原来的 bool。UI 是挂起/重试，不能 get()/wait()/join()。
3. 普通 `PublishProductCommand` 必须在 Host 最终发布门内带上前置 End；所有直接/兼容生产者都受保护。它原来的 Accepted 仍然只是可靠接纳。不要把这些命令先存进 Business 队列、等 End 后再冒用最新 scene 重发。
4. owner 取到 End 后暂停其后命令的**执行**，继续帧循环、viewport recovery/full present 和输出交接。只有真正 End 条件完成才续动作；不能依赖现有 commandProcessed。
5. 正常 End、场景退休、Shutdown 使用不同终态。只有正常 End 等原工具表面就绪/WindowService 成对切换；退休/退出不能等待这个 ACK。

这一方案只增加 follow 自己的会话终态、ticket/hold 与一个 owner 待完成屏障；UI 动作由原交互 owner 执行，历史命令仍由原唯一 Controller 执行。

### 2. 已读文件与现有模式

| 文件 | 本问题中的职责 |
| --- | --- |
| `Inkeys/IdtState.h`、`IdtState.cpp` | 工具、条件 revision、选择按钮 Clear 事务、Freeze、Publish/Reconcile guards、StateMonitoring |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | 已有 coroutine、原顶层 Run、主/More callback 分发、笔型手势和业务队列 |
| `Inkeys/Inkeys/UI/Bar/Bar.KeyboardShortcuts.h` | 已有 bounded intent queue、Pending、lease、具名回执及退休 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | void clickFunc 内部仍消费 bool；Clear/Recall 的 Accepted 语义 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` | 可重入生命周期门、workspace/target/command 的公开边界、StopProduct |
| `Draw3.Host.cpp`、`Draw3.Bridge.cpp/.h` | 最终接纳锁、scene-stamped FIFO、wake 预约、基础 gate、final command |
| `Draw3.Shortcut.h` | Scope、64 固定回执槽、租约与 owner 二次授权 |
| `Draw3.DrawingController.cpp`、`Draw3.WindowControl.cpp` | active.empty 安全点、命令执行/回执、完整呈现安全点及已有 completion FIFO 插槽 |
| `Inkeys/Inkeys/Graphics/FreezeCapture.h/.cpp` | 真正公共 Capture API、借用帧、Stop 与 callback 的寿命合同 |
| `Inkeys/Inkeys/UI/Freeze/Freeze.cppm` | 同步原子状态 observer、revision、唯一观察者 |
| `Inkeys/Inkeys/Window/Window.cpp`、`Window.CommandCompletion.h` | owner 显隐 guard、真实完成及同步等待现状 |
| `Inkeys/IdtMain.cpp` | BeginShutdown 早于 Host Stop；实际清理顺序 |

下文短文件名均指上述路径；行号是 2026-10-21 研究快照。

### 3. 返回值不能被「异步已排队」替换

| 当前入口/证据 | bool/receipt 的实际含义 | 接线要求 |
| --- | --- | --- |
| `IdtState.cpp:1348/1432/1492/1539` Selection/Pen/Shape/Eraser | 成功同步修改状态并发布后 true；不是异步请求 ID | 普通 UI 可把整个动作留在 coroutine，Ready 后再调原函数。不要在 setter 内隐式排队后假回 true。 |
| `1450` ChangeStateModeToPenTool | `1457` 同工具分支递增真实 mode revision，但 false 且不 Sync；测试 `1771` 明确依赖无视觉变化返回 false | 在意图前门、WouldChange 和 early-return **之前**请求 End。Ready 后仍执行这一原分支，false 不是 End 失败。 |
| `1507` SetShapeModeSelectImpl | `1520` 先递增 revision，同形状 `1521` false | 同上；不能只在 SyncDraw3State 内 Stop。 |
| `1238` SelectionFromButtonImpl | 配置需要清空时，先 PublishClear Accepted；拒收不换工具；随后 `1248` 同锁 CAS expected mode revision | Ready 后完整执行原事务。不能排队 Clear 却提前 Selection；也不能把 pending 当 Accepted。 |
| `1363/1410/1471/1515` 条件工具/形状/PPT | expected revision 与赋值同锁，失败明确不修改；shortcut 内核不登记 PPT 真退出标识 | 保留 expected 值和原函数。不要等待完换成最新 revision 或降级无条件 setter。 |
| `1321` ToggleFreezeFromButton | `1345` 仅 active 真变化才 true，用于反馈；可用性可能在动作期间改变 | pending UI 反馈不能宣布真实切换；Ready 后重验并调原函数，再按实际 bool 确认。 |
| `1393` ToggleFreezeForShortcutIfRevision | 调前 expected/工具/可用性验证，Toggle 后再核 revision，冲突时按 captured freeze revision 回滚；true 是该条件事务已完成 | 在原 shortcut owner Ready 后调用；不能异步换掉函数本体而让原调用栈错误地消费 true。 |
| `Host.cpp:2758` 普通 CommandResult | Accepted 为入可靠 FIFO，不等于 Applied/presented | 可在同一可靠流挂 End 前置；保持 QueueFull/NotRunning/Unsupported 及接纳时 lease 判断。 |
| `Host.cpp:2697` shortcut requestId | 固定回执槽已预约且条件命令已接纳；完成另取 | End ticket 必须是不同类型，不得把 End ID 填入 `pending.request` 冒充 canvas receipt。 |

「普通工具无返回」准确指多数 UI `clickFunc` 为 void，不代表 IdtState setter 本身无返回。`Bar.Button.cpp:323/436/643` 的 void callback 内仍用 bool 决定面板及 haptic；`Bar.Interaction.cpp:7394/7402` 也依赖 WouldChange/changed。把整个 callback 保留到 Ready 后执行可保存这些行为。

### 4. 建议 public Business 头/API（示意，可调整命名）

建议新增 `Inkeys/Inkeys/Business/ScrollFollow.h`；不依赖 UI 对象、HWND 或 borrowed Frame，不公开通用 Post(function)。类型的具体 scene 字段沿产品设计的完整身份，由 Core 定义；下面只画动作合同。

```cpp
namespace Inkeys::Business::ScrollFollow {
enum class EndReason : std::uint8_t {
    Toggle, ToolIntent, CanvasCommand, Freeze, Workspace, Geometry, Device, Shutdown
};
enum class EndDisposition : std::uint8_t { Restore, RetireScene, Shutdown };
enum class ActionState : std::uint8_t { Pending, Ready, Retired, Failed };

// 强类型凭据；不得与 Shortcuts::requestId 混用。
struct ActionTicket { std::uint64_t run = 0, id = 0; };
struct ActionPreparation {
    ActionTicket ticket;
    ActionState state = ActionState::Failed;
};
struct ActionCommit {
    // 0 仅用于无状态变化/动作拒绝；非零表示原 setter 实际发布的 bridge revision。
    std::uint64_t publishedStateRevision = 0;
};

void RequestToggle() noexcept;
void RequestStop(EndReason = EndReason::Toggle) noexcept;
Snapshot GetSnapshot() noexcept;

// 短操作：接纳凭据、关闭发布、冻结尾目标、唤醒控制/owner；不等待 End。
ActionPreparation PrepareAction(EndReason) noexcept;
ActionState PollAction(ActionTicket) noexcept;
void CompleteAction(ActionTicket, ActionCommit = {}) noexcept;
void CancelAction(ActionTicket) noexcept;

// 单调升级同一 End；废掉普通 action ticket，不恢复旧输出。
void RetireScene(EndReason) noexcept;
void RequestShutdown() noexcept;
}
```

语义要落实如下：

- receipt/ticket 的存储在返回接纳前保留；失败返回 Failed/零 ticket，不留一份没有 owner 的 accepted continuation。可用固定小槽，或 owned 小对象；不要让客户端保存指向控件栈的 callback。
- `PrepareAction` 即使当前 Off，也需在同一会话门内给一个短 action hold，直到 Complete/Cancel；否则「检查 Off → 新 follow Begin → 工具赋值」有 TOCTOU。Off 的 hold 只排除新的 Begin，不需要给原本无 follow 的每次工具点击新增 contact 屏障。Ready 表示 **End 前提已满足且此动作仍有 hold**，不表示动作已执行。
- 同一 follow 的多个请求共享一个 End/最后 seq，不能重复排多个 End；各调用者的 ticket/期望条件独立。请求/取消幂等并验证 run，旧 ticket 不影响新会话。
- `CompleteAction` 不总是立即开放 Down。工具真实发布后，将 hold 转成 owner 的状态应用交接（等指定 bridge revision 已完整应用）；无视觉变化分支可按已恢复的原输出收尾。动作被拒绝也必须 Complete/Cancel，不能泄漏 hold。
- UI 执行原 setter 时不持 Business/Host 锁；状态核的原 bool 保持同步语义。`publishedStateRevision` 必须是这一 setter 真实发布时取得的版本/回执，不能在解锁后读一个可能属于别人的“最新版本”来证明自己完成。
- 这个窄 revision 回执可由 `PublishDraw3State` 内部返回/记录，或产品状态发布提供带 revision 的窄 overload；不必把整个 State API 改成 futures。对于同工具 early-return 无 bridge 发布的情况，明确按 NoChange 收尾。
- 后台核心不执行任何 Bar 面板/haptic callback；`PollAction` 的 Ready 由原业务 owner 消费。UI 若销毁、路由退休或退出，取消自身 ticket，但已封口的 End 继续收尾，不能重新开放发布。
- 不要求给跨线程工具意图发明全局先来先执行语义。保留现有状态锁/revision 冲突规则；同一 Bar owner 的动作保持原顺序，已接受画布命令保持原 FIFO。独立生产者同时竞争仍在最终发布/状态锁处线性化。

Core/Product 还需要自己的窄接口（不以 UI ActionTicket 代替）：`BeginFollow`、latest `PublishFollowAbsolute`、`EnsureFollowEndLocked(disposition)`、`EndCompletion` 和 `AcknowledgeFollowPresentationHandoff`。Business 读取内容/呈现回执，Host 负责真正可靠接纳和输入 gate。

### 5. 普通工具、按钮和属性动作：在原 UI owner 重试

**主/More callback 的集中接线点**：`Bar.Interaction.cpp:5643` 成功 Up 后、`5651` callback 前；主栏 `5866/5872/5875` Clear 特殊分支/普通 callback 前。按 preset/明确动作分类，仅为工具/穿透/Freeze/相关内容动作准备 ticket，新增 ViewAdjust toggle 本身不走“先结束自己再 toggle”的递归门。

使用一个私有 coroutine helper，例如 `AwaitScrollFollowAction(reason)`：

1. 冻结这一次已识别业务动作、原 StateMode version、场景/route 与必要 immutable haptic 凭据；PrepareAction。
2. Pending 时 `co_await InteractionDelay(...)`/既有 mailbox wake 后 Poll，线程仍处理取消和状态投影；不能 while 忙等、Sleep、future.get 或在 clickFunc 中等待。
3. 原消息中的临时对象/裸控件指针不能跨生命周期；可由仍存活的同一 coroutine 保留 owned 身份。恢复时验证注册/布局/route/模式与原意图仍有效；过期 Cancel。不能把旧 Up 当新的 hit-test 再点击其它按钮。
4. Ready 后回到原 UI owner 调用一次原 callback，让其内部 bool 和面板分支正常运行。`AcceptedClickScope` 应在真正执行时用保存的可信凭据建立，不跨挂起保留 thread-local scope。
5. 记录真实 state publish 交接，Complete；拒绝/取消也收掉自己的 ticket。后续由 Host 在应用状态后撤自己的 action hold。

**必须位于工具分支判断之前**：Draw/Eraser/Geometry 主按钮在“已经是该工具则打开属性栏”的分支前就应 Prepare。否则同工具按钮完全不调用 setter，会漏 End。笔型菜单的 `SelectPenTool` 在 `7394` WouldChange 前也要前门；Hard/Soft/Highlighter 的调用点 `7779/7814/7848` 在 coroutine 中，可复用该 helper。Laser 的独立选择同理。`SetShapeModeSelect`、颜色/宽度等真实用户编辑在其业务手势开始处处理，不把 Host 的 DPI/paintDevice 上下文回调误当用户切工具。

`TryReadBarPointerMessage:1148` 已用 `Poll(false)` 收包并制造取消，让嵌套手势退栈；`Run:7907` 仅在顶层 `Poll(true)` 执行快捷键。等待 helper 必须保留这条规则：收到快捷键时退出原手势/取消原 UI ticket，顶层再处理，不能从等待 helper 内调用 shortcut Dispatch。

辅助面板中无法直接 `co_await` 的同步 surface callback，应只冻结一个明确的业务值/意图交回**原交互 owner**，在 Run 顶层用同一 ticket helper 恢复；不要搬 callback 到后台线程执行 UI。不需要让所有 callback、保存或 Shell 动作都进入新队列。

条件 PPT/长手势调用者可继续以 bool 拒绝未具备前提的请求，并保存原 expected revision 在自己的 owner 重试；不能由低层函数悄悄留一份晚到任务。所有生产用户工具入口需经过上述前门；Core 的场景/发布门只是防迟到目标的最后防线，不能代替同工具意图前门。

### 6. 普通 Undo/Redo/Clear：Host 可靠流是最后屏障

直接证据：`Host.cpp:2758` 在 `commandPublishMutex → presentationTargetMutex` 下先查 `shortcuts.OrdinaryCommandAllowed`，再预约 wake、bridge.Publish、发布或取消 wake。`Product.cpp:229` 公开命令和 `IdtDrawpadFacade.cpp:119/125` 兼容 U/R 都进入此门。`Bar.Button.cpp:94/1619` 根据即时 Accepted 决定双击候选和 recall 反馈。

应把 `EnsureFollowEndLocked(Restore)` 嵌在这个最终发布门；它是内部 locked helper，不能在持锁时回调 public Business API（后者可能再次拿 product/command 锁）。具体顺序：

1. 先按现有锁验证 Running、命令类型以及普通命令 lease 准入；租约不允许时仍 QueueFull，不把拒绝命令存起来等租约消失。
2. 若有活跃 follow/尚未完成的 End，在同一门内封口、冻结最后已提交目标并可靠排 End。随后将普通命令保留原 captured scene、排到其后。普通命令容量不足可继续返回 QueueFull；已经接受的 End 不撤回，也不伪造命令 Accepted。
3. 若普通命令 Accepted，必须已拥有完整 payload/wake，不能仅拥有 Business 异步重发计划。owner 的 End 未完成时命令留在原队列，不执行；完整 End 后按原 FIFO 执行。
4. 接纳后的普通命令不要再用届时的新 lease 重新拒绝。现有合同允许**租约取得之前已经接受**的普通命令依 FIFO 完成；新增 End 不能改掉它。反之，租约已经持有时的新普通命令必须当场拒绝。

**可靠 End 在满队列下的最小实现建议**：每个被接受的 Begin 预留一个 follow 专用终态/wake 预算；只允许一个未完成会话终态。Bridge 新增固定 `optional<EndEnvelope>` 和接受时的 `commandsBeforeEnd`/序号，仿照 `WindowController::TryEnqueuePresentationCompletion:796` 与 `TryDequeueCanvasCommand:559` 的有序固定槽：先消费 End 前已接受命令，再消费 End，再消费后来命令。`StopWithFinalCommand` 的 final 槽仍专用于退出，不能拿它做正常 End，因为它会 `running_=false`。

现有 command wake 预算是 258（`ContactInput.cpp:32`），普通 Bridge 容量 256（`Bridge.h:302`）。需要明确证明普通256 + 当前 follow终态1 + final1 的上界及回收；Begin/其它新命令仍须按正常容量，不随意扩大 ring。预留发生在 Begin 接受前；未能预留则拒绝 Begin。终态只冻结纯值/预先拥有 payload，不能在 Stop 路径临时分配后宣称可靠。

每个可靠 Bridge 命令仍严格对应一个已预约 Command marker。不能插两条命令却只发一个 wake。`PublishReservedCommandWake:410` 已有 fallback ordinal/down 水位，不改成可能丢失的普通 control wake。

**owner 端必须加执行阻挡，不能只排 End 命令**：

- `DrawingController.cpp:13702` 是 `while(active.empty() && TryDequeue...)`，当前可在同一帧连续执行多条；End 分支设置 pending 后必须立即退出该命令循环，随后帧继续恢复/呈现，后继命令还留队。
- pending End 未达到指定 viewport/session/seq 的 successful **full** present 且 `!viewportRecoveryPending && !viewportRefreshPending` 前，不取其后普通、shortcut、场景命令。`16490` 的 present 安全点可新增独立 End 内容回执；不能复用 `13695` reportCommand 或任何一次 successfulPresentCount。
- 正常内容回执后，Business 清 override 并请求原工具输出；确认 owner 的目标 output revision ready 和成功 Window pair ACK 后，再发窄的 handoff ACK。owner 才允许后继动作通过 End。等待期间继续 PumpBridgeState、terminal contact drain、viewport replay、present/ack；不在绘制线程 wait 控制线程或 WindowService。
- pending End 每轮都读取 disposition 的单调升级；RetireScene/Shutdown 的撤销信号不排在它自己堵住的命令后面。升级后走 CPU final/退休终态，跳过正常 full-present/Window handoff 前提并唤醒后继。否则“退出命令已经排队”也解不开正在等正常窗口 ACK 的 End。
- ActionTicket 的 hold 与 follow gate 独立计账。UI 工具尚未提交时不能在“End 已完成”瞬间开 Down。已入可靠流的普通命令以原 marker/owner 执行为接续点；也可在命令 guard 完成时释放其 hold，均不能出现 action 未排序先开放 Down 的空隙。

新 FollowBegin/End **不能**直接走 `PumpOneBridgeCommand:1679` 现有 `if(!shortcut) EnqueueCommandScene`。它们验证当前 owner 完整身份，绝不借 captured workspace 重放旧页；否则一个晚 End 会先把 owner 切回旧 Desktop，再污染场景。给 follow 分支显式跳过 scene replay/无意义 restoreLatestScene。普通历史命令原 captured-scene 语义保持，shortcut 原不重放语义保持。

### 7. shortcut：先在原 owner 等 End，再沿原 lease/receipt 事务

`Bar.KeyboardShortcuts.h:41` Pending 已保存 intent/request/lease/freezeRevision；`Drain:257`、`CompletePending:132`、`RefreshCanvas:243` 和 `Poll:295` 已有可复用状态机。只加 `PendingPhase::FollowEnd`/独立强类型 endTicket 和冻结的 expected Context，不新建第二个 shortcut worker。

1. 在顶层 Drain 已确定准备处理的头意图上 PrepareAction；可先留在原 queue，记录其唯一身份与 expected Context。Pending End 时只收包、刷新上下文、执行既有退休规则，不弹出并“完成”该意图，不获取 shortcut lease。
2. Ready 后再次走原 `Current(intent,...)`、scope/route/mode/Freeze 有效性核验；保持**原 expected scope/revision**。同一 scope 的普通内容变化仍由原 Delete candidate/具名 Fence 规则判断，不能因 viewport End 自己而换页身份或内容修订。
3. 验证通过后执行原 Dispatch。Undo/Redo 仍 `PublishProductShortcutCommand` → 原具名 canvas receipt；第一 Delete 仍无长期 lease；需要 Clear→Selection 的 X 此时才 Acquire→PublishClear→等真实回执→条件 Selection；第二 Delete 仍 Acquire(requireContentRevision=true)→只读 Fence→条件 Selection，不再清空。
4. Lease 直到原 CompletePending 真正完成或 Retire 才释放。独立 End ticket 不能替它释放输入，也不能把 End Presented 当 Clear Applied。完成后 CompleteAction 自己的 hold；lease 的原 handoff 继续单独组合。
5. mode/foreground/scene/config/lifecycle 变更仍触发原 Retire/CancelPending，释放**自己的** ticket/lease/request。已经排 End 的会话不复活；后来新场景只处理新意图。两击 Delete 已形成后 Ctrl Up 与 interruptionGeneration 的既有区别保持，不能由等待 End 额外发明时间窗。

**Host 最终防线仍保留**：`PublishShortcutCommand:2697` 原同锁 `current.available && scope==expected && Lease()==leaseId`、固定槽 Reserve、wake 预约和 bridge.PublishShortcut 全部保留。非 Bar 生产者即使绕过 UI 预等，也只能在这些条件成立时带同一 End 前置屏障接受；owner `Authorize` 在真正续执行时再验证，过期返回原 Stale/Blocked，不转换普通命令或场景重放。若它已经取得 lease，不可为了 End 先 Release 再 Acquire 另一份 lease；End 本身不依赖任何 shortcut lease。

**follow 不应把所有 shortcut 路由关掉**：现有 `ShortcutBaselineBlockedLocked:737` 与 contact 的 `RefreshPresentationInputGateLocked:767` 分开。follow 关闭的是新绘制 Down；场景仍稳定时 Ctrl+Z/Clear/Q/X 应能被原 owner 认领并请求 End。若直接把 followBlocked 加进 `Context.available=false`，`Poll:328` 发布的 keyboard route 会关闭，按键可能落入底层应用。可执行动作暂缓由 FollowEnd phase/owner barrier 表达，不伪装成场景不可用；真正 PPT/load/retired/fatal blocker 仍阻止动作。

### 8. 临时 selection 与可靠呈现 ACK

`PublishDraw3State:264/276`、`Reconcile:519/532` 和 Window guard `546/561` 必须统一读取 `(真实 modeRevision, follow overrideEpoch, effectiveSelection)`。Begin/End 更改的是单独的 follow projection epoch，**不能**改真实 StateMode/Pen 属性、mode revision 或 shortcut intent epoch。

原因是 `Bar.KeyboardShortcuts.h:88/313` 用真实 mode revision 判断外部工具意图；End 自己推进该值会废掉正在等 End 的合法快捷键。Bridge 的 `PublishState:45` 已只在真实 scene 变化时推进 shortcutIntentEpoch，selection/tool 变化不推进；保持此规则。

可复用 StateMonitoring 现有协调，不新增 Window 任务框架：

- Business 控制侧只发布 override 的有效投影和 request-reconcile 标志，不调用同步等待的 Reconcile；StateMonitoring 在 runtime revision/250ms retry 后沿原 Reconcile 执行成对切换。
- 在 `ReconcileDraw3PresentationState` 实际 `SetDrawpadSurfaceVisibility` 成功、后续 bridge/mode/override 仍匹配后，记录一份窄的 immutable 成功 ACK：Host run、follow projection epoch、bridge revision、mode revision、requested/ready output revision、visibility。存储/通知须短且不回调 Business。
- Business 只有看到这份同代 ACK 才把正常 End/Start 标 Ready。单纯 runtime outputReady、UI Selected=false、Reconcile 被调用、CommandResult Accepted 都不能替代它。
- 尚在队列中的旧 visibility guard 也检查 overrideEpoch/Shutdown；否则真实工具 revision 未变时，旧的“开启临时 selection”命令可能在 End 后仍命中。

`Window.cpp:1458` 的普通 Submit 会 `future.get()`；`Window.CommandCompletion.h:16` 只有传了 deadline 才有等待上界。现有 `SetDrawpadSurfaceVisibility:423` 没传 deadline。因此不得在新的 Business 控制线程做一次无期限同步 Reconcile，再让 Shutdown join 该线程。建议使用上述“读取原 StateMonitoring 成功 ACK”的方式；控制侧等待可取消，Shutdown 直接放弃正常显隐 ACK。若实现选择新的 follow 专用同步入口，则必须利用已有 Command.deadline 做有界可取消等待且晚成功仍靠 guard 过期，不能把等待超时当成功。无需扩成通用 UI task API。

### 9. Freeze、workspace 与退出的不同接续

**Freeze 用户动作**：原 Bar callback/shortcut owner 等正常 End Ready，保留 hold，再调用原 ToggleFreeze；重新检查 available、expected tool revision 和真实 active。普通按钮的 bool 反馈在实际执行之后；shortcut 的前后 revision 回滚保持。不能先 Toggle 让 observer 关 Capture，再宣布 End 已完成。

`Freeze.cppm:64/128` 的 observer 是在修改 Freeze 原子的调用线程上同步调用，且全局只有一个槽。不要注册第二个 observer 覆盖原 Mag 协调器，也不要在 observer 内等 Draw3/Window。工作区强制 Freeze off、原 Mag 故障关闭等底层状态变化是生命周期事实，可直接触发 revoke/retire；它们不是一份需要后台重放的用户 Toggle。

Capture 真正结束与 Draw3 End 是两个独立条件：`FreezeCapture.cpp:301` 锁外 callback 已获准时 Stop 后仍可返回；`138` Stop 立即关门，`182` Drain 只在 backend End 真返回后清 occupied。原 Mag 串行调度让 Freeze 的实际取帧自然排在旧 PW/回调之后；UI 可提交 Freeze 意图但不能 join PW。新 follow Start 在 occupied 未清时仍 Busy，不能由 Draw3 End Ready 假称 Capture 已释放。

**workspace/page/target**：`RequestWhiteboardActive:833` 请求时就撤销发布，`StateMonitoring:2128/2148` 的实际 workspace/以及 Product target 边界负责有序退休；Product 现在 workspace/target 直接改 Bridge（`Product.cpp:151/160/190/201`），应在这些已确认的边界并入 Host 的同一窄封口/retire 门，不能只靠后续 Sync。保持 ProductCall→command→presentation→bridge 的既有方向，pure frame 提交不回调它们。

对于有序离开旧 scene：owner 仍在旧 canvas 时先提交最后可信 CPU viewport，再存旧场景/替换页；不必 full present 旧画面或恢复旧主表面，End 结果为 Retired。场景/几何/device 已失效时不能把最后目标套新页，返回 Retired/Failed 并废掉旧 ticket。每次真实安装/ReplacePage/cold restore 都推进运行代次；相同 index/GUID 不能复活旧会话。下一 scene 的 load/PPT/base blocker 独立接管，不能释放后直接 SetAdmissionBlocked(false)。

**Shutdown**：`IdtMain.cpp:367` 已 BeginShutdown，`391` StopMag 协调；main `3844` join 原 Mag，`3848` 才 StopProduct。此时非 Hidden 的 Window visibility 命令会拒绝（`Window.cpp:1960/2096`），所以正常 End 的窗口 ACK 永远不能作为退出前提。

- 在退出请求（以及 Host::Stop 的最终防线）短门内撤发布，End disposition 单调升级 Shutdown，退休待执行 UI/shortcut continuation；不 join 旧 PW，不等窗口恢复。
- 这个升级是 owner 可直接观察的状态/可靠 wake，不靠排在 pending End 后的 PrepareExitAutoSave 才被发现；主清理此前还会 join StateMonitoring/Mag，因此必须在 SetOffSignal 的早期通知，而不能只在最后 StopProduct 才通知 Business。
- `Host.cpp:2241` 发布 `StopWithFinalCommand(PrepareExitAutoSave)` 的同锁边界必须把封口所得**最后可信目标**并入可靠退出前置条件。推荐把它带进已有 final envelope：owner 处理 final 时先在身份仍匹配的旧 canvas 提交最后 CPU viewport，再走现有 PrepareExitAutoSave；不另发一个可能被关闭 bridge 拒绝的普通 End。
- 已经接受的普通命令仍按原 FIFO 完成；final 排在其后。每个 mutation 前的终态屏障先完成，不能靠 final 最后补一下挽救早已在错误 viewport 执行的 Clear。
- Shutdown 不等 full present/Window ACK；这里单独记录 FinalViewportCommitted，不能冒称正常 End Presented。现有 producer drain、最终快照/worker 无超时 drain 保持（`Host.cpp:2250/2269`），新增控制线程不得成为它等待 WindowService 的反向边。
- owner 已故障/身份已退役不能伪造保存成功；如实失败并保留原退出监督/数据保护。不要为保证表面成功修改原 drain/签名/工具配置。

### 10. 并发时间线与失败顺序

#### 时间线 A：同工具点击与最后一帧竞争

| 时刻 | Mag/Tracker | UI / Business | Host / owner |
| --- | --- | --- | --- |
| T0 | seq40 可信目标已提交 shared latest | 真实用户工具 Pen，临时 selection active | base viewport + seq40 |
| T1 | seq41 的 LK 正在门外计算 | 同工具点击在 WouldChange/early-return 前 Prepare，短门 publishing=false，冻结 seq40，返回 Pending | 可靠 End(session,seq40,final) 入序；actionHold 保持 |
| T2 | seq41 算完，进门见已关闭，丢弃；不等 End | Capture.Stop 在锁外撤发布；原 coroutine 挂起 | End 应用 seq40，设置 pendingEnd，退出本帧命令循环 |
| T3 | 旧 PW/callback 可继续自然退出 | Poll 仍 Pending | commandProcessed 即使报告也无效；tile replay 尚未完，后继动作不执行 |
| T4 | — | 控制侧收到专用 ViewportPresented，撤 override 但保 hold | 对指定 viewport full present 且 recovery/refresh 都清；原工具 output revision 就绪 |
| T5 | — | 原 StateMonitoring 成对 visibility 成功记录同 projection ACK；ticket Ready | handoff ACK 允许通过 End；尚不因 Ready 抢开 Down |
| T6 | — | 原 UI owner 调原同工具 setter：modeRevision+1、返回 false、不 Sync；按 NoChange Complete | 撤本动作 hold 后重算总 gate；无跟随/其它 blocker 才开 Down |

若 seq41 在 T1 前先拿到提交门，则冻结值就是 seq41；没有“Stop 后旧 callback 先更新 latest 再 End”的第三种结果。正在运算不等于已经提交。

#### 时间线 B：shortcut X 等 End 时场景退休

| 时刻 | shortcut owner / UI | Host / Draw3 | 生命周期 |
| --- | --- | --- | --- |
| U0 | X 捕获 expected modeRevision=R、scope=A；FollowEnd Pending，尚无 lease | EndA 已封口/排队 | Desktop A |
| U1 | 原 queue 保留 X，不拿最新页重绑 | A 最后 frame seq8 可信，seq9 迟到拒绝 | PPT target/Whiteboard 请求到来，短门升级 RetireScene |
| U2 | Poll 检查 scope/route/mode 改变，Retire X ticket | owner 若仍在 A，提交 seq8 CPU viewport 后退休 A；无需原工具窗口 ACK | 新 scene B 安装，运行代次递增 |
| U3 | 不 Acquire、不 Clear、不 Selection | 迟到 EndA Presented/窗口 ACK 因 run/projection/scene 不匹配拒绝 | B 按原 load/ready 门推进 |

若 U1 发生在 X 的 lease+Clear 已接纳之后，原 scope/lease 仍由 owner 二次 Authorize；退休时请求回 Stale/Blocked，旧 Clear 不得 scene replay 切回 A，也不得用 EndA ACK 当 Clear 成功；CancelPending 只释放自己的 lease/request。

#### 时间线 C：普通命令、shortcut lease 与 full-present 间隔

V0：普通 Undo 在无 lease 时于 Host 同锁接纳，流为 EndA→UndoA；Accepted 立即返回。V1：之后 shortcut 取得 lease L 并接受 Clear/Fence（正常 Bar 建议先等 End；此例覆盖其它条件生产者），流为 EndA→UndoA→conditionalA。V2：新普通 Redo 看到 L，QueueFull，不能暂存到 L 释放后。V3：End 的 commandProcessed 已到但 replay 未完，三者均不得跨 End 执行。V4：同代 full present + 原输出/窗口 handoff 完成，先前 UndoA 按原 FIFO 完成，不能因现在有 L 改为拒绝；conditionalA 随后按原 lease/scope/expectedContentRevision 重新授权。若 Fence 的具名空态已被 Undo 改变，则 Stale，不 Selection。

#### 时间线 D：正常 End 中途 Shutdown

W0：正常 End 最后 target 已冻结，owner replay 未完或控制侧正等 presentation ACK。W1：SetOffSignal/Host::Stop 撤发布、升级 Shutdown、取消普通 UI continuation；WindowService BeginShutdown 已拒绝显示。W2：不再请求恢复旧 surface，不等待正常 End Ready；final envelope 保留最后可信 target。W3：owner 提交匹配旧 canvas 的 CPU viewport（若此前已提交则幂等），再捕获退出快照；已接受命令 FIFO 和原 worker drain 完成。W4：原 Mag/PW 按原生命周期释放；late callback/late visible ACK 不得重开 session。

#### 明确失败结果

| 条件 | 必须结果 |
| --- | --- |
| Prepare/ticket/Begin 终态预算预约失败 | 动作不执行/Begin 不接纳；不能返回 Ready 或 Accepted |
| 普通命令 queue 满 | 原 QueueFull；已经封口的 End 可继续完成，不偷跑命令 |
| 同工具 setter false | 正常无视觉变化，仍完成 End 和真实意图 revision；不当失败自动恢复 follow |
| End commandProcessed、任意一次成功 present | 仍 Pending，直到指定 final viewport 全部恢复并 full present |
| 正常 owner output/visibility 失败 | 继续 Stopping/closed 或明确失败，动作不执行；只可取消/退休/Shutdown，不能超时造 Ready |
| expected mode/scene/lease/route 已过期 | 原条件动作取消/回原 Stale/Blocked；不替换 expected 或无条件重试 |
| Capture Stop 后 occupied/inFlight 未清 | shared context 保活，旧回调拒发，新 Start Busy；不让 UI join |
| Shutdown / scene retirement | 终止正常等待，按专用 CPU final/Retired 规则收尾；不等 Window ACK |

### 11. Core 实现者需特别保留的锁/回调边界

1. `Product.cpp:15` 是 **recursive** productCallMutex；注释说明 SetWindowPos 可同步重入 Drawpad WndProc。不得在 public Product 命令持锁时再次进入会等待自己的 Business/End helper。`StopProduct:49` 明确只 drain call gate，锁不跨 Host.Stop；保持此合同。
2. `Host::PublishCommand/PublishShortcutCommand` 方向为 productCall→commandPublish→presentationTarget→bridge/shortcut；End 的 locked helper 只做纯值封口/入序。反向回调、WindowService、状态锁、PW、等待 ACK 都在这些锁外。
3. shared SessionContext 的 frame 提交只锁短 publish/stop 门；CPU LK/图像处理在门外。门内不得调用 public Host/IdtState。owner/Host 可按固定单向顺序取该门冻结 final；frame 解锁后再发不等待的 wake。这样不会形成 frame(门→Host) 与 Stop(Host→门) 的锁环。
4. `IdtState.cpp:42` 的 stateModeTransitionMutex 非递归。`PublishDraw3State:264` 已持它发布；新 override 查询不能回调 GetStateModeSnapshot 再拿同锁。控制侧不得持 follow 门调用 SyncDraw3State；只复制/修改投影，解锁后发布。
5. `Reconcile:523` 持 draw3PresentationMutex 调 WindowService；Window guard 在 Window owner 上跑，只读 version/bridge，不能回调 Reconcile 或取 mode lock。新增成功 ACK 只存值/通知，不在该锁内执行 continuation。
6. `Host::ObserveShortcutContext/AllowShortcutCommand:1362/1369` 是绘制 owner 到 Host 的同步回调并取 presentationTargetMutex；End Presented observer 同样只能存回执/唤醒，不能由 owner 回调等待 UI。`shortcutCompleted` guard 是离开命令作用域就发生，不能让 pending End 误触发 Applied/Failed 终态。
7. `WindowControl.cpp:563/634` 的 canvasCommandMutex 只保护队列。End 等待不能持它；原 completion 消费 `579` 已明确锁外 wake。固定 End 槽的顺序/唤醒也照此。
8. `FreezeCapture.cpp:301` callback 在 Capture mutex 外，Stop 可立即关闭，但 shared context 要活到已准入 callback/Drain 自然结束；不要从控制线程并发 Reset/Initialize/Process Tracker。
9. `Bar.Interaction.cpp:1513` 现有通用 Business FIFO 是保存/对话框/Shell STA，用 `std::function` 且 drain 可等待长外部操作，不是合适的 End/工具 owner。`Setting.cpp:9131` 的 DeferDraw3SyncIfRendering 只把 Sync 从 ImGui 锁下移出，状态已经修改；它不能保证用户动作 End-before-mutation。复用它们不能替代本报告的前门。

### 12. 相关 specs、参考与后续验证

已读取 `.trellis/workflow.md`、`native-desktop/index.md`、`cpp-conventions.md`、`keyboard-shortcuts.md`，以及 `draw3-integration.md`、`canvas-freeze-boundaries.md` 的相关输入、输出、可靠命令/完成与退出合同；沿用此前调查已确认的场景/输入/窗口入口。产品已批准 `10-21-scroll-follow-product-integration/design.md`、父任务 `10-21-scroll-follow-v1/design.md` 是本建议的约束。

外部参考：无。本问题是当前仓库内部返回值、锁与接线研究，没有网络/API版本假设或依赖升级。

后续必须由实现者做生产 helper/真实 owner 路径的定向无窗口或**全程不可见**测试，至少覆盖：上面 A–D；命令队列满仍可靠 End；同工具点击/同形状 false；选择按钮 Clear 拒绝不换工具；真实 shortcut X/两击 Delete 的原回执、expected 和 lease；End 不推进真实 modeRevision/shortcut scope；outputReady 未 Window ACK 不 Ready；Shutdown 永不等恢复显隐 ACK。专用 End presented 回执要有一例故意跨多帧 recovery 才完成，单个同步假 backend 的一次回执不能证明这一点。

## Caveats / Not Found

- 只写本 research 文件，未改源码、spec、任务 metadata/context、工程或他人文件；未执行 Git 操作、构建、测试、GUI/Computer Use。
- 这些是可执行建议，不是已落地接口；固定 End 槽、ActionTicket、独立 projection epoch 和实际 presentation ACK 需要 Core/Product 实现及后续 check。
- 原 Bar coroutine 可以避免线程阻塞，但挂起时的控件/布局/haptic 凭据与消息续接必须按原寿命验证；不能简单跨 co_await 留 thread-local AcceptedClickScope 或 replay 原输入。
- 当前普通 WindowService visibility Submit 可无期限等 owner；新增控制路径不得引入这种等待后再 join 的依赖。本建议通过读取原协调器的同代成功 ACK 避免这条新依赖，无法以此宣称既有全部窗口/退出行为已证明有界。
- 全隐藏测试可证明返回语义、FIFO/lease/viewport/历史/保存和软件呈现身份，不能证明实际屏幕穿透、滚轮到达底层、真实 Mag 内容、视觉交接或物理笔行为。用户本轮限制仍禁止可见/前台/Computer Use；这些实屏验收必须保留为未执行门槛。
