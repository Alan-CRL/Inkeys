未写入 `-o`：请求未提供目标路径，且当前沙箱只读。以下为报告；未修改文件、运行 git、编译或启动 GUI。

## 事实（file:line）

以下简称均位于 `Inkeys/Inkeys/Drawing/Draw3/`：DC=`Draw3.DrawingController.cpp`，SG=`Draw3.StrokeGeometry.cpp`，TP=`Draw3.TransparentPresentation.cpp`。M=`Inkeys/additional/ink_stroke_modeler/`。日志 L1/L2 分别为所给两份日志。

**1. 预测时间链存在不规则采样风险。**

- Pen/HardPen 启用 `useDisplayTime`（DC:17577）。真实输入以 snapshot QPC 转模型时间（DC:18032–18049）。
- [`ResolvePenModelInputTime`](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:2424) 只在 `modelClockStopped` 时压缩停笔空档；通常返回 `max(realTime-offset,lastModelTime+1µs)`。因此模型时间不倒退，但落后的真实样本可能被压到极短间隔。
- 无新模型输入的帧，超过 **预设**一帧便开始 settling（SG:2167–2172）：120/240 预设阈值分别约 8.33/4.17ms，与显示刷新率无关。未 settled 时，用最后位置、`frameRealTime-offset` 更新模型，未按单帧跨度封顶（DC:22294–22357）。
- `frameQpc` 在 DC:22169–22170 采样，随后才消费 active snapshot（22193）及重连 ingress（22219）。故 snapshot QPC 可以晚于本帧 QPC。该顺序本身不导致模型倒退：本帧有模型输入时不会再合成 stationary input。
- Endpoint admission 对停笔/恢复几何有边界过滤（SG:2436–2478），且激活时禁用普通 prediction（DC:22702）。所以“stationary update 直接画出无界预测针”不是完整解释；需关注恢复后重新开放预测的时刻。

**2. RTS 批处理不是逐包保留硬件时间。**

[`Draw3.RealtimeStylus.cpp:1489`](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1489) 只取 batch 最后一个 packet；1505–1507 使用 `QueryQpc()`，1528 明示 `retained=1 timestamp=callback-arrival`。随后 DC:17912–17916 又只消费最新 snapshot。因此模型看到的是回调/消费节奏下的稀疏位置，不是均匀硬件样本。

**3. Kalman 有时间窗口限制，但无几何长度硬限制。**

- [`kalman_predictor.cc:88`](D:/Project/Inkeys/Repo/Inkeys/Inkeys/additional/ink_stroke_modeler/internal/prediction/kalman_predictor.cc:88)：轴滤波器假设每次采样间隔为 1，再用窗口平均 `dt=(last-first)/N` 将速度、加速度、jerk 分别除以 `dt/dt²/dt³`。
- 同文件:118–125 先生成 cubic connector，再生成 extrapolation。Connector 点数由距离/端点速度决定，没有显式总点数/时长上限（147–168）；后段按 prediction interval × output rate × confidence 限制点数（230–282）。线性置信度下限配置为 0.4（`Draw3.InkPrediction.cpp:220–226`），不是异常预测硬拒绝。
- SG:470/472：120/240 预设后段 nominal horizon 为 16.67/8.33ms，最多约 6/4 点；**不包括 connector**。`max_outputs_per_call=2000` 不是可见预测长度限制。
- StrokeEnd 分支是复制位置模型做终点收敛，最多 20 次迭代（`M/internal/prediction/stroke_end_predictor.cc:40–47`；`Draw3.InkPrediction.cpp:239–240`），也不是像素封顶。当前会话实际选择何种 prediction mode，现有日志未证明。

**4. 一帧长针能被自然替换。**

[`RebuildPredictedPoints`](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:2513) 清空后直接转换全部结果，无通用 finite、路径长度或端点距离检查。预测只接入 L0（SG:2561），下一帧重新 Predict/重建（DC:22694–22709），旧/新 L0 dirty 均纳入重画（22767–22768）。画布裁剪不等于预测长度限制。

**5. 重连 512px 不是绘制上限。**

`Draw3.InkPrediction.cpp:576–584` 的 adaptive maximum 用于判断新 Down 是否可接纳。外推也是匹配计算。物理 Up 创建候选时确实冻结一份 prediction（DC:18165–18193），候选刷新时可用于 L0（22695–22700），之后保持原显示（22673–22674）。所以坏预测可被重连等待态保留；重连不会凭 512px 参数自动画出 512px 延长线。

**6. Stage A 确实能改变预测时序。**

直接读取提交对象确认 HEAD/parent；SG、InkPrediction、RealtimeStylus 在两提交间 blob 相同。DC 新增 [`PrepareFrameLatency`](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:21608)，位于后续 drain、snapshot 消费和 frameQpc 之前。TP:1295–1308 还给帧循环外 Present 增加补等；`Present1(0,0,…)` 保持不变。

Gate 超时可自适应至 200ms（`Draw3.FrameLatencyGate.h:69–76`），等待不可由普通输入中断（TP:883）。但两日志没有 tripped，已记录 waitMax 最大 28.3294ms；这不支持把全部长停顿归于 gate。`acquired` 也不是实际显示 FPS。

## 推断

**尖峰首选解释：不规则有效采样 → Kalman 导数估计异常 → 无几何保护的 L0 预测。** RTS 回调到达时间、末包保留、latest snapshot 合并共同改变采样间隔；小平均 dt 会放大导数。若发生“空帧先推进模型、较早 snapshot 后发布/消费”，1µs 补时进一步恶化。普通延迟 batch 通常获得新的到达 QPC，**不能假定每次 burst 都触发 1µs 压缩**。

Endpoint admission 能缓解停笔阶段，但没有修复预测器对非均匀采样的假设。单帧恢复特征更支持临时 prediction，而非已经提交的真实几何。重连是次要候选，尤其需与异常 Up/Down 对齐。

**Stage A 判定：can；是否引入本次尖峰仍 unclear。** 它不修改预测公式，却改变 drain 时机、被合并样本和 frameQpc；也可改变 settling 分支选择。原有风险在 pre-A 已存在，需 A/B 证明发生率变化。

**停顿按现有证据排序：**

| 路径 | 判断 |
|---|---|
| Present/驱动、UI3 GetDC/ULW、调度竞争 | 最值得先测。悬停绕过 pacing（DC:22088、23535–23605），约千次/s acquire 与之相容；不能据此认定全部都是千次/s Present。 |
| ULW 同步读回/整屏交接 | TP:603–606 的 `Map(READ,0)` 可等待 GPU，660 调用 ULW；2880×1920 切输出时尤需测。可阻塞 Draw3，系统光标影响仍是推断。 |
| History/compose/restore | DC:19185、19882、21572 同步恢复；`Draw3.InkHistoryGpu.cpp:1370–1525` 可逐 tile 重放；DC:22078 的后台 prime 单 tile 内无输入抢占。可造成下一笔延迟，日志无耗时归因。 |
| Stage-A wait | 已证实偶发约 13–28ms；理论可至 200ms，但本次未记录该量级。 |
| AutoSave clear | DC:20979 先 capture tail，21007 再 capture/export，均在 render thread。**记录到的 export 仅 0.428–5.630ms**（L1:84/113/161，L2:108）；不能覆盖整个 Clear 耗时。晚数秒的 commit 在 worker，不能算 render stall。 |
| 重连/eraser idle/admission/锁 | 重连是可唤醒的 deadline 等待（DC:23575–23603），非无条件 sleep 80ms。Eraser idle 有 arm、sequence 复查（5859–5889）。Admission 是原子门控（ContactInput:1043），可造成拒收而非线程阻塞。navigationMutex（935）、WindowControl command/style/feedback 锁、DC historyPolicy 锁可竞争，但未发现长持锁证据。 |

**系统鼠标光标冻结应单独归因。** `MouseHook.cpp:35–39` 直接链传 MOVE；Transport:153–168 有界提交，无业务锁等待，故当前钩子实现嫌疑较低，仍可能受其线程调度或后续 hook 影响。UI3 Bar 在 `Bar.RenderLoop.cpp:12218/12231` 执行 GetDC/ULW；L2:62 最大 GetDC=78.322ms，L2:103 GetDC=50.054ms、ULW=42.632ms。它们证明 UI3 存在慢操作，**不证明导致系统光标冻结**。

## 待实测

现有秒级摘要没有尖峰/冻结标记，无法时间对齐根因。最小增量诊断：

- 异常帧环形记录：contact/sequence、RTS batch 数、arrival/publish/consume/frame QPC、raw/model dt、offset、stationary/settling/recovery 状态。
- prediction：模式、connector/后段点数、平均 dt、最大距离、累计长度、horizon、raw/real/predicted tip；重连记录创建/接纳/过期。
- >16/32ms 阶段记录：wait、drain/model、history/compose、Clear 全程、Map、Present、GetDC、ULW，以及锁等待/持有时间。钩子用计数器记录 callback/链传耗时和线程心跳，避免同步写日志。
- 同设备固定刷新率，对 pre-A/A、prediction disabled/StrokeEnd/Kalman 做短矩阵测试；系统冻结时另采调度/GPU ETW。

## 建议（最小修改，按优先级）

1. **Stage B 前：加异常触发诊断，并给预测可见尾段加 finite、时间、累计长度/最大距离保护。** 最小位置是 SG `RebuildPredictedPoints`；超限丢弃本帧预测，保留真实尾段。重连冻结 prediction 也复用校验，防止坏预测进入匹配。
2. **Stage B 前：完成 A 开/关基线及实际 Present 计数。** B 若改变 pacing，会同时改变模型采样节奏；应先保存可比较证据，并覆盖 hover。
3. **Stage B 前取得证据，修复时序问题可独立提交：** 若确认合成推进后的真实样本被压至 1µs，再修模型时钟/预测器重锚策略；不要直接全局增大最小 dt。
4. **可在 B 后按测量处理：** 单 tile 重放预算、Clear 快照开销、ULW/GetDC 路径。当前证据不足以支持优先重构 AutoSave 或鼠标钩子。