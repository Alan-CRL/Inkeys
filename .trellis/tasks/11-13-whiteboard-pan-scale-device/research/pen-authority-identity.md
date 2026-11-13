# Research: Pen 导航权威反馈的稳定身份与 ABA 隔离

- Query: RTS cursor/contact 与 WM_POINTER pointer 的真实身份关系；如何纠正陈旧 Touch bool 造成的 provisional 反馈抑制，同时不清新笔、不按近似时间/位置猜测？
- Scope: mixed（现有身份/发布链、当前规格、Microsoft 官方 API）
- Date: 2026-11-13
- 状态：本报告只写 research；实现代理正改相关模块。下列“基线”指研究读取时仍存在的原函数/字段合同，不评价半成品新增实现；行号后续会随插入移动。

## Findings

### 1. 最关键的事实

1. **基线 DrawingCursorSample 没有 pointerId。** 它只有 x/y/qpc/valid/inverted/inContact/sequence 与 InputSource。不能给它编造 pointerId 来源。见 [PenCursor.cppm:59](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cppm:59)。
2. `source.contextId/cursorId/generation` 是 RTS 来源/decoder 生命周期身份，不是每次 Down 的身份；同笔连续多次落下可以完全相同。`sample.sequence` 是 mailbox 发布代次，每次发布都会推进，也不是笔划 token。
3. RTS 同一个 Down 的 ContactSnapshot 与 cursor sample 使用**完全同一个 source 和 qpc 值**，这是复制同一事件的精确关联依据，区别于“两个 API 到达时间接近”。但 QPC 不是保证永不重复的序号，需要独立 token 或唯一性检查。
4. pointerId 与 tablet cursorId 不同。只有消息线程成功执行 GetPointerCursorId(pointerId) 的结果，才能把 Pointer 范围与 tablet cursor 关联；不能把 contactId/cid 数字当 pointerId。
5. source/cursor 匹配只能证明同一个 tablet cursor（通常为笔的一端），不能证明它的某一次 RTS Down 与某一次 WM_POINTERDOWN 是同一笔划。RTS 接管期间甚至可能没有对应 Pointer Down。

### 2. 基线数据流和字段含义

| 字段/对象 | 实际来源及生命周期 | 不可替代的用途 |
| --- | --- | --- |
| StylusInfo.tcid | RTS tablet context | 与 cid 一起定位当前 producer binding |
| StylusInfo.cid | RTS stylus/tablet cursor | 生产者 API 中的接触路由键；基线传给 coordinator 作 contactId |
| InputSource.contextId/cursorId | SourceForCursor 在末尾写入 decoder.tabletContextId / stylusInfo.cid | 来源诊断与设备身份，不是每笔编号 |
| InputSource.generation | decoder cache 的生命周期代次 | 映射/decoder 变更可失效旧身份；普通 Up/Down 不保证改变 |
| ContactHandle.record+generation | coordinator 分配 record 并递增该槽 route generation | 消费者辨认同一 record 复用后的 ABA；RTS 发布 cursor 时尚未取得它 |
| Canvas panKey | Controller 为平移运行时分配的单调身份 | Dynamics decision 对应本地 runtime；也不是 Windows pointerId |
| DrawingCursorSample.sequence | DrawingCursorSampleMailbox 的 seqlock 发布代次 | 跨字段一致读取和变更检测；不应作为跨 producer 的每笔 token |
| WM_POINTER pointerId | LOWORD(wParam)，当前 Pointer in-range 生命周期身份 | Pointer 本地路由、可选触觉 API 绑定；同范围内可多次 Down/Up |
| PointerDetails.cursorId | GetPointerCursorId(pointerId) 返回的 tablet cursor | API 支持时的明确跨设备身份桥 |
| Haptic range revision | CanvasPointerMailbox 对 Pointer 范围的内部代次 | 防旧范围绑定复活；不是每笔 Down 代次 |

源码锚点：

- [RealtimeStylus.cpp:890](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:890)：SourceForCursor 用唯一 cursor 查 pointerSources，检测歧义，最后写 context/cursor/generation。
- [RealtimeStylus.cpp:535](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:535)：RtsActiveContactBinding 当前只有 tcid/cid/decoder slot+generation/type/occupied，没有每笔 feedback token。
- [ContactInput.cpp:785](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp:785)：PublishDown 才分配 record route generation；传入 snapshot 按值复制、锁存 admissionRevision、入 ingress/navigation。
- [ContactInput.cppm:153](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cppm:153)：ContactHandle record+generation。
- [PenCursor.cpp:235](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:235)：mailbox 每次 Publish 均更新 qpc/source/sequence。

### 3. RTS 的 Down、Move、Up 与 cursor

`StylusDown` 在 decoder/binding 建好后 DecodeSnapshot(...,QueryQpc())，设置 SourceForCursor 和 inverted，再调用 PublishPenCursor，最后 coordinator.PublishDown。因此 cursor 可先于 contact 出队可见；不可据此认为它已经获导航准入。见 [RealtimeStylus.cpp:1242](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1242)。

`PublishPenCursor` 对 Pen 才执行，把 snapshot.position/qpc/source 原样复制到 DrawingCursorSample，并设置 valid/inContact/inverted。见 [RealtimeStylus.cpp:1663](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1663)。Packets 更新同一路径；InAirPackets 是同设备的 hover/presence，不能自行创造 Down。普通 Move 后 sample.qpc 已不再等于 Down qpc，所以不能用“当前 latest.qpc 与 verdict.downQpc 是否相近”判断一笔。

基线 `StylusUp` 解码真实终态或关闭错误包，producer routing 用 tcid/cid；部分路径调用全局 PublishDefaultPenCursor/ClearPenCursorSample。并发多笔时，全局 latest/clear 本身没有 per-contact 语义；新身份旁路必须按 binding token 关闭对应生命周期，不能照抄一个 global Clear 去清另一个新 token。见 [RealtimeStylus.cpp:1292](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1292)、[RealtimeStylus.cpp:1682](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1682)。

### 4. Pointer 的真实桥与现有信息丢失

`QueryPointerDetails` 在窗口消息线程调用 GetPointerType/GetPointerInfo/GetPointerPenInfo，成功时再 GetPointerCursorId，保存 cursorId。当前没有把 POINTER_INFO.sourceDevice/PerformanceCount 留在 PointerDetails。见 [WindowControl.cpp:187](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:187)。

WM_POINTER 构造 cursor sample 时只有坐标、inverted、inContact 和接收时 QueryPerformanceCounter，**没有填 source，也没有 pointerId 字段**。然后 PublishPenCursorSample 可能把 lastKnownPenSource 继承给空 source。这个继承用于显示策略连续，不能升级成真实身份关联或让旧 RTS verdict 清当前 Pointer suppression。见基线 [WindowControl.cpp:1571](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1571) 的 sourceKnown 分支和 WM_POINTER sample 构造分支。

RTS 的 qpc 是回调接收时刻，WM_POINTER 的 qpc 是消息消费时刻；它们没有在当前实现中共享同一个 Down 时间凭据。不能把这两个 QPC 的接近、顺序或坐标接近当作跨 API 笔划相同的证明。

### 5. 推荐最小方案：独立生命周期旁路，不改变 latest sample 语义

保留通用 latest mailbox 的现有语义。由集成实现代理负责在 PenCursor sink 增加默认空实现的小型元数据回调；历史测试 sink/demo 不因纯虚函数新增而被迫实现。由 RTS producer 在已序列化的真实 lifecycle 发送元数据，不由窗口 latest 状态猜 Down。

概念数据（非最终 C++ 命名）：

```text
PenFeedbackContact
  producerRun / token         // 每个真实 Down 唯一，0=没有凭据
  rawTcid, rawCid, sourceGeneration
  downQpc                     // 同一 Down 的原样复制值，只用于事件核对/诊断
  sampleQpc, phase            // Down / Up / Cancelled；必要时仅更新 presence
  source                      // 保留真实识别，不使用 lastKnown 继承值
```

Token 可选择整个进程内单调非零 serial，或 `{producerRun, perRunSerial}`；不能在插件重建后从 1 重新开始又不带 run。mint 在 Down binding 建立时，存入该 binding；普通 Move、真实 Up、无包取消、duplicate Down 关闭旧 binding、UpdateMapping/Disabled/Error/销毁的取消都沿原 token。未成功发布 coordinator Down 的候选也应发送同 token 的 Cancelled，避免窗口永远等待一个不会出现的准入 verdict。

这里的 sourceGeneration 与 feedbackToken 含义分开：前者可以跨多个笔划相同，后者每次 Down 都不同。也不把 per-contact token 放进 InputSource.generation 冒充设备代次。

#### 5.1 把 token 送到 Controller 的两个可行入口

**最直接：** ContactSnapshot 增加不可变 feedbackToken（或窄小 contact credential），与元数据/cursor 同个 Down 一起发布；Controller 从 DownSnapshot 取得。需要同步 ContactInput 的快照读取/导航样本传播，而不能只添结构字段。基线 ReadSnapshot 自行构建 candidate，仅显式复制 source，不自动复制 DownSnapshot 的所有字段。见 [ContactInput.cpp:299](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp:299)。该选项跨文件较多，须由主会话协调所有权。

**更小的旁路选项：** 不改 ContactSnapshot。Window 元数据表按 `{raw tcid,cid,sourceGeneration,exact downQpc}` 保存 token；Controller 由同一个 DownSnapshot 精确查询**唯一** token，并锁存在 runtime，后续不再按时间查找。这不是近似时间匹配，因为两边来自同一个 DecodeSnapshot Down 的字段复制。必须遵守：

- 不使用窗口 latest sample.qpc；不允许容差、最近值、坐标猜测。
- 同 key 多条（例如 QPC 分辨率碰撞/异常 duplicate Down）、已淘汰、epoch 不符或无法证明唯一时返回无凭据；不清任何旧/新 suppression。
- 历史项即使已 Up 也可供唯一查询，但返回的生命周期已终态，后续 accepted verdict 不能让它复活。
- 固定有界表满/漏元数据只降低可选反馈纠正能力，不能丢产品 Down 或阻塞输入；关闭项淘汰不应覆盖活动新 token。

第二种方案能适配“默认空 sink 元数据回调”的小范围接线，并且严格唯一性失败时安全。若希望完全避免 QPC 作为查找字段，选择第一种显式 token 传播；不能把未经唯一性检查的 qpc 本身声明为永不复用 token。

#### 5.2 有序权威结果

Controller 为 runtime 锁存 `{feedbackToken, handleGeneration, panKey, downQpc, navigationEpoch}`。Dynamics 输出 accepted/rejected 的 panKey 后，Controller 回查对应 runtime，向 Window 发布：

```text
PenNavigationVerdict
  producerRun, feedbackToken
  navigationEpoch
  runtime key / handleGeneration   // 本地查回/诊断，不能替代 producer token
  accepted / rejected / terminal
```

Window 在同一小锁下只修改匹配 token：run/epoch 当前、token 存在、仍是该来源的当前笔划、未 terminal、未被新 token 替代。旧 epoch/旧 token verdict 直接 no-op。Stop/离开移动模式应更新预期 navigationEpoch，不能等下一 Down 才拒绝迟到旧 verdict。

Provisional 和 authoritative suppression 分开记录：陈旧 Touch bool 可以临时隐藏反馈，但不能改变 core 接触准入；accepted 清的只是**这个 token**的 provisional 状态；rejected 为该 token 锁存直到真实终态。别的 Pen token 的状态不受影响。Getter 对当前显示/候选反馈来源求结果，不用一个全局 bool 代表所有笔。

### 6. Pointer bridge 的可用边界

Window 单独维护 `PointerRangeKey={pointerId, rangeEpoch, apiCursorId}`，必要时保留 sourceDevice 作为附加证据。rangeEpoch 在新范围/Leave/取消/复位后变化；普通同范围多次 Down 不等于新范围。若要逐笔 Pointer provisional，另加 PointerContactSerial，每次明确 Pointer Down 递增并在 Up 关闭，不能用 range revision 代替。

满足以下条件才建立 tablet cursor 到 Pointer **范围**的桥：GetPointerCursorId 成功、cursor 非零且与真实 RTS cursor 精确相等、实时映射唯一、无 source conflict、range/producer generation 当前。不能用空 source 经 lastKnownPenSource 补齐来满足这些条件。

即使范围匹配，也不意味着“当前 PointerContactSerial 等于当前 RTS token”。两个 API 没有共享本轮 contact token，且 Pointer Down 可能缺席；不应按第 N 次 Down、最近时间或当前位置盲目配对。特别是 Pointer 新 Down 已到而对应 RTS Down 尚未到时，旧 RTS accepted ack 不能清这个 Pointer 新 contact 的 provisional latch。

**因此本次更可靠的小边界是：** RTS verdict 纠正 RTS token 的反馈状态；Pointer-local suppression 按自己的 range/contact epoch 结束。不要在 RTS accepted 中无条件 `suppressedPenPointerId_=0` 或清全局所有 Pen suppression。若两者要合成一个可见状态，以当前明确来源的 token/范围为查询目标，未关联身份时保持可选反馈保守无操作，不能让 Pointer-only provisional 反向否决 core 已接纳的 RTS contact。

如实现确需清某条 Pointer-local provisional，必须在创建该 provisional 时捕获并保存可证明的 bridge 凭据（rangeEpoch+PointerContactSerial+已绑定的 RTS token），ack 对三者逐项相等才可清。没有该凭据时保留到本地 Up/Leave，而不是退回近似时间判断。对于无法证明同笔划的跨 API 到达顺序，不能宣称强一致纠正已经实现；可选择身份隔离优先，反馈暂缺。

### 7. 可借鉴而不应直接滥用的现有 Haptic mailbox

[HapticRequest.h:71](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.HapticRequest.h:71) 已明确把范围与笔划分开：Begin 记录 cursor/contact/generation/context/sourceGeneration；End 只在 contact+generation+context 相同才结束；旧 End 不清新范围；Observe 同 pointerId/cursor/eraserHint 的 Down 不另建 range revision。这是很好的 ABA 模式。

但不能直接把 haptic range mailbox 当导航权威表：它是单范围、会因 Selection/Pan/反馈关闭而 Reset，且允许同范围多次 RTS 笔划复用；不是多 Pen 导航队列或每笔认证 token。当前 `BeginHapticContact` 的另笔门控也保守拒绝多 Pen 触觉。新导航 metadata 至少独立于“本帧是否允许播放触觉”，不能因 Selection 本来禁连续触觉而丢掉反馈身份。

### 8. 边界矩阵

| 场景 | 安全处理 |
| --- | --- |
| RTS-only（Win7 或 Pointer 不到） | 用真实元数据 token 纠正 RTS/app cursor；无 pointerId 就不绑定设备触觉，不猜 |
| Pointer-only presence | 保存本地 range/contact；没有 RTS Down token 就无 RTS navigation verdict，不能制造 contact |
| source.kind/recognition Unknown，但 tcid/cid/gen 来自 RTS 元数据 | 不扩大物理可信；仍可用真实 producer token辨认本条生命周期；跨 Pointer 桥保守不可用 |
| 空 source 的 Pointer sample 被 lastKnown 补齐 | 只作既有显示连续策略，不授予 token/清锁权限 |
| 同笔 Up→新 Down，source tuple 不变 | token 必须不同；旧 verdict/Up 无法清新 token |
| 同一 pointerId 跨多次 Down | range 可复用，每笔 token/PointerContactSerial 不复用 |
| pointerId 数字复用或 Leave 后返回 | rangeEpoch 不同，旧 bridge/ack 无效 |
| 多个 Pen 同时存在 | 每 token 独立；全局 latest 只表达最后显示的样本，不代表唯一活笔；无法唯一桥接时不清其它笔，触觉维持原多笔保守规则 |
| token 元数据早 Up，core verdict 迟到 | 终态不可复活，accepted/rejected 不改后来笔；终态记录只供审计/精确查找 |
| 两个精确 down key 碰撞 | 旁路查询返回 ambiguous，不用最新一条覆盖旧条后谎称唯一 |
| producer reset / navigation Stop | 分别使 producerRun 或 navEpoch 失效；旧 ack no-op；不清另一个来源的新 run |
| 表满/缺回调/测试 sink 默认空 | 导航完整继续，缺少可证明反馈身份则保守不纠正，不猜测 |

### 9. 建议加入的生产共享 CPU 测试

- 同一 source 连续两 Down，旧 accepted/rejected/terminal 在新 token 后到达：新 token 状态不变。
- 指定样本中源身份完全一样，downQpc 也刻意相同：唯一查询失败或显式 token仍区分，不选择最近值。
- 已 terminal 的 token 收 accepted 不复活；Cancel 与后续新 Down 顺序交错不清新笔。
- A Pen/B Pen 不同 cursor/context 并发，A ack/Up 不影响 B；相同 cursor 被多个真实源声明时桥接失败。
- Pointer-only 样本经过 lastKnown source 继承，仍不能获取 RTS token；RTS-only token没有 Pointer 时可正确处理 app feedback。
- 同 pointer range 多次 Down/Up，以及 pointerId 离开后数字复用：旧 range/stroke ack 不影响新状态。
- 上一帧 Touch bool=true；真实 Touch 已 Up 后 Pen Down 先到 Window 再到 Dynamics，accepted 只清该 Pen token 的 provisional，不关闭其它真正 rejected Pen。
- Pointer 新 Down 已到，旧 RTS ack 后到，再到新 RTS metadata：旧 ack 不清新 PointerContactSerial；普通 latest sample继续只做presence。
- metadataDown 已发布但 coordinator.PublishDown 失败：对应 token Cancelled，不能成为永久 suppression。
- navEpoch Stop 与 producer reset 后的旧 verdict 都 no-op；默认空 sink/表满不影响 producer ContactSnapshot/Down 的完整路径。

## External references

- [GetPointerCursorId](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointercursorid)：返回 tablet cursor；实体笔的不同端可能对应不同 cursor 对象。证明设备端身份桥，不提供跨 API 笔划 token。
- [StylusInfo](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/ns-rtscom-stylusinfo)：tcid 辨认 tablet，cid 辨认 stylus，倒转是独立字段。
- [POINTER_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_info)：pointerId 仅在检测范围生命周期唯一；sourceDevice 与高精度事件时间是不同字段。
- [GetPointerInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerinfo)：数据关联调用线程最近取出的 Pointer 消息，后续取消息可令旧数据不可用。因此应在窗口消息线程捕获 API 身份，不把旧 pointerId 扔给绘制线程事后猜。

## Related specs

- [pen-haptics.md](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/pen-haptics.md)：Pointer 范围与 RTS 笔划分离、未知/多笔保守、禁止把 contactId 当 pointerId、迟到终态隔离。
- [plan-integration-review.md](D:/Project/Inkeys/Repo/Inkeys/.trellis/tasks/11-13-whiteboard-pan-scale-device/research/plan-integration-review.md)：Pending→有序 Dynamics 唯一裁决、窗口反馈是可纠正投影。

## Caveats / Not Found

- 没有硬件或 GUI 验证，不声称不同驱动必然同时发送两个 API 的完整 Down/Up。
- 本轮未改 common sample 或产品代码；建议的 metadata回调/token表属于集成代理的真实必要新增接口，须由它与主会话协调实现/测试。
- 当前基线缺少每笔共享 token，靠已有 source/generation/latest qpc 无法严格防所有 ABA。直接带 token最强；精确事件 key→唯一 token 是受限但安全的最小旁路，失败必须保守无操作。
- 如果要求对无法证明同笔划的 Pointer-local suppression也立即强制纠正，在不增加共享凭据的条件下无法可靠满足；应隔离身份保留本地终态清理，不把“相同 cursor”说成“同一笔 Down”。
