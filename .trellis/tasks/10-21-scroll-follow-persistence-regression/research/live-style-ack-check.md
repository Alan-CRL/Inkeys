# 独立检查：样式发布后的精确窗口 ACK 重核

日期：2026-10-22。任务保持 in_progress；本报告不结束、归档或提交任务。

## 范围与结论

本轮由主会话明确授权 core 对 `IdtState.cpp` 做最小生产修复，checker 仅只读检查及写报告，未修改源码、构建、运行产品 CLI 或启动 GUI。新的精确 ACK 事实谓词没有发现阻止有限真实 ink 测试的缺口；已通知主会话可由当前 GUI owner 继续原用例。这个结论是代码检查和已有编译/纯 CLI 通过，不是样式到系统窗口 ACK 的实屏通过。

读取当前任务 prd/design/implement/check JSONL、相关实际规范及调查报告；此前完整各阶段检查继续见已有 check 报告。本次重点增量为 `research/live-style-ack-investigation.md`、`scroll-follow-product.md`、真实 StateMonitoring/Reconcile、StateBridge/Host wait 和 ink setter 诊断。`get_context.py --mode packages` 返回 Single-repo project (no packages configured)，本增量涉及 native-desktop 及集成 Host 的 native 合同；没有修改 HLSL、PPT 或模块公开接口。已重读这两个 index 的验证要求及跨层指南。

## Findings (fixed)

此增量由 core 实施，checker 没有另外修改源码。

- 文件：`Inkeys/IdtState.cpp`，StateMonitoring 原条件约 2174。
- 问题：真实宽度/颜色发布可能推进 mode/bridge 而不推进 Host runtime/output/content。原监控仅在 runtimeChanged 或一次性 retryPending 时重核；并发新 Publish 的 retry 也可能被旧成功 Reconcile 的末尾清 false 覆盖。精确 ACK 已失效，却可能没有下一次确定性重核。
- 修复：在原条件后增加 `ProductRunning() && !ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch())`。沿已有 StateMonitoring 和原 250ms 等待重新调用真实 Reconcile，不新增线程、事件或发布，不改变 ACK 接受条件。
- 与实施前完整字节备份 `Build/ScrollFollow/product-live-stage/IdtState.before-style-ack.cpp` 比较，唯一源码变更为上述条件及中文原因注释。未更改 Publish/dirty/retry 存取协议、原 cadence 或 WindowService 事务。

## 独立行为核对

1. **没有丢通知依赖。** 新判断读取持久的 projection/mode/bridge ACK 事实，不以 retry 的一次 true 为唯一依据。旧成功回执即使把新 retry 清掉，当前 bridge/mode 与旧 ACK 不符仍会在下一原等待周期进入重核。并发快照不是整体事务，瞬间变更可延至下一周期；不因此接受旧窗口回执。
2. **没有发布反馈。** StateMonitoring 仍只有 `followProjectionDirty.exchange(false)` 分支调用 PublishDraw3State。Reconcile 不调用 Sync/Publish 或 RequestScrollFollowPresentationRefresh。新谓词本身只读取原子与 bridge 快照。它不制造 bridge revision 增长，也不设置 dirty。
3. **成功后停止额外重核。** 对稳定最新期望，原实际 WindowService 提交及 guard 成功后写 followAckBridge/followAckMode/followAcknowledged 并清 retry；精确 ACK 为 true 时附加条件 false。原 runtimeChanged 路径仍按原语义执行。未运行产品的附加项因 ProductRunning 短路而 false，不新增 Off-only Window 请求。
4. **锁和线程。** HostRuntimeRevisionSignal::WaitForChange 的 mutex 为局部 unique_lock，返回到新判断前释放；StateBridge::Snapshot 同样只在复制时局部 scoped_lock。判断未持 mode/presentation 锁调用窗口 owner。原 Reconcile 先在短 mode lock 读取后释放，再取得 presentation 锁；真实窗口提交、先后 revision guard 和失败重试均未改变。没有新增反向锁边。
5. **未更改可用性。** Business::Available 不读取精确 ACK，因此不能从这次缺口推断普通主栏按钮必然禁用。已证实的问题是稳定 style 发布后的 ACK 收敛保证不足。

## Ink 诊断增量

将当前 `Draw3.ProductLiveTest.inl` 与 `Build/ScrollFollow/product-live-stage/Draw3.ProductLiveTest.inl` 比较，只有新增 InkToolFacts 和 A/B setter 返回值、等待结果分开打印的展开。

- A 仍严格依次 SetPenWidth，然后仅宽度成功时 SetPenColor；只有两者成功才执行原 3s `PrimaryReady() && actual precise ACK` 等待。B 仍颜色成功后才执行相同等待。错误原因字符串、Down 前提没有放宽。
- InkToolFacts 只读取 mode、desired bridge、runtime、PrimaryReady 与精确 ACK 并打印，不发布工具或状态、不手写/刷新 ACK、不设置隐藏输入 seam。各快照独立取样，日志不是跨模块原子事务。
- 原真实系统输入的专用 SourceGT、实际光标命中自身 HWND、取消/安全门、Down 前物理左键检查、ownLeftHeld/有限 Up 和异常清理均无改动。

## Verification

### 编译与纯 CLI：复核 core 原始记录，没有重复执行

- 入口：`Build/ScrollFollow/build-motion-tracker.ps1 -Round product-live-style-ack`。脚本实际使用 vswhere 定位 ARM64 原生 MSBuild，同 PowerShell 规范 Path/禁 node reuse，完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，允许 600000ms。
- 主会话进程回执：exit 0，15.559s；MSBuild 文件日志记录 `Build succeeded`、0 Error(s)、6 Warning(s)、Time Elapsed 15.23s。6 条均为既有 additional/hashlib++ 的 C4267，两个重新编译 TU 各产生 3 条；未改第三方以消警告。
- 原始文件：`Build/ScrollFollow/motion-tracker-build-product-live-style-ack.log`、对应 `-console.log`、空 `-stderr.log`。
- `Build/ScrollFollow/product-live-stage/offline-215493359/result.json`：16 个坏 argv 均 exit 2，产品 stdout/stderr 0B；纯 protocol exit 0/264ms，state exit 0/131ms。逐条结果和实际 stderr 已读。
- protocol stderr 明确实际 Tick/Frame/Tracker，而外部 Capture/Host/ACK 为 seams、无 HWND；state stderr 记录原 workspace/shortcut/lease/cancel/FineDial 用例 PASS。这些不代替本次新增 StateMonitoring 分支的实际窗口验收。

### 静态、类型与格式

- Lint/静态：目标范围 `git diff --check` 无输出/exit 0；增量精确 diff、锁路径/无反馈路径审查通过。
- TypeCheck：MSVC 完整 solution 实际编译和链接通过，以原始日志及主会话 exit 回执为据。
- 两源码 UTF-8 严格解码通过，无 bare LF/CR。IdtState 保留 UTF-8 BOM、2468 个 CRLF、EOF newline；inl 保留无 BOM、822 个 CRLF、EOF newline。IdtMain 本增量无修改。
- 未重复全 --no-window/长算法矩阵：没有 Tracker/Capture 算法、codec 或 renderer 变化。先前软件证据保留原版本范围；本次短 CLI 不冒称验证实屏窗口。

### 冻结版本

- `Inkeys/IdtState.cpp` SHA256：`3E7814E507FBC2B1C909ACB3B95A1019A5E1B56B63FBF796D2D98EA9B3443762`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl` SHA256：`6C655BF5449AEF8A401DDC0B5A35854069574BD6C99D0A7C280F847B62956D7A`
- `Build/ARM64/Debug/Inkeys.exe` SHA256：`D6A348E46F78E0FC53F5C39B921EFAB11A7BB4B498DE4CACC03940964FA456ED`

以上 hash 为本轮 checker 读取的磁盘文件，不称进程内存 hash。后续新 build 应记录新版本，不沿用本表。

## Findings (not fixed) 与证据边界

- 本次新增判断的 style→实际 Window ACK 收敛仍需由 GUI owner 执行原 ink 用例，以 A/B after-setter/after-wait 的真实 mode/bridge/ACK、后续真实输入/像素/文件断言和正常清理作为证据。checker 本轮无 GUI 或额外纯 CLI 运行权，故未执行；不是代码已知阻断。
- 未加入对旧 Reconcile 末尾与新 Publish 交错的确定性调度测试。持久事实谓词的并发理由已独立核对，但不能把静态检查写成调度压力测试通过。
- PID29296 的旧失败发生 A Down 前且 cleanup 正常；不能称 RTS 不支持。此前 PID72368 AV 只有 WER 模块/偏移无线程栈，readiness 修复前后的行为不能确定唯一故障指令。此次未改这些历史证据。
- 浏览器/Word/PDF、长滚动/惯性/真实笔触/旧 Windows 及完整主栏视觉矩阵不在此有限检查内；不标父/本任务或完整功能已完成。规范增量由主会话维护。

源码/build/GUI 所有权继续归主会话指定 owner。checker 无待处理源码修复，不占构建或桌面。
