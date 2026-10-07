# E04 UI3 Scheduler 原始采样设计独立审查

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。仅审 `ui3-raw-sampling-design.md`、生产 RenderPipeline/TLS/Diagnostics 与实际 Bar 提交合同；未改产品或运行新测试。

## 结论

**最新修订的首批 Scheduler recorder 设计 GREEN，可进入受限实现。** 原 Configure/Start/Stop ABA 阻断已在设计中补齐 lifecycle epoch、唯一 reservation token、Releasing 释放状态及确定性 R13/R14 竞态用例。此结论只批准 `RenderPipeline.cppm/.cpp`、Diagnostics.h 和 `render_scheduler_tests.cpp` 的首批 recorder/TLS/封口合同；Bar 真实提交时刻、完整主栏 fixture、SVG/path 分段与三轮 Release 采样分别审查。首批的 `commit-callback-end proxy` 不能升级称为真实 API 返回时刻。

生产 `RenderPipeline.cpp:544–616` 目前仅当 `diagnosticsSink` 存在时建立 FrameDiagnostics/TLS；无 sink 时 `CurrentFrameDiagnostics()` 为 null。`Impl::Stop:357–373` join 后才做清理，但 render thread 在 `:654–656` 执行最后 control tasks **之前**已发布 `running=false`。因此仅看 running 就 Take 有未完成回调/控制任务风险。设计的 joined/sealed 状态和 Stop 后 Take 是必要条件。

## 原阻断的修订核对

1. **ABA 已有可实施的线性化合同。** 初次锁中捕获 epoch/token，锁外分配，第二次 try-lock 同时核 epoch、token、running 与 joined；Start→Stop 的完整周期也推进 epoch，旧候选被拒绝。`Start` 原有的锁前 `running.exchange(true)` 使并发 Configure 最终检查失败；若 Configure 已先发布 Prepared，Start 之后在同锁消费。设计明确 Start 不等待 64 MiB 分配、不释放预约 owner 的候选。
2. **容量 0 与释放所有权已明确。** Reserved/Releasing 时容量 0 立即 false；Prepared 移出后发布 Releasing，原 owner 在锁外释放后才清自己的 token。失败候选同样先释放再撤销自己的预约；别的 Configure 不能在这个区间创建第二个候选。R13/R14 覆盖这两个有风险的交错。
3. **报告与下一录制器分开。** `nextRecorder` 的 Empty/Reserved/Prepared/Running/Sealed 与 `sealedReport` 正交；Stop join 后同锁封口，重复 Stop 和无采样新 run 不清旧报告，Take 只在封口后一次转交。render thread 早发的 running=false 不被当成 join 证明。
4. **实现审查仍要核状态与异常路径。** 析构与 Configure 应遵守设计写明的单 owner 生命周期，不得在锁外分配期间销毁 Scheduler；Start/Stop 原 owner 串行。实际源码必须按 R13/R14 验证 `Start` 锁前预标志、Stop 后 epoch、try-lock 失败、分配异常和容量 0 的 token 清理。此为实现检查项，不再是设计阻断。

## 首批可验收范围

默认关闭时 TLS=null、无数组分配/新阶段读钟；opt-in 仅 Configure 一次至多 64 MiB 预分配，写入按固定索引，无热路径扩容/锁/文件/字符串/用户 sink。callback 与 batch 分母分别 `seen=retained+dropped`，溢出报告 truncated；idle transition、合法 Retry、设备恢复无 callback 的批次与失败 API 各独立计数。异常 sink 拒绝或抛错不改变 raw 保留，raw-only 不触发原 `DiagnosticsAccumulator` 格式化/投递。活动间隔按 client generation/device epoch/idle 分段。封口后一次 Take，原始 `steady_clock` 整数 tick 与 clock period 保持精度；callback end 只称成功提交后的**代理时刻**。不得改变原 frameTime、16.67ms pacing、wake/retry、动画/视觉逻辑。

用生产 Scheduler/Diagnostics 的 R01–R14 作为首批门，尤其确定性 `Configure预约→Start→Stop→Configure完成`、`Configure预约→Configure(0)`、Prepared 释放期间第二 Configure、Stop finalTasks hold、重复 Take/Stop、分配失败、容量2溢出与合法 Retry。R12 表中“容量0拒绝”须按状态解释：Empty/Prepared 可按合同禁用未来采样，Reserved/Releasing 返回 false；不能把容量0一概记为非法。测试中的假客户端只证明记录器合同；Bar GetDC/ULW/EndDraw 真成功、动画完成、SVG/光影成本、HC/H2 比较与 Win7 体验仍需后续独立 runner。`RenderPipeline.cppm/.cpp`、Diagnostics.h 与 `render_scheduler_tests.cpp` 的写入者应唯一，root 串行构建完整 Solution。

## Verification

已只读核对 `check.jsonl` 引用、PRD/design/implement、生产 `Start/Stop`、TLS/诊断及最新修订设计。Lint/TypeCheck/Build/Tests：本设计审查未运行；GREEN 仅为可开工设计，不给尚不存在的 recorder 记功能 PASS。
