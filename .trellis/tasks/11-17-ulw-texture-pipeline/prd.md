# 第2步-B ULW 独立纹理、CPU/GPU 重叠与合成时机对齐

> 状态：规划完成（2026-11-18），待用户审阅批准；批准后用户会先压缩会话再开工。父任务 `11-17-win7-ulw-presentation`；分支 `refactoring/draw`。设计见 `design.md`，步骤见 `implement.md`。

## Goal

ULW 主画布（Win7 / DComp 不可用或失败 / 强制 ULW）不再使用交换链，Draw3 直接画到独立 `ID3D11Texture2D`；GPU 读回与 ULW 提交改为最多领先一帧的自适应流水线，做到“CPU 计算时 GPU 在画，GPU 在画时 CPU 在算/提交”；提交不超过显示刷新率，并在 DWM 合成时对齐合成时机降低延迟；逐行复制改为单遍 SIMD。GPU 快时延迟不变差。

## 已确认决策（2026-11-18，详见 design §1）

1. 独立纹理 + Staging 读回到 DIB；不采用 GDI 兼容纹理 `GetDC`。
2. 读回完成自适应：GPU 跟得上时本帧完成（即时模式，同现状）；跟不上时转为下一帧完成（流水模式，最多领先一帧）。
3. 性能页“渲染与兼容”新增“ULW 模式下异步读回”，默认开、重启生效，位于 waitable 开关之后；配置键 `Experimental.Inkeys3.Draw3.EnableUlwAsyncReadback`。关闭 = 同步读回、不节流、不对齐。
4. 辅助输出 SelectionUlw 保持同步。
5. 回执沿用“Present 返回 = 已提交”（与 DComp `Present1` 同口径）；延后完成失败时整帧读回并走现有恢复。
6. ULW 提交不超过显示刷新率（预设帧率高于刷新率时），跳过的帧合并脏区下次一起读回。
7. 逐行复制改为单遍 SIMD（SSE2 / NEON），结果逐字节不变。
8. 检测 DWM 是否在合成（启动与 `WM_DWMCOMPOSITIONCHANGED`）并记日志，刷新间隔来源随之切换。
9. 原阶段 E 的 ULW 部分并入 B：DWM 合成时提交对齐合成时机（预设高于刷新率时每周期提交最新且赶得上的一帧；等于或为约数时调整帧相位；非约数不调）；DWM 未合成、流水模式或对齐关闭时退回简单间隔节流。另加无界面配置键 `Experimental.Inkeys3.Draw3.UlwCompositionAlign`（默认 true）用于对照。
10. DComp 的合成时机对齐留在阶段 E（Win8+ 用 `GetFrameStatistics`），阶段 D（Win7 DWM GPU 路径）也可能复用 B 的合成时钟模型。
11. ULW 提交线程不做；B 完成后用实机日志（`ulw_ms` 平均与长尾）再评估。
12. 多矩形脏区不在本阶段，已记入中期重构第5步。

## 约束

- 不引入 GPU 查询自旋或 Win8+ 专属完成事件；阻塞只用驱动的 `Map` 内核等待（同现状）。
- 保持画布调试、主窗命中底层 α、像素诊断、辅助 DrawpadPresentation 回收语义、Win7 SP1 + KB2670838 兼容。
- `Map` 阻塞与对齐等待计入等待、不计入工作耗时（阶段 C 口径）。
- 帧循环仍按预设帧率运行（A2 光标节拍、鼠标笔迹采样不变）。

## Acceptance Criteria

- [ ] ULW 模式不创建交换链；Resize、恢复、运行期 DComp→ULW 降级、强制 ULW 正常。
- [ ] 异步开：GPU 轻载时 ULW 延迟与同步相当（即时模式）；GPU 加压时自动转为流水模式，帧率高于同步路径，无残影、无漏帧（停笔后最后一帧必定上屏）。
- [ ] 异步关：行为等同现状的同步读回（仅源换成独立纹理、复制换成 SIMD）。
- [ ] 预设帧率高于刷新率时，ULW 每秒提交次数不超过刷新率，停笔后最后一帧必定上屏，无残影。
- [ ] DWM 合成时开启对齐：每个刷新周期一次提交，`lateCount` 很低；与关闭对齐对照，延迟不增加、主观更跟手。
- [ ] SIMD 复制与标量逐字节一致，整帧 `rows_ms` 明显下降（benchmark 给出对照数据）。
- [ ] Win7 关闭 DWM（Basic 主题）时 ULW 正常，日志正确报告 DWM 状态与刷新间隔来源。
- [ ] 输出切换到 SelectionUlw、隐藏、Resize 时无错帧。
- [ ] 无窗口单测、自测与构建通过；Codex 审阅无 P0/P1 未修。
- [ ] 用户实机：本机强制 ULW + Win7 设备（Aero 与 Basic）验证通过。

## 现状依据

父任务 `research/presentation-pipeline-current.md` §1–§6、§8.3；归档 `11-17-dcomp-waitable-latency/research/field-test-20261117.md` §6。
