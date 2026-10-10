# P1 实现说明

## 归属和边界

实现位于 `Inkeys/Inkeys/Business/PresentationLinkContracts.h`、`PresentationLinkService.h/.cpp`，命名空间为 `Inkeys::Business::PresentationLink`。采用普通 C++20 头/实现文件和标准库纯值，单一 owner 串行调用；产品本轮没有消费者。没有创建 COM、ROT、线程、窗口、IPC、helper，也没有修改旧 PptSession、Draw3、token、UI 或 PptCOM dynamic/释放/ABI。

`Inkeys/Inkeys.vcxproj` 新增两个 ClInclude 与一个 ClCompile；headless 工程新增服务、测试及两个头登记；既有 animation main 仅新增测试声明和调用。没有更换构建系统/工具链或变更依赖。

## 实際合同

- Envelope 首版明确接受 1.0，Full/Delta/Heartbeat 分开；源由 RegisterSource 明确建立 epoch。包不能自动登记新来源/epoch。换代保留有界 retired epoch 集合。
- 候选目录以局部副本验证后发布。保留有界 candidate generation/state 高水位及显式删除/结束 tombstone，目录、身份 UTF-8、总字节、拓扑、源及命令均有界。
- 初始、断开、sequence gap、超时后必须 Full；没有连续的其他可靠来源时分配新 logical show。只有可信 adapter 验证的 session/PID/process creation/HWND/window lifetime 证据才跨源归并，local generation、路径、标题和页数不能证明同场放映。
- heartbeat 只推进 source 接收时间，page/activity 各用自身 revision 维护本机事实新鲜度；重复 activity revision、页包、其他重复来源不增加挑战样本。
- 先按统一活动等级选 show，再在 show 内按可用性、页面身份/能力及 backendPreference 选来源。同等级保持；无 current 按稳定 source/candidate 键决定。
- 真实已核验 foreground 对应的快照未准入/Busy 时也先撤销后台命令 route。Inkeys 焦点保留外部上下文，selfInduced 不提升挑战；随后新的真实外部 show 仍走快路径。
- 弱挑战使用持续窗口和同一独立证据来源的新 observation revision；source 恢复、soft interval、reverse window 独立。所有测试时间参数为调用方显式测试值，无产品初值或实测时延声明。
- BindingRequest 只进入单一 latest outbox；Confirm 核请求、source/epoch/candidate/generation/logical show、最新仲裁意图以及目录内同一完整纯值 snapshot。失败/timeout 屏蔽该未变 stateRevision，避免每 tick 重绑；新可靠状态才可再准备。
- 同 show 换源保持 showSessionRevision，仅已证明稳定文稿/页面映射才保留 targetRevision；临时 Ordinal 跨源不继承旧页面安全标记。正常当前绝对页更新直接推进 target，不等待弱挑战窗口。
- Selected.pageSafe 只表达事实及页面身份准入，不能代替 Draw3 Present/UI ack/input ready。Ordinal 来源在新独立场次内仍需后续 bridge 按会话隔离，不能继承稳定 SlideID 墨迹。
- 命令仅一个确认 route；GuardCommand 供 fake/后续执行 owner 重用。服务 commandId 用单调高水位防重放，执行 ledger 在 source epoch 内防重放。提取后 timeout、断连或 target 变更为 OutcomeUnknown，不改投。ACK 只改有界结果账本，不改页/绑定。
- `LegacyAutoSelect` 观察合同保留，但执行 guard 拒绝 BrokerSelect 命令；真正旧/新 owner 接管与模式切换仍由 P2/P4 实现。

## 已知限制

ShowIdentity 的 verified 标记由可信 adapter 提供；P1 不调用 Win32 核实真实 PID/创建时间/窗口关系，也不设计 pipe peer 鉴权。P1 不冻结共享内存布局或 byte wire schema，不验证真实 COM 全候选目录。真实 Win7 SP1/.NET4、跨权限 helper、Office/WPS/损坏 COM、释放与端到端呈现时延均 NOT VERIFIED。

implement 版本最后一次完整 Debug ARM64 solution build 和随后 --no-window headless 均退出 0，107 项合同/仲裁断言通过。本说明中的“最新仲裁意图”和“仅一个确认 route”是需要满足的合同；独立检查正在验证同候选强证据降为弱证据、排队期间能力/执行模式撤回，以及非法 payload 同时发生 sequence gap 的边界。源码/测试所有权已交 checker，其最终修复与验证结果才决定 P1 完成门禁，详见 validation.md。
