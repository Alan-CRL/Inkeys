# 人工无反色反馈调查（2026-10-13）

## 证据边界

人工截图仍有抬笔折线且没有反色，阶段三未通过人工验收。本轮保持所有算法阈值不变。以下原因由真实 Modeler→生产点接纳→FinalizeStoredStroke 的无窗口合成回放确认，尚无截图对应真实输入，不能将某条合成原因直接宣称为该截图唯一根因。没有执行 GUI。

## 完成态链检查

正常 Up 经 CommitRuntimeStoredStrokeCpu → FinalizeStoredStroke → InkCanvas::AppendStroke(move) → 从同一 Stored Stroke 调用 DrawStoredStroke；完成重绘前清 L1/L0，之后进入历史 L2。源码未发现该路径丢弃反色瞬态标记。无窗口检查不能证明实际 GPU 呈现。

先前着色成功测试使用手造约 8 px 轨迹、0.3 px pin；先前 72 个真实 Modeler 测试为直线、停稳及续画，未覆盖快速大弧的修正效果。测试覆盖不足，不能由其成功推断产品大弧已修好。

## 新增真实模型弧线矩阵

使用 CreateStrokeModelConfiguration(96)、120 Hz 输入、2 rad 圆弧，半径 30/300/1000 px，时长 0.15/0.6/2 秒；各场景立即 Up 和停顿 0.2 秒后 Up。经过实际 Modeler Update、AppendNewModeledPoints、AppendEndpointBoundedModeledPoints、FinalizeStoredStroke。停顿模拟 endpoint admission，不覆盖全部 Controller 帧调度。

| 半径 px | 时长 s | 立即 Up 结果 | 最大拟合位移 px | 原末端角 rad → 拟合角 rad |
|---|---|---|---|---|
| 30 | 0.15 | Apply，反色 | 0.399324 | 1.561 → 0.002 |
| 30 | 0.6 | Apply，反色 | 0.022244 | 0.371 → 0.000 |
| 30 | 2 | NoChange，低于触发角 | 未求解 | 0.031 → — |
| 300 | 0.15 | Rejected / excessive_displacement，无反色 | 3.657342 | 1.374 → 0.023 |
| 300 | 0.6 | Apply，反色 | 0.265727 | 1.497 → 0.009 |
| 300 | 2 | NoChange，低于触发角 | 未求解 | 0.325 → — |
| 1000 | 0.15 | Rejected / excessive_displacement，无反色 | 12.191048 | 1.374 → 0.023 |
| 1000 | 0.6 | Apply，反色 | 0.796637 | 1.498 → 0.030 |
| 1000 | 2 | Apply，反色 | 0.088896 | 1.554 → 0.004 |

9 次立即 Up 中 5 次 Apply、2 次位移拒绝、2 次低角度不触发。快速大弧已明确进入求解，候选角从约 78.7° 降为 1.32°，但超过产品当前固定 3 px 位移限制，整个补丁被丢弃。R300 原 pin 长 3.4094 px；R1000 为 11.3644 px。反色逻辑因此不会显示，这是可复现的生产算法限制，而非本批样本中的渲染标记丢失。

9 次停顿场景均 short_window：Up 记录 raw 时，超过 160 ms 的旧 raw 被淘汰，仅剩 Up；窗口起点取唯一 raw 的 firstPoint（末点），可用窗口只剩 1 点，求解根本不调用。这暴露了接入层把可选 raw 当作窗口充分性前提的问题：raw 为空允许最近 512 点，只有 Up 一个 raw 却收缩为 1 点。独立 solver 的零/单辅助能力并未完整传到产品。当前停稳样本未证明还存在明显折线，不能为此直接向 160 ms 外扩大重构范围。

触发逻辑只看最后三个点，理论可能漏掉 pin 内部已经多点直线的场景，但本批快速大弧已触发，不能把该猜测冒充实际原因。

## 新诊断与下一步

临时开关 kDebugCompletedTailVisibility 继续统一控制反色和诊断。完成态写入 %TEMP%/Inkeys-Draw3-tail-<pid>.log：tail-diag-v2、编译日期时间、实际 EXE 路径、决策/文字 reason、点数、窗口起点、真实 Up、terminalFirst、committed、末端角、候选最大位移及最多 512 个模型尾窗点、64 个原始输入。每进程最多 128 次，文件上限 2 MiB；不会每个 Move 写盘，不保存窗口外 raw。仅临时本机诊断，包含屏幕轨迹坐标。

用户复测后应取得对应日志，先确认实际二进制、调用路径及拒绝原因，然后以该真实窗口精确回放。若仍为 excessive_displacement，需重新设计与 pin 长度/局部曲率/真实 raw 证据协调的位移预算，保留硬上限、入口/Up固定及自交/长度/真实尖角保护，不能只把 3 px 改大并宣称修复。单 raw 窗口应独立设计真实物理时间的模型窗口记录，避免无辅助时失去可平滑范围或跨出授权窗口。

## 当前验证

本轮首次原生 ARM64 完整 solution 构建通过；产品无窗口 probe 333894 assertions / 0 failures。新增测试检查活跃几何不被修改和真实 Up 保留，报告 Apply/回退而不把“无崩溃”视为平滑成功。主会话负责最终诊断 I/O 检查修正后的重建及 Headless 结果。

证据：Build/modeler-baseline/stage3-main-native/ARM64-Debug/tail-investigation-probe.log 与 tail-investigation-records.log（忽略生成物）。未 commit/push，任务保持 in_progress、人工未通过。

最终验证：诊断 I/O 审查修正后，原生 ARM64 InkeysRepo.sln Debug 完整 solution Build exit0；产品 probe 333894 assertions / 0 failures。最终 EXE SHA256=95344689c8361a6db6d8d71ad5cf020f3c7b3572a9e44d0909f5b9528d665cb5。Headless 首次仅 RenderScheduler 的 pending idle diagnostics 时序断言失败；未修改任何测试或该模块，同一 Headless 二进制（SHA256 f666205629b286ad91393e7367e4855d721038c5a79b34bf6b4d70fe11f24ade）复测 exit0/PASS。首次失败保留 tail-investigation-headless-first.log 与 result-first.json，未把波动隐藏为始终通过。最终结果见 tail-investigation-result.json。diffcheck 通过；没有运行 GUI/实体笔或改变求解器参数，现场根因仍待用户日志。
