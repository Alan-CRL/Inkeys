# Draw3 激光增量几何无窗口基准设计

## 实测对象和入口

`Draw3.PerformanceProbe.cpp` 导入产品模块 `Inkeys.Drawing.Draw3.ink_prediction`，直接调用产品 `PlanLaserIncrementalRanges`，随后按 `DrawingController::UpdateLaserIncrementalCoverage` 的做法更新 `stableCommittedIndex` 和 `rebuildRequired`。入口为 `RunDraw3GeometryBenchmark() noexcept`，由主程序显式 `--draw3-geometry-benchmark` 参数调用；不初始化 Host、RTS、GPU、窗口或测试专用产品配置。GUI subsystem 下需由调用者将 stdout/stderr 重定向到文件。

计时区只含逐帧构造 `span`、上述生产函数调用及游标更新。轨迹和保护时长数组、每轮状态重置、正确性检查、校验和及输出都在计时区外。报告值是此调用路径的平均软件耗时，仍含循环与传参开销；不是 Draw3 全链路、GPU、成功 Present、像素或光学延迟，也不报告 P99。

## 固定场景

| 场景 | 点数 | 每块重复完整生长轨迹 | 保护时长 |
| --- | ---: | ---: | --- |
| `short_steady` | 96 | 128 | 75 ms |
| `short_changing` | 96 | 128 | 25/150/45/120 ms 循环 |
| `long_steady` | 4096 | 2 | 75 ms |
| `long_changing` | 4096 | 2 | 25/150/45/120 ms 循环 |

每条轨迹从 1 点增长到指定点数，时间戳间隔 1/120 秒；坐标和半径使用固定算式，输入在所有 block 与进程轮次相同。变化的保护时长使受保护起点前后移动；已提交的 L1 游标不得随时长增加而后退。每场景先做 3 个 warmup block，再做 11 个 measured block。每个 block 都输出原始 `elapsed_ns` 与调用数；summary 给出 11 个 block、总调用数、`mean_ns_per_call` 和确定性 checksum。前后对照须逐场景比较相同的调用数与 checksum，并保存至少三轮原始输出及构建配置。

## 语义检查与边界

计时前调用同一产品函数验证空输入、单点、L1/L0 共用连接点、稳定 delta 与前次 L1 的衔接、时长加大导致保护起点后退、负时长归零和重建游标。完整场景每个前缀还断言游标不后退、范围在界内、live 从已提交连接点开始以及稳定 delta 的连接点重叠。失败返回非零退出码；测量数据无效。

这一基准只隔离激光增量范围规划的 CPU 成本，不能据此声称整笔画、其他工具、history/persistence 或视觉体验改善。主任务的 Host 分段采样、完整 Solution 构建、真实设备和输入语义复验仍分别记录。
