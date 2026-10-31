# 第一批 S1–S4 独立质量复核

日期：2026-09-23。角色：已派发的 trellis-check。范围为当前源码、测试、工程登记及规范差异；未派发其他代理、未运行 GUI、未 commit/push。主代理统一执行完整构建与完整 headless，本文明确区分审查者自行检查和引用的执行结果。

## Findings (fixed)

- File: `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h`
  - Issue: 新的健康汇总开关是编译期常量，主代理首次完整构建在 `EndBatch()` 报 C4127（conditional expression is constant）。
  - Fix: 改为 `if constexpr (HealthySummariesEnabled)` 包含期限判断，保持关闭/开启时原有聚合语义，不抑制 warning。
- File: `InkeysHeadlessTests/render_scheduler_tests.cpp`
  - Issue: 新诊断合同的计时器幂等停止、非空 sink 替换保留 pending 与已安装 sink 的 Stop/restart 缺少直接回归；原有 restart 用例没有安装 sink。
  - Fix: 添加真实 `FrameStageTimer` 的显式 Stop/重复 Stop/析构只计一次、nullptr 安全；用真实 Scheduler 验证拒绝后的聚合由替换 sink 接收，保留失败证据及原 1 秒限频；验证重复 Stop 后 restart 不保留原 sink/TLS，且只有新请求触发客户端。

本次独立复核未发现除此之外必须修改的 S1–S4 产品代码缺陷。

## 审查结果

- S1：diff 只删除 11 个颜色块的 22 处中间描边目标；选中勾号仍由原分支控制。最终 `ft.SetTar(drawAttributeLayoutScale)`、批次同步和一次性显隐/换边重定向保留。动画测试调用真实模块验证比例和最终收敛；生产 RenderLoop 的目标来源另由静态审查确认，不能称为已执行完整布局集成。
- S2：三个私有 helper 返回本次裁剪后的实际 damage 贡献，不借用旧 pending/invalidated 状态。cursor 历史边界更新先于 primary/cursor 结果合并；同区域强度变化、旧光离开仍有 union。全部订阅者仍更新最新快照，仅 affected hooks 进入锁外广播。首次订阅/退订、布局、动画与失败入口的独立请求保留。
- S3：Bar/PageControl 的 DC 在获取到释放期间只作 ULW 源；COPY、原配对/失败路径及四阶段成功事务保持。空 RECT 表示没有 GDI 修改，不以此承诺具体耗时收益。
- S4 采样：TLS 只在当前 callback 的 RAII scope 内有效，异常路径恢复旧指针；独立 Scheduler 有独立 sink、累积与恢复状态。FrameStageTimer 和 mask miss 计时对 nullptr 不采时；逐分片只累计实际调用数。
- S4 统计：合法 Retry 不独立记错误；只有实际推进才累计 used dt。latest、slowest、lastFailure 分开保留；真实 present failure 的恢复要求 commit，PageControl 隐藏 Idle 不构成成功恢复。Scheduler idle、客户端上次 Idle、重新注册都断开活动间隔，raw commit 间隔仍独立可见。
- S4 限频：输出失败/拒绝/异常同样消耗 1 秒额度，pending 不被健康帧清除；idle 到期仅唤醒日志，不添加客户端请求。非空 sink 替换保留聚合；禁用、Stop 清理采样。sink 在内部锁外执行。
- 日志桥复用原 file sink/thread pool，专用 async logger 使用 `discard_new`，不改主 logger 的阻塞策略及格式。当前代码只有该桥使用 discard_new；前后 discard_counter 用于发现队列拒绝。接受入队不等于已落盘，Stop 不等待尚未到期的尾部聚合，与已记录的退出边界一致。
- 调度语义：`DispatchState::Complete` 将原先直接 OR 的 TakeRequested 结果保留到字段再 OR，掩码内容不变。原固定派发、Continue/Retry、设备恢复、pacing 和 stop 路径保持；新控制点仅交接诊断 sink。
- 工程与规范：内部 header 已在主工程、filters 和 headless 登记；未更改 SDK/依赖/构建入口。新诊断合同已单列于 `.trellis/spec/native-desktop/ui3-render-diagnostics.md`，S1/S2/S3 相关窄合同已同步。

## Findings (not fixed)

- 真实 Scene hooks 计数未在 headless 集成执行：当前工程只链接 Scene 接口，没有完整 Scene 实现。遵循任务最小范围，以生产 call chain、已有真实 damage 算法和完整产品构建验证；没有新增公共接口或用复制 bool 的测试冒充集成。后续实机场景还需确认实际通知/呈现次数。
- 低概率严重卡顿仍缺故障用户运行证据。S1 确定动画缺陷及 S2 无效通知路径的修正不能证明全部偶发慢帧消失。D1 时钟/退避、D2 exact 平移、D3 容量与 source 包含保证均为已记录但未批准的后续设计范围，本审查未修改。
- 原始 GUI 视觉、ULW 实机场景及完整日志队列满载磁盘持久化未执行；本轮没有 GUI 授权。异步桥的容量拒绝路径静态审查与 sink 拒绝/异常回归可以验证不改变调度结果，不能代替现场性能测量。

## Verification

审查者自行执行：

- Lint/静态：`git diff --check` 退出 0。
- 编码：16 个已跟踪文本差异均可 UTF-8 解码，BOM 与 HEAD 保持；工作区均为 CRLF。Git 的 index LF/worktree CRLF 由 `core.autocrlf=true` 解释，不能把它误判为本次格式变更。构建生成的 DLL/TLB 排除出文本编码检查。
- 工程 XML：主 vcxproj、filters、headless vcxproj 均成功解析。
- TypeCheck/编译：审查者没有另启 MSBuild；由主代理统一执行，避免并发构建和混合版本。
- Tests：本审查新增测试已落盘；完整运行由主代理执行并通过，见下列最终验证。

主代理已提供的验证：首次完整 `InkeysRepo.sln Debug|ARM64` 使用原生 ARM64 MSBuild，退出 0，耗时 96.879 秒；日志 `Build/TestResults/ui3-stutter-msbuild-20260923.log`。此次在 reviewer 修复 C4127 与补测试之前，不能替代最终状态验证。主代理随后完成最终完整 Solution 增量构建及 `InkeysHeadlessTests.exe --no-window`；最终结果如下。


### 最终验证（主代理执行，审查者读取日志复核）

- TypeCheck/编译：通过。完整 `InkeysRepo.sln Debug|ARM64` 最终增量构建 `MSBUILD_EXIT=0`；日志末尾为 0 Error(s)、耗时 23.00 秒。`Build/TestResults/ui3-stutter-msbuild-final-20260923.log`。
- Tests：通过。使用该次构建的 `InkeysHeadlessTests.exe --no-window`，`HEADLESS_EXIT=0`，末行 `PASS animation correctness`。`Build/TestResults/ui3-stutter-headless-20260923.log`。
- 新增 C4127 已消失；最终日志只余未改动第三方 `additional/hashlib++/hl_hashwrapper.h` 的 3 条既有 C4267，按最小范围未清理第三方。
- 最终 `git diff --check` 仍退出 0。此后没有产品或测试修改；仅补全本审查记录。
