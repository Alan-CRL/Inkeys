# Host 与输入首批实现复审

## 范围与结论

只读复审本任务已批准设计下的 Draw3.Host.h/.cpp、Draw3.Product.h/.cpp、Draw3.DrawingController.cpp 及既有 control-fence CLI 接线。未审尚在实施中的 IdtState/UI；Window/BG 另行复审。

初轮确认一个跨 Host 生命周期的 fence ABA 缺口，已由 Host 实现者补充 expectedHostGeneration 并重新冻结，本复审者已定向复核。当前五文件未发现剩余阻断问题；仍需主会话统一编译/CLI，以及最终协调接线集成复核。

## Findings (fixed)

### H1：跨 run 的旧 true/false 发布没有资源代际约束

初稿 ResetRuntimeDiagnostics/CloseInputAdmission 清空 workspaceTransitionEpoch，而 setter 只比较业务 epoch。新 run 可重新接纳相同数值，旧 run 的结束可能清掉新 fence，旧开始也可能关闭新稳定场景。该方法层身份缺口不作为此前现场故障根因推断。

实现者现将 Product/Host setter 改为必需的三参数：
SetProductWorkspaceTransitionPending(epoch, pending, expectedHostGeneration)。

Host.cpp:2793–2815 在既有 presentationTargetMutex 内核验当前 acceptedStrokes.HostGeneration，再执行原 epoch/stop 门。没有增加第二个业务 epoch、线程或新计数器。Product 包装仍在 productCallMutex 内调用。

Host.cpp:3005–3019 的生产 probe 新增 Reset 后同 numeric epoch 的旧 Begin/End 拒绝、新代自己清门，以及旧更大 epoch 不能覆盖新 run 的检查。:2970–2988 另覆盖稳定最小化保留同一准入原因、恢复新奇数 epoch 以及旧最小化 false 不能开门。协调调用者必须带开始时捕获的 Host generation，最终集成由主会话继续核对。

本复审者没有直接修改源码。

## 已核对的合同

### 1. 真实输入与快捷键 OR 门

- workspaceTransitionPending 由 presentationTargetMutex 保护，RefreshPresentationInputGateLocked 仅增加原因位 1024，并与 stop、commandScenePending、消费者 fatal、load、恢复、PPT CPU/UI/暂停门作 OR。
- matching false 只撤销自身原因；不清 ConsumerFatal、shortcut gate、加载或 owner recovery。旧 epoch、超前未开始的 false 和同 run 已退休 epoch 的迟到 true 被拒绝。
- ShortcutBaselineBlockedLocked 同样拒绝 pending/stop/fatal/recovery 等不可用场景。旧 lease 仍可 Release，关闭 base 原因不会丢失独立 shortcut 原因。
- setter 发出原 input ControlWake 和 runtime revision，不屏蔽控制、准备、Frame/UI 观察或真实 Move/Up/Cancel 终态。

### 2. 锁序与实际工具凭据

- Product 新包装使用 productCallMutex→Host presentationTargetMutex；Host 内 Bridge 快照和既有 target/shortcut/accepted-stroke 锁使用方式保持原序，没有增加协调锁或窗口同步等待。
- appliedProductRevision/appliedTool 只在 PumpBridgeState 真正执行 WindowController selection、tool、eraser、style 及场景派发之后，在 target 锁下发布；单纯 Bridge.Publish 不产生该 ACK。
- RuntimeSnapshot 在 target 锁下读取新 ACK 与现有 Host generation；selectionMode 继续使用原原子发布。新字段未发现无保护的跨线程读写。
- 新 ACK 仅表示 owner 已应用偏好，不等于 CPU slot/Frame/Native 已完成。协调者仍须核对相同 Host/工具/输出/内容身份及最终 current guard，不能只比较 revision 或工具枚举。

### 3. 实际 Selection 不初始化新笔迹

- DrawingController.cpp 的 DiscardSelectionDown 调用现有 DiscardUntilTerminal；Run 的真实新 Down 路径在 ContactAdmitted 检查后使用它，已取消原仅 Whiteboard 的条件。
- helper 与测试共用，不是测试复制拒绝算法。所有 workspace 都走同一个无 workspace 分支的实际 Selection 判断。
- 选择态 Down 的 route 保留至真实终态；切回 Pen 不补画被拒的 Move/Down。新 physical generation 可正常接纳。
- 修改只作用于新 Down；既有 contact 处理、非 Selection 右键/笔尾临时擦除、普通 Move/Up 的原生产代码未改。测试包含 RTS 对应 MouseLeft/Right、Touch、Pen 和 inverted metadata；Win32 fallback 原 producer 侧 Selection 拒绝仍保留。

### 4. 无窗口 probe 的接线与生命周期

- IdtMain.cpp:1571–1573 的既有 --draw3-control-fence-test 在产品 GUI 初始化前调用 RunDraw3ControlFenceProductionProbe；后者实际调用新增 Host probe 和共用 Selection helper。
- Host probe 构造真实 Host/Impl/ContactInputCoordinator，手动建立有限运行前提，但没有 Start、Attach、绘制 owner 线程或 HWND 创建。
- RestoreStopped 局部 guard 在 Host 析构前恢复 running=false；因此 Host.Stop 走未附着/无 owner 的清理分支。默认 WindowController 析构只 Detach，未附着时不操作 HWND。
- probe 的 PublishState、PumpBridgeState、setter、ShortcutContext/Lease、ControlWake、Coordinator Down/Move/Up/Discard 均使用实际生产方法。
- Selection probe 实际通过 DrainIngressBatch 和生产拒绝操作，验证 route/终态/新 generation，不是运行完整 DrawingController::Run。

## Findings (not fixed) / 验证边界

没有当前五文件中的剩余确定阻断。以下不声称已被本次动态验证：

- Host probe 通过生产 ObserveWorkspace/ObserveRecovery 模拟通知；没有真正 GPU Present、RTS COM 设备或窗口显隐。不能把该测试说成完整 Frame/Service/USER32 集成已经执行。
- 最终 IdtState 必须在目标发布前成功建立同 Host generation 的 fence，完成或受保护安全退休后再按匹配身份撤销；稳定最小化继续保留关闭原因。该调用者尚不在本阶段冻结范围。
- appliedProductRevision 不能代替请求版本、真实 Frame/UI/Native 与 Host 生命周期；工具可能被后续发布更新，最终提交仍需 current 身份复核。
- 后续设备、RTS、临时擦除和各 Windows 系统的实际行为不由这些纯 CPU 接缝证明。

## Verification

- 五文件 git diff --check 通过；后续代际补丁再次检查通过。
- 初轮确认五文件均 CRLF、无裸 LF/CR；Controller 保留 EF-BB-BF，其他四文件保持原无 BOM 格式。
- 本复审者未编译、运行 CLI/测试、GUI、窗口/进程/PDB 采样或提交；主会话统一验证。
- 只新增本报告，没有改产品、测试、spec 或其他任务资料。
