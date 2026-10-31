# Hook/PPT 规划复核（仅规划，2026-10-19）

已按顺序阅读 `decisions.md`、`prd.md`、`design.md`、`implement.md`。四项独立 Config、两种样式、默认值和 Drawing=Pen/Shape/Laser 均一致。未实现、编译或改产品文件。

## 需在实现前补清的必要点

### 1. 明确本地滚轮与 signed-120 累加器的唯一 owner

design §1 定 Hook 桥消费者为 Bar 交互 owner，§2 又让 PageControl 的本地 WM_MOUSEWHEEL 进入同一准入。目前本地 WndProc 在 Window Service overlay owner，二者不是同一线程。若两者直接调用共享累加器会产生多 owner；若把本地事件直接塞进为 Hook 设计的 SPSC ring，会变成两个 producer。

最小建议：明确 signed remainder/admission 只归 Bar 交互 owner，本地 WndProc 保留现有 visible/inputLocked 检查后只转送值型请求；传输要么独立于 hook 的 SPSC ring，要么明确为有界多生产者实现，不能假设仍单生产者。两个来源都携带捕获时 expected token，不能靠 msg time 相等去重。共同的是身份、余量与业务输出；global 的“非电脑模式才接管”不应未经决定套到本地控件 wheel，否则会改变 Selection 时本地 wheel 的既有行为。

### 2. expected token 必须进入最终 PPT business request，文件范围需要 IdtPlug-in.cpp

当前 `IdtPlug-in.cpp:105–110` 的 `QueuePptUiBusinessCommand` 会无条件 `CapturePptSession()`，而 UI callbacks 是 `void()`（`:964–968`）。只在 Bar/Ppt/PageControl 先验证 expected token，再调用既有 callback，仍存在“验证之后换场→重新捕获新 token→旧 wheel 作用新场次”的缝隙。

最小建议：在规划写明扩展必要的 typed wheel/business ingress，使最终 request.session 直接采用事件的 expected token，执行前沿现有 `IsCurrentPptSession` 拒绝。把 `IdtPlug-in.cpp` 及必要 callback 声明列入最小文件范围；仅加一次前置检查不足以实现 PRD 的跨场隔离。新 ingress 明确仅构造 PPT command，不通过通用 InvokeDirection 回落白板。

### 3. EndShow 也要有界，不能只说沿既有 outstanding

当前 outstanding 仅覆盖 Previous/Next（`IdtPlug-in.cpp:107–109`、`:1025–1026`），不覆盖 EndShow。结束黑页多刻度/连滚在异步 owner 快速消费、业务线程慢或忙时可以持续产生 EndShow request；“沿既有 outstanding”本身不能满足“不无限排队”。

最小建议：给同一 PPT token 的 EndShow 做至多一项 pending/in-flight 的幂等准入，完成、拒绝、过期或停止时正确释放；不新增确认、不修改 managed exit ABI。也可使用统一有界的 wheel-intent mailbox，由 PPT business owner 直接执行，但必须写清容量/丢弃策略。

## 建议收紧的实现约束（非产品决策阻塞）

- **入队与 wake 的提交点**：建议直接沿 Bar 已有、所有嵌套连续交互都会调用的统一 poll 入口 drain；不要为了 wake 再加一个可能失败的通知步骤。若需要可选 wake，入队成功就是唯一 admission/抑制提交点，之后 wake 失败不能改为 CallNext、让同一已提交事件稍后再执行。只有入队尚未提交时才允许失败链传。设计现有“不能幽灵副本”要求正确，但需落实此确定协议。
- **嵌套交互仍消费桥**：除顶层 PollInteractionMessage，还应从共用 TryRead/Wait 入口消费，保证 Seek、选色、粗细/轮盘等按住循环不让全局桥积压或让 100ms owner timer 长期饥饿。每轮 drain 要有限 batch，不能清空到队列永久为空才让指针继续。
- **余量归属**：同一 owner 上，余量属于有效 token+路由 generation+workspace/mode；切身份立即退休。被 outstanding 拒绝的完整 120 意图已消费，不保留成以后跨时执行的欠账；只保留 signed 模数余量。恢复后不能把“未转成一页的 30”流到新场次。
- **快照与热路径**：§1 已正确禁止业务锁、堆分配、无限 retry。实现检查要覆盖路由 snapshot 本身的读法，不能把多字段 token 放进不保证 lock-free 的大 atomic，或用无限读取直到一致；固定尝试次数失败时链传。
- **性能证据范围**：当前 AC1/验证边界正确，指标只量本程序 callback body/入队，消费者停顿测试证明不反压；不将 headless 合成基准冒充真实系统 hook/光标验收。

除上述 1–3 的线程/身份/有界性细节外，未发现需要改变用户已确认产品需求的冲突。以上属于将现有 PRD 承诺写成可实现合同，不替用户新增产品选择。
