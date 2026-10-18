# 显示物理标尺与笔速橡皮完善

## Goal

为笔速橡皮、手掌橡皮及其他物理距离业务建立可靠、可订阅的显示物理标尺，并在该契约稳定后完善橡皮算法。

## Requirements

- 第一阶段完善活动显示拓扑识别、逐 target EDID 获取、原始 EDID 诊断信息和逐逻辑屏业务可用状态。
- 第二阶段只消费第一阶段发布的缓存物理标尺，不在输入逐点热路径查询 DisplayConfig、SetupAPI 或注册表。
- 复制及部分复制拓扑全局禁用物理标尺；纯扩展拓扑按屏判断，单屏 EDID 失败不连带禁用其他可靠屏。
- 不使用 DPI 反推尺寸并冒充 EDID；物理标尺无效时仍保留像素、DPI、工作区和方向信息。
- 保持现有 Draw3 GPU、文档和输入协议兼容。

## Acceptance Criteria

- [ ] `display-physical-size-validity` 子任务提供可靠的显示物理标尺契约和完整失效原因。
- [ ] `speed-eraser-physical-scale` 子任务仅在第一阶段完成后进入详细规划与实现。
- [ ] 第二阶段能够在接触批次开始时锁定一代物理标尺，逐点处理只读取绘制线程配置。
- [ ] 两个子任务分别完成无窗口测试、完整 ARM64 Debug Solution 构建和任务验收。

## Task Map

- `09-12-display-physical-size-validity`：第一阶段，当前实施目标。
- `09-12-speed-eraser-physical-scale`：第二阶段，依赖第一阶段，目前只保留需求入口。

## Out of Scope

- 父任务本身不直接承载产品代码修改，不单独启动。
- 第一阶段不调整笔速橡皮或手掌橡皮算法参数。
