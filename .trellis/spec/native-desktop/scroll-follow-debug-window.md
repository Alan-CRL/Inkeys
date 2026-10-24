# Scroll Follow 进程内只读诊断

## 1. 范围与触发

`--scroll-follow-debug-window` 仅在正常进程启动成功后、StateMonitoring 与 MagnifierThread/Pump 开始前打开一次普通 Win32/GDI 窗口；与 `--scroll-follow-console` / `--scroll-follow-verbose` 独立。关闭后本进程不自动重开，不注册快捷键，不持久化普通用户配置，不改变 ViewAdjust、画布、定格或跟随状态。公开的 Open 仅供启动与现有合同/专用夹具使用。

新增 `WindowRole::ScrollFollowDebug` 经 Window Service 的设置 owner 线程和命令队列创建/显示/销毁。使用独立的线程路由判断，不扩大 Setting 样式和业务规则；可调整的 TOPMOST 窗口、owner=null，不进入画布 owner 链。WM_CLOSE 销毁角色，不发送进程退出消息；退出统一隐藏/销毁，释放定时器、字体、GDI 和诊断 Channel。

`BuildCurrentFilterList()` 在每次 Source 更新前查询当前有效角色 HWND，加入此角色并保留所有旧排除角色；另读取 `UI::ScrollFollowDebug::ExportPickerHandle() noexcept -> HWND` 返回的当前导出选择器 HWND，沿原 IsWindow/当前进程/非 child/去重校验。选择器由本次导出 STA 通过 IOleWindow 观察，在 Show 返回/取消/清理时归零，Mag 不访问 COM。不缓存过滤列表或旧句柄，不用捕获亲和属性。列表合同通过不等于 Mag 实际像素排除通过，必须用可见窗口覆盖专用内容并核对正式 Mag 返回的底层像素。

## 2. 签名与 Tracker 数值合同

`Initialize/Process(MotionFrame, TrackerDebugSnapshot* = nullptr)` 保留旧调用和 MotionResult 语义。公开诊断类型仅含标准库/数值；320 个参考点、320 个补点、48 格和有限检查槽，记录操作不抛异常。关闭时不创建大型快照，也不新增 OpenCV 调用、拟合或补跑提前返回后未执行的阶段。

关键签名（完整字段以对应纯数值头为准）：

```cpp
CV::MotionResult CV::VisualMotionTracker::Initialize(const CV::MotionFrame&, CV::TrackerDebugSnapshot* = nullptr) noexcept;
CV::MotionResult CV::VisualMotionTracker::Process(const CV::MotionFrame&, CV::TrackerDebugSnapshot* = nullptr) noexcept;
Debug::ChannelPtr Debug::Activate() noexcept;
Debug::WriteLease Debug::Channel::BeginFrame(const Debug::FrameMetadata&, const CV::MotionImageView&) noexcept;
bool Debug::WriteLease::Publish(bool targetPublished, double totalTrackerMs) noexcept;
void Debug::Channel::RecordMotion(const Debug::FrameMetadata&, const CV::MotionResult&, double totalTrackerMs) noexcept;
bool Debug::Channel::ConsumeLatest() noexcept;
void Debug::Deactivate(const Debug::ChannelPtr& expected) noexcept;
```

所有诊断取自原生产检查处，保留判断顺序和短路。`checks.condition` 是原表达式真值，不能统一解释为失败；`firstFailure` 才指向实际拒绝门。联合条件保留 AND/OR 含义，未执行值明确未执行。异常来源、reached/completed 阶段和每点有效位不可丢失。

- `referenceSequenceBefore` 是本次真实匹配的旧可信参考；接受后的 `result.statistics.referenceSequence` 已是新参考。
- reference/forward/refined/backward、补点、flow 和支持范围使用处理图像像素。半尺寸为各轴向下取整，除以实际 scaleX/scaleY 还原 ROI 原始像素，不能固定乘 2 或再乘 DPI。
- anchor、delta、total 已是输入/内容原始像素，不能再次缩放。
- 8×6 拟合证据按参考点所在格；保留/补点按当前点所在格，显示必须注明。
- mainMotion、strongestCompetitor、eligibleRunnerUp、rejectingCompetitor 来自生产实际评估的候选；最强竞争候选不必等于触发拒绝的候选。不得为展示额外拟合或修改 Tracker。
- 候选估计、精确返回 MotionResult、Target 发布是否成功、owner 实际应用序号/Viewport 必须分开；拒绝的 result.delta=0、total 保持最后可信值。

## 3. 跨层载荷与线程所有权

Business 只在真实 Initialize/Process 前取得当前 Channel。Borrowed BGRA 在原 Mag 回调内逐行复制实际 WorkArea ROI，不能保留 Capture::Frame 指针，也不另调 Mag/PrintWindow。源区、ROI、处理尺寸、stride、实际比例、windowGeneration/businessSession/captureSession/sequence/displayGeneration 随同一包发布。

Channel 只在窗口开启时分配。唯一 Mag producer 与唯一 UI consumer 通过三槽 latest-only 交接自有 BGRA 和同帧快照；复用 capacity，消费落后可覆盖旧预览。生产者不等待绘图、格式化、目录选择或磁盘 I/O。30 条固定数值摘要使用独立短锁；生产者 try_lock，争用时丢摘要并计数。UI 不持该锁绘图或导出。

Freeze 不消费新 front，固定图像/标记/该帧统计，后台在另外两槽继续发布；解除后直接消费最新。无 CV 的 AwaitTracking、捕获失败和状态更新不生成虚假拒绝帧，不覆盖最后真实失败图像。实时状态另行标明，不能把 Frozen 或陈旧图像标成当前新采样。

关闭封闭当前代次，迟到 lease 不得发布到新代次。Context 不永久保留 Channel，UI/全局/在途回调共享寿命；关闭后最后回调释放即回收大缓冲。每个 Context 仅用小 atomic 保存首个业务暂停原因，后续 Stop(Requested)/CaptureUnavailable 不覆盖真正首因。 捕获质量/API 等实际暂停入口在 Stop 前记录原 Capture snapshot 的数值故障事件（stage/reason/time/quality/nativeErrorValid/code）；随后 End 失败或 Requested 只更新 live 的最近捕获状态，不得抹掉暂停时的原始证据。

### 捕获观测与性能解释

Capture 只追加只读 Read 开始/完成、最近 accepted sample、实际 Read 完成间隔、失败时间/阶段/原生错误及 qualityObserved；qualityObserved 只在实际 Analyze 后有效，失败 Read/Update 的默认 Quality 不能解释为已分析的 InvalidBuffer；调度、质量判定和停止决策保持不变。原生错误仅在实际失败 API 返回后立即取得，并且该 API 文档明确保证可用 GetLastError 时才设 valid=true；PrintWindow/GdiFlush/RedrawWindow 等未保证设置 last-error 的 API，即使立即采样也应 valid=false，避免线程残留错误。PrintWindow 失败后原有 GdiFlush 仍照常执行，不能因诊断改变调用顺序。

`Frame.timestamp`/lastReadCompleted 是后端 Read 返回时间，随后才做 Analyze，不是 compositor 新内容保证。现有 lastReadDuration 包括完整 Backend Read（几何校验、PrintWindow、GdiFlush 和 alpha 修复），不含 Analyze，标签为 Capture Read。Capture Accepted、Tracker Accepted、实际采样 FPS、预览刷新率分别报告；UI 消费速度不能冒充捕获 FPS。totalTrackerMs 仅在启用时围绕原 CV 调用测量，不含复制/Target 发布/绘制，复制和显示年龄另列。

### 窗口与导出

图像保持比例，点/箭头/网格/范围共用同一个映射。点、光流、网格、支持范围开关及 1×/2×/4× 箭头倍率只影响显示。绿色主内点、灰色近静止、橙色竞争、红色失败/排除、青色补点；形状与有效位区分 Forward-only 等阶段。范围名为 Main Motion Support Bounds / Candidate Motion Region，不能宣称识别了滚动客户区。

Save Debug Snapshot 在点击时克隆当前 packet、显示选项和数值摘要，选择目录后独立任务写 raw PNG、标记 PNG 和 JSON；默认不保存图像，最多一个导出任务，不 pin front/Channel，不实现录制。取消、异常和窗口关闭不能使导出任务或目录对话框永久阻塞退出。JSON 保留原始检查值、比较、门限、执行有效位、身份和生产结果；未执行不伪装成零失败。

## 4. 校验与错误矩阵

| 条件 | 诊断行为 / 生产边界 |
| --- | --- |
| 无启动参数或关闭后 | Enabled=false；无 Channel 大缓冲、预览定时器或图像复制 |
| BGRA layout/stride/size 不合法，或预览分配失败 | 空 WriteLease、frameDrops；原 Tracker 仍按原输入运行，诊断不得制造异常拒绝 |
| 关闭与在途 Publish 相遇 | closed 位使迟到 Publish=false；新 windowGeneration 不接收旧包 |
| 检查 evaluated=false | 显示未执行；不以 measured 默认 0 判失败 |
| 没有实际 CV（AwaitTracking/状态/捕获失败） | hasMotion=false，不增加 trackerRejected，不替换失败图像 |
| 失败 API 没有可靠错误码 | nativeErrorValid=false，保留真实 failureStage/time |
| 导出忙/取消/写入失败 | 仅改变导出状态；不改变 Capture、Tracker、Viewport |
| 短入口多参/畸形参数 | exit2、无输出，不启动正常产品 |
| 实屏安全界面门命中 | 原入口 exit4/blocked，初始化前停止；必须记未验证 |

## 5. Good / Base / Bad 用例

- Good：真实 Process #11 使用参考 #10，接受后 referenceSequenceAfter=#11；同一 packet 的 BGRA、points、result 和身份均为 #11。ownerAppliedSequence 可能稍后才到 #11，不能提前显示为已应用。
- Base：冻结 #11 后实际采样推进到 #20，仍显示 #11 图像与其统计；live 状态/最近摘要另行推进，恢复直接取最新完整包。
- Bad：#12/#13/#14 连续真实拒绝，#14 进入 Paused；保留 #14 的失败输入、firstFailure、最后可信 total，后续 Stop(Requested) 不覆盖首因，不产生假的 #15 Tracker 拒绝。

## 6. 必需测试与断言边界

- 无窗口短入口 `InkeysHeadlessTests.exe --scroll-follow-debug-only`：诊断 on/off 非计时结果差分、提前返回、特征接力、三槽寿命/Freeze/关闭/状态及导出写入。严格单参，额外/畸形参数退出 2；不得顺带运行完整长 CV 或 UInk 保存矩阵。
- 屏幕外 `--exit-visibility-gate-test` 复用窗口合同，验证角色创建销毁、线程、句柄、统一退出和资源。
- 产品严格单参 `--scroll-follow-debug-exclusion-test` / `--scroll-follow-debug-product-test` 复用现有正式 Mag/产品夹具；专用输出目录和安全门不可绕过。真实 covered pixel、原 ACK/Off/drain、可见性和资源收尾不能由合成/句柄验证代替。 产品性能入口为无交互三阶段，各15秒；先观察absent，真实Off/反向ACK/drain后Open，再实际toggle/F0/ACK得到新会话，open/closed保持这个新会话。absent与后两段不同会话，不作性能因果推断；控件另走正常启动的人工验证，不在该短入口中等待。
- 构建实际产品遵根 AGENTS：完整 InkeysRepo.sln、Debug|ARM64、vswhere 定位 ARM64 原生 MSBuild，同 PowerShell 先规范 Path 并禁节点复用，至少五分钟预算。沙箱扫描失败但同命令宿主成功时记录环境边界，不改工具链/依赖来绕过。
- 实际结果、有限性能观察范围和未验证项以本轮任务 validation.md/交付记录为准；安全桌面拦截不得记 PASS。无窗口导出测试不代表目录选择或 GUI 冒烟已通过。旧 Windows/Word/硬件笔也不能由 ARM64 专用夹具外推。

## 7. Wrong vs Correct

```cpp
// Wrong：把原表达式真值当通用通过标志，或把未执行的默认 0 当实际失败。
if (!check.condition) ShowRejected(check.measured);
// Correct：展示原比较，拒绝归因只取生产 firstFailure；联合门仍注明 AND/OR。
if (!check.evaluated) ShowNotExecuted();
else ShowComparison(check.measured, check.comparison, check.threshold, check.condition);
ShowFirstFailure(snapshot.firstFailure);
```

同样，不能把 `point * 2`、`result.total / scale`、捕获成功计数或 PrintWindow 返回时间分别冒充正确坐标、实际应用运动、Tracker 成功或 compositor 新鲜度。

中途动态Open不是正式路径：旧活动捕获夹具有一次覆盖像素失败（seq113/max_error225），后续范围修正不能追溯证明其根因或改判。任何未来增加重开/中途开窗的需求都需要单独评估在途Read和首次可见排除；导出选择器的首次可见像素验证也独立于主窗启动验证。

## 2026-10-23 局部身份、完整输入录制与同序号呈现

本节更新旧“三次拒绝”和“不实现录制”的历史描述，具体算法合同见 scroll-follow-tracking.md 的局部目标版本。网格只辅助观察，不再表示全屏可靠性门槛。显示目标身份、发现/锁定/恢复阶段、支持范围、真实模板峰/细格反证、成熟目标点与待认证点；粗峰 unique 必须是真实计数，细格反证补充的唯一性用独立 identityDisambiguated 标记。静态背景、无运动假设、候选被拒绝、捕获失败、已发布但未应用/呈现分别解释。

录制默认关闭，只有显式 Arm Recording 后从下一个真实 Initialize 开始，记录每个 Initialize/AwaitTracking/Process 输入。原尺寸无损紧凑 Gray8、时间戳、原输入格式/stride、会话/序号/几何和非计时结果必须配对；录制独立于预览 lease 和 UI 消费。10秒、64帧、256MiB任一上限达到即封存完整前缀；序号断裂、缺结果、身份变化或内存失败标 Failed，不悄悄拼接。只有封存的不可变数据可导出，生产回调不写磁盘。BGRA→Gray8 与生产转换使用同一规则，并要求真实非零 Accepted 的逐帧回放结果一致。

严格入口 --visual-motion-replay <manifest> 保留历史精确重放，并对新 schema Inkeys.ScrollFollowRecording.v1 的全部非运行耗时统计进行审计；独立 --gt <truth> 使用 Inkeys.ScrollFollowGroundTruth.v1 的绝对采样位置。GT 报告全部输入误差/最长未更新、正确更新率与错误提交，不能仅统计 Accepted。没有有效样本、错误格式/顺序、尺寸/限额不符必须失败。旧算法与新算法的字段差异不能冒充 exact audit PASS。

Draw3 Target 携带采样 Read 完成和 Publish 时刻，Applied 必须从实际消费的 Pending 副本取同序号元数据，不能混入并发发布的 latest。Present 返回时立即采样成功时间，再执行回调；ObservePresent 仅关联实际已应用序号且保留其首次成功 Present。UI 的 applied/presented 各有独立序号。Read→Present return、预览图年龄均不等于 compositor 可见滞后；20Hz/80ms目标必须另用专用源时间及可见像素证明。
