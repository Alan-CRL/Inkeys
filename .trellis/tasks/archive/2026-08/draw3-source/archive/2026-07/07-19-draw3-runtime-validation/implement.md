# Draw3 运行时全量验证实施计划

## 1. 工程与测试骨架

- [x] 新增无第三方依赖的测试主程序、断言和用例调度。
- [x] 新增 `inkStrokeModelerTestTests.vcxproj/.filters`，链接生产模块源码并定义测试宏。
- [x] 注册 Debug/Release x86、x64、ARM64 解决方案配置。
- [x] 忽略 `TestResults/`，提交环境/阈值/分位数摘要模板。

## 2. 可中断帧调度

- [x] coordinator 增加 event、wake generation 和窄化等待接口。
- [x] Down、Up/Cancelled、ControlWake 发布后 signal；Move 不 signal。
- [x] DrawingController 活动等待改为 generation-aware event wait，保留 120 FPS Move 合并。
- [x] 增加 lost wake、空闲阻塞和零自旋自动化测试。

## 3. 运行指标

- [x] 新增可选 `RuntimeMetricsSession`，预留有界样本并定义只读诊断快照。
- [x] 记录工具/设备、首次可见资格、首次成功 Present、帧间隔、工作/Present 耗时和输入计数。
- [x] 主程序解析输出/严格验收参数；关闭指标时不创建会话、不写文件。
- [x] 输出 JSON 并增加解析与阈值测试。

## 4. 自动化基准

- [x] 用 `SendInput` 覆盖 200 次落笔、直线、曲线、240Hz Move、停笔和三种工具。
- [x] 覆盖 resize、clear、5 秒 idle 和按键 `9` 正常退出。
- [x] 连续三次 Release 运行核对 p99/长帧/空闲门槛。
- [x] 把原始报告留在 `TestResults/`，更新可提交摘要。

## 5. 跨架构与真机

- [x] ARM64 Debug/Release 全解决方案 Rebuild 并运行测试。
- [x] x64/x86 Release 构建并运行可执行的测试，验证指针宽度和 lock-free 断言。
- [x] 记录 Pen 100 次、Touch 100 次及完整多指/书写稳定性矩阵。
- [x] 如实记录 D3D11 Debug Layer、Windows 7 或硬件限制导致的未验证项。

### 2026-07-20 ARM64 真机摘要

- 环境：Windows 11 ARM64，Qualcomm Adreno X1-85，D3D feature level 11_1，`UlwDirtyRect`，RTS `maxTouches=10`。
- Pen 隔离基线：157 Down/terminal/recycle，0 rejected/contended，landing p99 3.5352ms，frame p99 8.6592ms，long-frame ratio 0，idle frame/present growth 0。
- Touch 场景轮：120 Down/terminal/recycle，覆盖多指、resize、clear；0 rejected/contended，landing p99 11.5426ms，其中 4 个样本超过 8.33ms，frame p99 8.8682ms，long-frame ratio 0。
- Touch 隔离基线：103 Down/terminal/recycle，0 rejected/contended，landing p99 2.4555ms，超过 8.33ms 的样本为 0，frame p99 8.5140ms，long-frame ratio 0。
- 原始报告：忽略目录 `TestResults/pen-100-retry.json`、`touch-100.json`、`touch-100-retry.json`；通用 strict gate 要求 200 landing，因此 100 次真机矩阵不以 `strictPass` 字段判定。

## 6. 质量门

- [x] 运行 `trellis-check`、`git diff --check`、编码/换行检查。
- [x] 确认两个 Shader、C++ Modules、资源嵌入和最终链接。
- [x] 确认 concurrentqueue/vcpkg manifest、triplet 和安装路径配置未改；构建生成的未跟踪 `Vcpkg/` 缓存继续排除在提交外。
