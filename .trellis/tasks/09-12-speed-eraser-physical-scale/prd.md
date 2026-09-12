# 笔速橡皮物理尺度完善

## Goal

在第一阶段可靠显示物理标尺基础上，将现有 DIP 笔速橡皮演进为可使用真实物理距离的实现，并为手掌橡皮建立同一消费边界。

## Requirements

- 依赖 `09-12-display-physical-size-validity` 完成，不得在依赖接口稳定前启动实现。
- Draw3 Host 在显示快照通知或窗口换屏时解析当前 `HMONITOR`，向绘制线程低频发布小型物理标尺配置。
- 接触批次开始时锁定同一 generation 的配置；逐点路径不得查询 DisplayConfig、SetupAPI、注册表或重新取得显示快照。
- 仅在 `available=true` 时使用横纵 `pixel/cm` 换算真实运动距离，不得用 DPI 推算并冒充 EDID。
- 以当前已实现的笔速橡皮与 OC 平滑为行为基线，具体物理阈值、不可用降级和手掌橡皮策略在本子任务进入规划时另行确认。

## Acceptance Criteria

- [ ] 显示变更后新接触批次使用新标尺，活动批次不混用不同 generation。
- [ ] 输入逐点热路径只读取已发布配置，不产生硬件查询或动态分配。
- [ ] 可用 EDID 下不同 DPI/分辨率的等物理轨迹得到一致行为。
- [ ] 不可用状态不会被 DPI 反推值伪装为有效物理尺寸。

## Deferred Decisions

- 物理速度到橡皮直径的产品曲线。
- 物理标尺不可用时沿用 DIP、固定宽度或禁用动态橡皮的策略。
- 手掌橡皮的接触面积阈值和设备差异策略。

## Out of Scope for Current Stage

- 当前显示物理尺寸子任务不修改本任务所涉及的任何 Draw3 源码或测试。
