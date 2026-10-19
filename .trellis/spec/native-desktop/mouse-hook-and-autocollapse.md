# Mouse Hook、PPT 滚轮与主栏自动收起

## 1. Scope / Trigger

修改低级鼠标 Hook、全局/本地 PPT 滚轮、四项自动收起策略或已接受起笔通知时应用本合同。既有原生 HiMsg 点击、Raw 私有拖动补充和 WM_TOUCH 来源会话继续遵守 [ui-pointer-session.md](ui-pointer-session.md)。

旧 IdtDrawpad.cpp 确有键盘 Hook → ExMessage → 画板 HiMsg → KeyboardInteraction → PPT 翻页；该文件目前不参与编译，Facade 不安装旧键盘 Hook。HiMsg 可以承载转译输入，但入队必须有明确的活动消费者。现行滚轮从 Bar owner 进入 PPT 业务队列，不新增四个 PPT HWND 的 HiMsg 绑定。

## 2. Signatures

- `MouseHook::{Start,Stop,PublishRoute,TryPop,DispatchWheel,PollPptWheel}`；交互 owner 负责发布路由和消费。
- `HookTransport::{RoutePublisher,Bridge,SpscRing,WheelAccumulator}`：无分配的生产传输及纯准入。
- `PptSessionToken` 位于轻量头 PptSessionToken.h；typed wheel 将捕获的 token 原样传入最终业务请求。
- `config.UI.Bar.AutoCollapse.{Drawing,Erasing,Dragging,OutsideClick}`，默认 `1/0/0/0`，均为 NoUpload。
- `ShouldAutoCollapse(int, BarBottomDockMode)`、`DragCollapseGate`、`OutsideCollapsePending`。
- `Bridge::AcceptedStrokeBegin`、`HostAcceptedStrokeState`、`Host::IsAcceptedStrokeCurrent()`、`Bar::NotifyCanvasStrokeAccepted()`。

## 3. Contracts

- Hook 专用 jthread 消息泵同线程安装/卸载；Start/Stop 串行，先停 producer 再结束 Bar owner。安装失败有一次 owner 日志。回调对负 code、停止、普通 Move 立即链传，不执行 UI/COM/渲染/配置/日志格式化/HiMsg/业务锁/任意业务回调；三个鼠标键缓存用固定原子位，键盘缓存合同保持。
- 路由载荷全为原子，Hook 只尝试一次稳定读取；不稳直接链传。事件复制点位、时间、delta、来源、路由/生命周期代次和 PPT token，不保留系统 lParam 指针。SPSC 256 槽有效容量255，满/owner超过250ms未续租/停止/零delta/提升wheel不接管；提升点击仍可作为链传的栏外候选。
- 入队提交是 wheel 抑制的唯一线性化点；失败不得留下稍后执行的副本。Bar 顶层和 Seek/颜色/粗细等嵌套 TryRead/Wait 共用有限批量 drain，沿原轮询消费，不新增后台业务线程或 Hook 定时业务。
- 本地 PageControl 原生 wheel 保留 targetVisible/inputLocked/workspace wheel 门禁及 Selection 语义，进入独立64项有界 mailbox，不能充当 Hook SPSC 的第二 producer。WhiteboardExpanded.wheel=false，不得将旧 PPT wheel 回落白板 callback。
- 两路累计器的唯一写者为 Bar owner；signed delta 净累计至120，余数保留，多刻度逐次尝试原业务门禁，拒绝不留欠账。模式/路由/会话变化清余数。捕获 token 在 owner 与最终业务线程复验，不得重新 CaptureCurrent 认领旧事件。保留 Previous/Next outstanding；同 token 的 EndShow 至多一个 pending/in-flight，完成/异常/过期/停止释放，旧完成不能释放新 token。COM ABI、焦点策略、确认策略不变。
- 新三态值固定0=关闭、1=非底栏模式时、2=任何时候；1仅 Floating允许，自由底栏与居中底栏同属 BottomDocked。两个主栏样式均显示且生效。只读写 Inkeys3 main.json，不继承/迁移/双写旧 deploy 的四个 bool。JSON 仅可表示为 int 的整数0..2有效，其余分别回 schema 默认。
- 设置仅变化时沿既有 FIFO 冻结写盘；修改选项不即时收起/展开。三语说明明确画笔/形状/激光与居中底栏，布局按字体测量和换行。
- 真实新笔迹初始化全部成功并进入 active 后才发一次 accepted begin，类型复用本笔实际 runtime tool（包括右键/笔尾覆写橡皮）；断触续接、hover、拒绝/隔离、重放、尾迹不重复。原聚合 drawingActivityChanged(bool) 继续用于副面板/光照，不能用于推断实际工具。
- Host 生命周期、逻辑场景（换页/工作区/Clear/恢复）与 PPT identity 共同退休旧起笔。检查逻辑场景，不依赖延后 Present 回执；已结束短笔同场景仍可触发。每笔单独判断 Drawing/Erasing，多指不只看0→1。
- 主按钮仅实际越过既有拖动阈值后判断 Dragging，一手势一次。非底栏策略须真实脱离后才触发，居中转自由和弹性变形不算脱离。自身发起 fold 更新当前 pointer fold 基线，清副面板/居中并发布形态 barrier；保留逻辑抓点、Raw/Touch 归属和单终态，外部 fold/cancel 仍取消。
- 栏外点击在 Selection/ClickThrough 下生成100ms候选。成功上屏区域/原生栏内Down排除栏内；新的栏内鼠标/触摸/笔输入、设置/工具/dock/工作区/折叠变化取消旧候选。候选只作幂等 Fold，不重投 Down、不产生第二笔输入。

## 4. Validation & Error Matrix

| 场景 | 结果 |
| --- | --- |
| consumer 停顿、队列满或快照不稳 | Hook 固定步骤返回，未接纳 wheel 链传且无幽灵副本 |
| 30+30+60 / -30反向 / 大delta / 换场 | 同一路由累计，换场清余数，旧 token 不影响新场 |
| 原生本地 Selection wheel | 保留本地门禁，不能套用全局电脑操作模式排除 |
| 短笔已结束、多个实际工具并发 | 每个有效起笔按真实类别处理，不看聚合计数是否已归零 |
| 非底栏拖动策略，底栏弹性或居中脱离 | 未离开竖向底栏时不收起；真正 Floating 后才判定 |
| 当前手势内部自动 fold / 外部 fold | 前者继续同一拖动；后者按原版本门取消 |
| 栏内输入或场景变化发生在100ms期限内 | 旧栏外候选取消，后续重新启用不复活 |
| 新配置缺失/布尔/浮点/超宽或越界 | 四字段独立默认，不读取旧 bool |

## 5. Good / Base / Bad Cases

Good：Hook 提交值型事件即返回，Bar 内真实消费者做累计和最终请求；底栏拖离后自动收起仍保持同一个抓点。

Base：自动收起关闭时仍保留副面板关闭与既有点击/拖动输入合同。

Bad：钩子内调用名为 Queue 的同步业务；只向 HWND HiMsg 入队就假定会翻页；用绘制活动 bool 猜实际橡皮；自动 fold 后全局放宽所有手势的失效检查。

## 6. Tests Required

- Headless 直接调用生产 Bridge/RoutePublisher/Accumulator/EndShowGate，覆盖快路径、满队列、owner暂停、并发快照、DWORD回绕、代次与单次抑制，聚合 P50/P95/P99/max；不安装系统 Hook，不把下游 CallNext 耗时算作本应用回调。
- 真实 Config 写读/ReadMini/冻结副本、非法值及旧 bool 隔离；双样式×三态×Floating/自由底栏/居中底栏矩阵。
- 生产 HostAcceptedStrokeState 覆盖短笔、首帧前、scene ABA、Clear、Host restart、PPT identity/admission；Drag/Outside 生产 gate 覆盖阈值、底栏脱离、单次及期限取消。
- i18n sync/check、完整 InkeysRepo.sln Debug|ARM64、Headless --no-window 与适用初始化前生产探针。真实系统光标手感、PPT/WPS、UIPI、Win7和触摸板/触摸均需另列人工运行条件；纯逻辑通过不证明设备行为。

## 7. Wrong vs Correct

```cpp
// Wrong：名称为 Queue 仍可能同步持业务锁，不能从 Hook 调用。
Ppt::QueueGlobalWheel(delta);
return 1;

// Correct：只在实际入队提交后抑制；业务在既有 owner 中执行。
const auto admission = bridge.TryCommit(event, now, heartbeat, lifecycle);
if (admission == Admission::Suppress) return 1;
return CallNextHookEx(hook, code, message, lParam);
```
