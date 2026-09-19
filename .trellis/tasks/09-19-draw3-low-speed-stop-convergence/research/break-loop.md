# Bug Analysis: Draw3 停笔把“模型未推进”误判为“模型已收敛”

### 1. Root Cause Category

- **Category**: C - Change Propagation Failure（主因），并伴随 D - Test Coverage Gap 与 E - Implicit Assumption。
- **Specific Cause**: RTS 多 contact 迁移删除了旧鼠标循环每帧发送同坐标 `kMove` 的行为，却保留了依赖该推进的 idle freeze。`Predict` 不改变 modeler state，三帧重复 L0 因而只证明“状态没推进”，不能证明笔尖到位；当下一份真实 Move 才推进模型时，积欠运动集中甩出。
- **Evidence update**: 初始候选包括 shader 停刷、预测参数回归和 freeze 状态机。源码/API 契约、迁移前 Git 行为与真实 modeler 回归测试共同把“缺少 stationary Update”的置信度提升到 95% 以上；shader 假设被排除。

### 2. Why Fixes Failed

1. **原迁移**：只迁移了 sequence 变化时的真实 snapshot 消费，遗漏“动画帧推进模型但不产生速度证据”的另一半合同。
2. **原稳定门槛**：用 L0 连续相同替代模型收敛证据，隐含假设 prediction 会自行推进内部状态。
3. **初始实现范围**：先修复了独立测试宿主；复核 native-desktop 规范后发现产品保留独立且更演进的 Draw3 实现，交付前补齐产品路径与 HardPen。
4. **首轮失败处理**：stationary `Update` 错误会逐帧重试和日志；Trellis check 增加 per-contact 锁存，并补齐对象池新 Down 的重置边界。

### 3. Prevention Mechanisms

| Priority | Mechanism | Specific Action | Status |
|---|---|---|---|
| P0 | Executable spec | 在 `native/runtime-and-rendering.md` 记录 stationary advance、position+velocity 收敛、三帧视觉门槛和失败锁存 | DONE |
| P0 | Regression test | 用真实 modeler 断言 Predict 非推进、同点 Update 收敛、长停不增点、恢复 Move 与 Up 稳定 | DONE |
| P0 | Product integration | 测试宿主与产品两侧同构实现；产品额外覆盖 HardPen | DONE |
| P1 | Review checklist | 修改 packet/frame 驱动边界时同时核对“真实采样证据”和“模型时钟推进”是否都被传播 | DONE（写入场景触发条件） |

### 4. Systematic Expansion

- **Similar Issues**: 任何把高频 packet 合并为 mailbox snapshot 的输入迁移，都可能遗漏“无新 observation 仍需推进的内部状态”；橡皮尺寸动画已有独立 `Advance`，本任务未改变其语义。
- **Design Improvement**: 用 `modelInputThisFrame` 显式区分“本帧模型是否收到输入”，用 `IsModeledTipSettled` 让冻结依赖可测的模型状态；不以跨工程抽象强行合并已经分化的产品/测试宿主。
- **Process Improvement**: 调整测试宿主时必须搜索产品镜像；修改 stable/frozen 判定时必须用真实 state progress 证据，而不是仅看输出连续相等。
- **Knowledge Gap**: `Predict` 是只读推演而非时间步进；这一点现已进入代码规范和回归测试。

### 5. Knowledge Capture

- [x] 更新 `.trellis/spec/native/runtime-and-rendering.md`。
- [x] 保存根因与迁移证据到本任务 `research/root-cause.md`。
- [x] 保存本次分类与预防机制到本文件。
- [x] 增加真实 modeler 回归测试并覆盖产品完整构建/无窗口集成。
- [ ] 可见设备上的真实鼠标、压感笔和 Touch 仍由维护者进行人工验收。

仓库不存在 `src/templates/markdown/spec/` 镜像目录，因此本次没有可同步的模板。根规则禁止在未获明确授权时创建 commit，本次仅保留工作区改动。
