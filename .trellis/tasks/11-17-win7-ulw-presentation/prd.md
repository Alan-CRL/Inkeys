# 第2步 Draw3 透明画布呈现：交换链等待与 ULW 流水线

> 角色：第2步父任务，持有现状调研、任务地图与跨子任务验收；本身不作为实现目标。分支 `refactoring/draw`，与第1步（`refactoring/drawpad-ui`，UI 改动）并行。

## Goal

在不破坏 Win7 SP1 + KB2670838 兼容和画布调试功能的前提下，让 Draw3 透明画布在各呈现后端上都做到“最多排队一帧、CPU 不被 GPU/系统提交拖住”，降低输入→上屏延迟。

## 现状依据

- `research/presentation-pipeline-current.md`：端到端流程图、L0/L1/L2、脏区、ULW 方案对比、DComp CPU/GPU 重叠现状、waitable 与排队控制方式、超时与熔断建议（Claude 读码 + Codex 交叉核对）。
- `research/codex-investigation-20261117.md`：Codex 只读调查原文（file:line 证据）。
- 总任务 `research/第2步 …研究_20261113.md`：旧基准研究备忘（候选方向，非已批方案）。

## 任务地图

| 阶段 | 子任务 | 状态 | 说明 |
| --- | --- | --- | --- |
| A | `11-17-dcomp-waitable-latency` DComp waitable 交换链帧延迟控制 | 已完成并归档（0c8a7337，另有 A2 `11-17-cursor-frame-pacing` 696cc4f1） | 先做；Win8.1+ 正确使用 waitable（令牌/自适应超时/熔断），其余系统保持现状；性能页新增“waitable 模式下启用等待”开关 |
| B | `11-17-ulw-texture-pipeline` ULW 独立纹理、CPU/GPU 重叠与合成时机对齐 | 已完成并归档（2026-11-18，本机强制 ULW 实机认可；Win7 实测并入 D 之后） | 独立纹理作最终目标 + 自适应异步读回（最多领先一帧）+ 提交不超过刷新率 + DWM 合成时机对齐（原 E 的 ULW 部分）+ 单遍 SIMD 复制 + DWM 状态检测 |
| C | `11-17-perf-dashboard` 性能页设备性能看板 | 规划完成待审阅（2026-11-18） | 逐帧累加 → 空闲移交 → 按设置签名×日分桶写 `Inkeys/Memory` JSON → 性能页顶部看板（最近 7 个使用日、四档配色、帧率建议、可展开 6 维度明细） |
| D | （未建）Win7 DWM 呈现路径 | 规划预告 | 实现 Win7 DWM 透明呈现路径（像素留在显存、不回读），DWM 未合成时回退 ULW；复用 B 的 DWM 状态检测与合成时钟模型 |
| E | （未建）DComp 合成时机对齐 | 规划预告（ULW 部分已并入 B） | DComp 帧相位对齐合成时机（Win8+ 用 `IDCompositionDevice::GetFrameStatistics` 的下次合成时间与合成频率），需与 A 的 waitable 等待顺序、超时/熔断计数及动态刷新率协调，单独 A/B；复用 B 的合成时钟模型，阶段 D 也可能使用 |

交接说明见 `handoff.md`（上下文压缩后从这里恢复）。

## 跨子任务约束

- 不以微软文档为由改动 FLIP 配置：用户实测 Win7 SP1 + KB2670838 上 `CreateSwapChainForHwnd` + `FLIP_SEQUENTIAL` 可用。
- 不引入应用侧 GPU 事件查询自旋 / 信号量兼容等待（历史上出现过丢信号问题）；任何等待都有超时与熔断。
- 画布调试 / 可视化调试保持可用：调试背景板、标记、接触表、输入→上屏延迟，以及 ULW 像素诊断、可信快照、无窗口测试。
- 主窗 ULW 命中底层 alpha、辅助 DrawpadPresentation 的按需读回与隐藏回收语义不变。
- Resize、设备丢失恢复、运行期 DComp→ULW 降级、强制呈现模式均可正常工作。
- 新增诊断走统一日志接口，Draw3 热路径不得因日志阻塞。

## Acceptance Criteria

- [ ] A、B、C 三个子任务各自验收通过并归档。
- [ ] 集成后 Win11（DComp 与强制 ULW）书写、橡皮、激光、漫游、画布调试显示正确，无残影。
- [ ] Win7 SP1 + KB2670838 可启动并以 ULW 正常书写（需用户实测）。
