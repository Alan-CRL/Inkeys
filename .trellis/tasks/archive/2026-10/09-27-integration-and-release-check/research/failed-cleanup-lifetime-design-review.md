# E02 C 失败清理寿命合同独立设计复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 只读核对真实 AGENTS、保存的完整 hook/context、check.jsonl 引用、当前 PRD/design/implement、父 handoff/performance 与生产调用链；只写本报告，不改产品/工程/spec/账本，不构建、运行进程或 GUI。

审查对象：`research/failed-cleanup-lifetime-contract.md` 最终冻结版本，SHA-256 `4A7EDE9C5C44AEF068F892BDC489B3203D270D1A10A8288CCEA577D16CCBF683`。该文件是下一步设计；其接口、helper 和 C 测试尚未实现。

## 结论

**GREEN_DESIGN。** 固定共享 State、按值 owning Signal、单字取消/过期 CAS、真正 HANDLE join 和失败时 noreturn 的形状可以关闭上一版释放仍活上下文及误伤后续 ULW 的问题。按本冻结合同可进入最小分批实施；这不是源码或运行 GREEN，也没有解除发布/Win7/真实 callback 排空门禁。

15 秒只从显式已知失败起算的 cleanup grace；未完成清理到期再接受 fatal Close 并守 15 秒进程截止，约 30 秒是这个新失败 episode 的技术预算。普通 Close/Restart 已发布的原 15 秒绝对 tick 必须取更早值，不能重置为 30 秒。应由 root 在接线时保留这一口径；不把一般 cold start 或未返回系统调用写成有时限。

## Findings (fixed)

- File：`failed-cleanup-lifetime-contract.md` 的 Presenter 合同；生产证据 `Draw3.TransparentPresentation.cpp::Impl::TryInitialize`，约 703–727。
- Issue：先前文案只在 TryInitialize 返回 false 后 Begin，实际 ConfigureWindow/CreateSwapChain/renderer.Init/InitializePresenter 的失败分支先 cout。已知失败日志若背压，monitor 仍 Dormant，外层 release 钩子未被执行。
- Fix：作者最终版已将 local release Signal 传入私有 TryInitialize，在终止该 mode 的失败日志前 Begin。requireMode/最后 mode 在同处将同一 local deadline 交给 outer；ReleaseAttempt 后必须真 join 才能试下一 mode。本 reviewer 未修改设计或产品。

## 实际调用链与寿命核对

| 重点 | 当前生产事实与冻结合同评价 |
| --- | --- |
| Host 返回 false 之前 | `Draw3.Host.cpp::Impl::Start` 约 1199 发布 startupCompleted 后，1221 释放 Controller/presenter/renderer/device；1234 graphics-failed 和 1278 startup-failed 分支均在 return false 前 join/drain/detach。仅 Main 收到 false 后开始 timer 不覆盖这些步骤。冻结方案在绘制 owner 的已知 graphics/first-frame/catch 失败，以及 RTS false/catch 前置 Begin，Signal 必须由 lambda 按值拥有。 |
| 跨 owner 与迟到 Signal | State 不存 Main 栈/Host/Window/业务对象，publisher 是进程寿命普通函数。scope、Signal、monitor envelope 各持强引用。Cancel/join 后旧 Signal 仅访问旧 Cancelled State；wake HANDLE 由最后引用释放。Begin 已赢 CAS 但尚未 SetEvent 时 Cancel 先完成的交错也不会访问已关闭/复用的 event。 |
| Dormant/Cancelled/Expired | control 初值 0 不带启动时钟；Expired 将原 deadline 编入高位，同一个 CAS 决定终态。Complete 看到已到期竞争 Expired，不能用另一字段尚未写入的 tick。Expire 胜者不返回；Cancel 胜者只在真实 thread HANDLE signaled 后返回。64 位原子 lock-free 是 Win32/x64/ARM64 编译门。 |
| prepare/join/API 失败 | PrepareOrFatal 不返回可忽略 bool；取消唤醒失败、WAIT_FAILED、管理 join 超时转当前线程 noreturn，保留 monitor HANDLE/State/业务栈。禁止 detach/TerminateThread/CloseHandle 冒充 join。约定的 1000ms 是 join 管理上限，不改变普通保存 worker drain。OOM 路径必须使用已保存静态 publisher，无再分配/新线程/日志。 |
| 不重置普通截止 | `IdtMain.cpp::SetOffSignal` 264 附近首次 CAS 决定意图，`ShutdownSupervisor.cpp::ArmCore` 在创建 fallback/helper 前发布原 `g_fallbackDeadlineTick`。新 publisher 只 CAS/原子 BeginShutdown/offSignal/Wake 与读取 tick；不能调用 SetOffSignal 的隐藏队列、logger、CrashHandler 或 Arm。helper 先存自己的 tick，再取已发布更早 tick；state1/UEF 不调用 E01 state3-only 接管 API。 |
| Main DComp 重建 | `IdtMain.cpp` 2611 附近的日志→StopProduct→Window.StopAndJoin→新 Window.Start/ULW.Start 必须保持为一个首失败 scope，不能在 Host false 时取消。真实旧 HWND 和 producer/consumer 清理完成后 Complete 真 join；全局意图仍空才进入下一代。首 Host 成功也必须正常 Complete 后继续。 |
| presenter/optional-window 局部恢复 | `TransparentPresentationController::Initialize` 801/809 的失败 ReleaseAttempt、`Window.cpp::CreateWindowFor` 907/938/991/1004/1026 的 rollback 均在 bool 返回之前。单独局部 episode 是避免后续正常初始化背上旧 clock 的必要增量。中间失败 outer 保持 Dormant；最终失败内外同 tick。每次局部 cleanup 真 join 后再继续，不扩展 Recover/Resize/普通 Present。 |
| Window promise 前路径 | `Window.cpp::RunGroup` 747 附近可在 readyPromise 前进入失败；CreateWindowFor rollback 和 CreateGroup false/catch 必须提前激活，随后 `StopUnlocked` 690 附近 join 也继续同 scope。optional Magnifier 局部 cleanup 可失败后继续，但不能让外层普通后续创建受旧截止约束。 |
| RTS 停止失败 | `Draw3.RealtimeStylus.cpp::Shutdown` 2624 附近目前 Disable/Remove FAILED 仅 Log 后继续 CloseAllProducerContacts、Release、清 coordinator。产品持有效 owning Signal 时，FAILED 必须在这些动作之前 FailUnprovenProducerStop；保留 removedPlugin 的临时引用与 Host/input/HWND，禁止 return false→下一代 Reset。Enable 失败且 pluginAdded=true 也在此范围。 |
| 工程边界 | `InkeysHeadlessTests.vcxproj` 137 附近直接编译 Window，未链接 Main/ShutdownSupervisor。新增 helper 普通 header/cpp 不 import 产品模块、无 Main 符号依赖，允许每个 EXE 独立登记一份；Main/ShutdownSupervisor/工程仍由 root 单 writer 接线。不得为测试把完整产品监督器拉入 Headless。 |

作用范围已经收敛为固定 helper 和必要的 Window/Host/RTS/presenter 参数，不需要重写正常 drain、COM 所有权体系、每 packet registry 或 GPU 初始化所有 HRESULT。成功路径有 monitor 创建/唤醒/join 成本；应记录启动成本，不声称零开销或性能改善。

## Findings (not fixed)

- **P2，明确保留的低层范围**：TryInitialize/renderer/COM 内部尚未返回的调用、内部 local COM 析构/日志和异常栈展开在外层 catch 前，不受这些外层可观察失败钩子保证。当前合同已诚实限定，不要求扩成所有 HRESULT 的 timer。本 reviewer 没有现场栈，不能把这些写成已确认产品故障；若实现仍在已可观察失败处先日志，属于必须修的合同偏离。
- **P2，运行证据门**：成功 Disable/Remove 返回并不由本文件证明全部 callback in-flight 已排空。既有 gate 也不是完整 fence。本次 FAILED fail-closed 可以防止已知静止失败后释放/Reset；成功返回的真实 provider/回调交错需单独验证，不能以 watchdog 存在宣布已安全。
- **P2，尚无实现**：helper 真实 fatal tick 的首次 CAS、所有 early returns/异常分支、module global-fragment include 和三个架构 lock-free/链接都要审最终 diff。设计 GREEN 不能代替这些源码/编译证据，未自行修改公开接口或接线。

## 实施后最小验收

1. 同一生产 helper 的 Dormant 超 15 秒、Cancel/Expire 明确 barrier 竞争、迟到旧 Signal、Begin-CAS 后暂停再 cancel、prepare 故障与真实 monitor join 停滞；失败不释放 State/返回 caller，重复 Begin 不延长原 tick。
2. 真实 Host known-failure owner、Window promise 前 rollback、presenter ReleaseAttempt 与旧 Window owner join 的受控停滞；和无停滞/放行、DComp→ULW 成功反例成对验证。成功反例须旧 HWND 无效、新唯一 generation、首成功 Present、超过旧 grace 后继续一笔且未占 Close，不能只看 Start=true。
3. 正常 Armed Close 下真实 Host render、Desktop write worker、PPT UInk 已提交/index 未换分别暂停；保留原 15 秒，父超时强杀只记 FAIL。Desktop 需实际进入 writeDelay 的默认空 event 证据，accepted/pending 不能证明已到 Sleep；PPT 复用现有两个提交 event。
4. 强退后 fresh 生产 AutoSave service/UInk codec Load：核最后 Committed 的 identity/binding/SlideID/revision/笔迹；PPT 旧 index 指向有效 version，pending/孤儿新版不记 Saved。durable sentinel 不能替代恢复。
5. root 串行完整 Debug|ARM64、严格 no-window/相关套件及可得 Release 三架构；实际调用点/source 身份最终独立复审，更新 spec 与 HF。仅控制门停滞的红→绿不冒称自然驱动故障复现。

## Verification

- Lint：未运行；按研究分工只读，未改源码。
- TypeCheck / Build：未运行；C++ Solution 类型/链接门留给 root 的完整 MSBuild，本报告不占构建槽。
- Tests / EXE / GUI：未运行；没有动态 PASS。源码/合同边界和 frozen SHA-256 已核对。
- Spec：实施后需同步 errors/resources、Draw3/input 的 scope、stop-failed 和剩余运行边界，由 root 所有。
