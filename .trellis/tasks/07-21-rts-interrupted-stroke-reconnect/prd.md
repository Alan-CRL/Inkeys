# RTS 断触墨迹暂留与续接

## Goal

在绘制线程中把物理 Up 后的墨迹短暂保留为可续接状态；若同一硬件与同一工具状态在末端延伸方向上于 80ms 内重新 Down，则复用原预测模型和渲染资源继续同一笔，修复屏幕瞬时断触造成的笔画断裂。未命中时按既有模型 Up、L2 提交和资源回收流程完成。

## Background

- 当前 `DrawingController::Run()` 在读到 Up/Cancelled 后立即向 `StrokeModeler` 发送 `kUp`，同帧把完成几何提交到 L2，随后回收 contact 和 runtime。
- `StrokeModeler` 接受 `kUp` 后结束当前 stroke，不能再用新 Move 延续；因此续接必须发生在模型 Up 之前。
- contact 池最小容量为 32，runtime 预热 16 个；暂留必须有时间和数量上界，且仅剩候选时不能维持 120Hz 空转。
- `retainPredictionOnUp` 只选择最终尾段来源，不承担断触续接开关语义。

## Requirements

### R1. 暂留与终态

- 新功能默认开启，由 `StrokeModelConfiguration` 中单个 bool 控制，后续外部设置可在构造控制器前覆盖；不增加命令行参数或运行时 Set/Get。
- 仅物理 `Up` 可进入暂留；`Cancelled` 继续立即终结且不得续接。
- Up 先以 `kMove` 保留最终坐标和笔状态，保存原始 Up 快照；80ms 超时后才向同一模型发送 `kUp`。
- 暂留期间保留 modeler、真实点、prediction、L0/L1 CPU 状态、提交游标、宽度估算器和 contact handle。

### R2. 匹配策略

- 覆盖 Touch、Pen、MouseLeft、MouseRight，以及普通笔、荧光笔、橡皮。
- 新旧 `InputDeviceType`、批次选择工具、有效工具、宽度模式、倒转状态和压力屏蔽策略必须一致。
- 间隔不超过 80ms；物理 Up 作为 `kMove` 后立即冻结 prediction，按新 Down 间隔选择预测速度方向；超出预测时域使用最后预测状态。
- 桥接方向与预测方向夹角不超过 35°；prediction 为空、无有效速度或工具禁用 prediction 时，回退 96 DPI 下回看 12px、至少 4px 的真实尾方向和滤波原始末速。
- 预测有效时以 `预测速度 × 间隔` 形成 `[0.35, 2.75]` 距离包络，增加 4px DPI 余量，96 DPI 下仅保留 64px 紧急硬上限；回退路径维持原 32px 保守上限。
- 多个候选同时匹配时，依次选择实际/预测距离比例更接近 1、角度更小、距离更短、Up 时间更新者。

### R3. 续接、资源与调度

- 匹配成功时回收旧 handle，把新 handle 绑定到原 runtime，并把新 Down 作为连续时间的 `kMove` 输入原模型；禁止 Reset 原模型。
- 未匹配的新 Down 正常初始化新 runtime，旧候选继续等到超时。
- 最多保留 8 个候选；第 9 个进入前立即终结最旧候选。
- 超时或容量淘汰走既有批量完成路径：模型 Up、一次 L2 resolve、活动层重建、指标提交、handle 回收和 runtime Reset。
- 仅剩暂留候选时停止 1ms timer period 和 120Hz 帧循环，等待输入/控制 wake 或最近截止时间。
- resize 从 CPU 状态重建候选几何；clear 延续现有延迟到活动集合为空的语义，新增等待最多 80ms。

### R4. 诊断与兼容

- 启动打印开关、窗口、角度、速度比、距离上限和候选上限。
- 每次成功续接打印设备、工具、运动来源、间隔、实际/预测距离、参考/原始速度、角度和速度比；正常模式不逐候选打印拒绝原因，人工测试模式只打印一次最接近候选的拒绝诊断。
- 保持 Windows 7 SP1 + KB2670838、ARM64/x64/x86、L0/L1/L2、指标和 `retainPredictionOnUp` 既有契约。
- 只修改本功能必要的 `draw3`、测试和 Trellis 文档；保持源码 UTF-8 BOM + CRLF，且不修改未跟踪 `Vcpkg/`。

## Acceptance Criteria

- [ ] 80ms 内满足设备、工具、方向、距离和速度条件的新 Down 复用原 modeler，并生成连续桥接段。
- [ ] 超时、Cancelled、状态不一致或任一运动学条件失败时不续接。
- [ ] 普通笔、荧光笔、橡皮以及四类 `InputDeviceType` 均使用同一判定契约。
- [ ] 暂留候选不超过 8 个；持续快速 Up 不导致 runtime/contact 无界堆积。
- [ ] 仅有候选时绘制线程不按目标帧率空转，最近候选到期后能自行唤醒清理。
- [ ] 多候选选择确定，Up 与新 Down 同帧到达也可匹配。
- [ ] resize、clear、多个完成 contact 的批量 L2 提交和 Present 行为保持正确。
- [ ] 控制台启动提示和成功续接提示包含约定字段，拒绝路径不刷屏。
- [ ] 自动测试覆盖 prediction 时域选择、曲线方向、预测/回退距离边界、兼容状态、模型连续生命周期和候选选择。
- [ ] ARM64 Debug/Release 完整解决方案构建、ARM64 测试与 Release 严格运行指标通过。

## Out of Scope

- 不新增命令行选项、设置 UI、运行时 setter/getter 或持久化墨迹格式。
- 不修改第三方 `additional/` 中的 stroke modeler 实现。
- 不让 prediction 进入正式持久化记录。
