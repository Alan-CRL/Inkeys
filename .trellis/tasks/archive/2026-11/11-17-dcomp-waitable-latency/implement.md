# 第2步-A 实施计划

> 分工：Claude 编写全部实现；Codex（gpt-6.1-sol / high / fast，只读）在完成后审阅；Claude 先独立审阅，再交 Codex 审阅，最后修复。不并行写入。

## 步骤

1. **`Draw3.FrameLatencyGate.h`（新增）**：令牌 / 超时 / 熔断状态机，等待函数注入；UTF-8 BOM + CRLF。
2. **无窗口单测**：`InkeysHeadlessTests` 新增 `draw3_frame_latency_gate_tests.cpp`（或并入现有 draw3 测试文件），覆盖：首次获取、已持有不重复等待、空帧保留令牌、`Present1` 失败/未调用不消耗、超时仍可继续、GPU 慢（信号间隔 80 ms 等）自适应超时后等待成功且不熔断、信号丢失（≥8 次且 ≥1000 ms）熔断、不可计数状态不熔断、`Reset` 不清熔断、超时取值公式（含 EMA 与上下限）。登记到 `.vcxproj` / `.filters`。
3. **`TransparentPresentation`**：选项字段、waitable 允许条件、取句柄、`CloseHandle` 生命周期、`PrepareFrameLatency`、`PresentSwapChain` 内的补等与令牌消耗、熔断走恢复重建、刷新间隔获取、Debug 诊断汇总。
4. **`DrawingController`**：在 C:21598 附近调用 `PrepareFrameLatency`，传入超时与“是否可计数”；运行指标记录 `latencyWaitMs`；画布调试无限制帧率/负载与 `RecordActiveFrame` 的工作耗时扣除等待耗时（第2步-C 看板口径前提）。
   - 可选（改动面小时才做，否则留给第2步-C）：为后续看板/探针预留按工具维度标记的 POD 帧记录字段；不做 JSON、不做分析。
5. **Host / IdtMain / 配置 / 设置 / i18n**：`EnableWaitableWait` 配置项、启动快照与透传、设置 ToggleRow、zh-CN 文案，`pwsh 7` 运行 `Scripts/i18n.ps1 sync` 后补译其余语言。
6. **构建**：`InkeysRepo.sln` `Debug|ARM64`（ARM64 原生 MSBuild，`-t:Inkeys;InkeysHeadlessTests`，超时 ≥ 5 分钟，PowerShell 内规避 PATH 重复键）。
7. **验证**：新增单测 + 相关既有测试（draw3 bridge/contact、config metadata、i18n 一致性）；`dumpbin /imports` 核对未新增 Win8+ 静态导入；`git diff` 检查编码与 EOL。
8. **审阅**：Claude 独立审阅 → Codex 只读审阅（附 diff 文件、PRD/design、已接受偏差清单）→ 修复 → 复验构建与测试。
9. **交付**：向用户报告；实机 A/B（60Hz / 120Hz、开关开/关、4K 大橡皮、漫游、隐藏/最小化/切换工作区）由用户执行，Debug 日志与运行指标用于对比。未经授权不提交。

## 回滚点

- 步骤 1–2 独立可回退；步骤 3–5 依赖步骤 1；开关关闭即回到现状（带 waitable 标志、不等待）。
