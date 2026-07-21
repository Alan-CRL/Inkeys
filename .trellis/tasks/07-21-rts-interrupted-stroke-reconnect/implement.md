# RTS 断触墨迹暂留与续接实施计划

## Implementation

- [x] 在 `draw3.ink_prediction` 增加配置 bool、纯匹配输入/结果和 DPI 感知判定函数。
- [x] 扩展 `RuntimeStroke` 暂留状态，并抽取模型快照输入、最终 Up、runtime 清理等可复用步骤。
- [x] 调整绘制循环顺序，先消费旧快照、再匹配新 Down，并实现模型/handle/指标状态同步。
- [x] 实现 8 候选上限、最旧淘汰、80ms deadline 和仅候选时的定时等待。
- [x] 保持 L0/L1/L2、resize、clear、批量完成和 `retainPredictionOnUp` 语义。
- [x] 增加启动与成功续接控制台日志。
- [x] 增加运动学边界、兼容状态、候选选择和模型连续生命周期测试。
- [x] 将真实尾方向/末速主判定升级为预测方向与预测速度距离包络；prediction 不可用时保留原 32px 回退路径。
- [x] 在候选创建时冻结 prediction，复用到同帧匹配和候选首帧渲染，并增加人工测试拒绝诊断。

## Validation

- [x] 运行 ARM64 `Debug|ARM64` 完整解决方案构建，超时至少 5 分钟。
- [x] 运行 ARM64 Debug 测试程序。
- [x] 运行 ARM64 `Release|ARM64` 完整解决方案构建和测试。
- [x] 运行 Release 严格运行指标，确认等待/帧率/landing 未回退。
- [ ] 人工验证 Pen、Touch、Mouse 的普通笔/荧光笔/橡皮续接、拒绝、超时、resize 和 clear。
- [x] 执行 `git diff --check`、编码/CRLF 检查和改动范围审查；确认 `Vcpkg/` 未纳入。

## Manual test aid

- `kInterruptedStrokeReconnectManualTestModeEnabled` 当前临时设为 `true`。
- 开启时禁用笔尾倒转橡皮，并在 L1 上用绿色细线覆盖模型生成的续接桥接段，方便普通笔、荧光笔和橡皮人工观察。
- [ ] 人工测试完成后将该开关改为 `false` 并重新构建；关闭时编译期移除额外绘制调用，恢复原笔尾橡皮行为。

## Validation record

- 2026-07-21：ARM64 Debug/Release 全解决方案构建及两套控制台测试通过；警告仅来自既有第三方头文件。
- 2026-07-21：首次 strict benchmark 无可见墨迹窗口且未收到任何输入（Down/Move/Terminal 均为 0），报告因无样本失败，记为无效运行，等待用户确认后重新进行可见窗口测试。
- 2026-07-21：用户确认可见窗口后重跑 strict benchmark 通过；252 次 Down/Terminal/Recycled 全部闭合，landing p99 2.7245ms，活动帧 p99 8.5476ms，长帧比例 0%，空闲 frame/Present 增量 0/0，最终 occupiedSlots 为 0。
- 2026-07-21：加入临时人工测试开关后再次完成 ARM64 Debug/Release 全解决方案构建和两套控制台测试；未启动或操作绘图窗口。
- 2026-07-21：预测方向/速度包络调整后再次完成 ARM64 Debug/Release 全解决方案构建和两套控制台测试；仅有既有第三方警告，未启动绘图窗口，Release strict 动态指标等待人工测试确认后再运行。

## Risk and rollback

- 最大风险是同帧 Up/Down 顺序、暂留 handle 回收和共享 L1/L0 重建不一致。
- 第二风险是候选等待仍维持活动帧或错过 deadline wake。
- 功能 bool 为直接回滚点；关闭时必须完全保持当前立即 Up 路径。
