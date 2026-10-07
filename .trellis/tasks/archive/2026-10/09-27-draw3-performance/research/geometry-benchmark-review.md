# Draw3 激光增量范围基准独立静态审查

- 范围：`Draw3.PerformanceProbe.cpp/.h`、`IdtMain.cpp` 命令行早退、`Inkeys.vcxproj/.filters` 登记，以及集成产品的 `PlanLaserIncrementalRanges` / `FindProtectedStartIndex`。本审查未运行构建、基准或 GUI；Release 数据由主任务另行采集。
- 判定：没有发现阻断同一 Release|ARM64 版本三轮窄口径基线比较的代码缺陷。输出只可用于激光增量**范围规划函数加调用方游标更新**的 CPU 微基准；不能作为完整 Draw3 几何、Host、Present 或可见延迟结论。

## 路径和计时口径

- **集成生产实现：静态通过。** `Draw3.PerformanceProbe.cpp:13-14,160-162,185-189` 导入产品 `ink_prediction`，直接调用 `PlanLaserIncrementalRanges`；后者在 `Draw3.InkPrediction.cpp:155-176` 调用 `ink_prediction_detail::FindProtectedStartIndex`，定义在集成 `Draw3.StrokeGeometry.cpp:224-233`，每次从索引 0 扫描。三个文件均登记在产品 `Inkeys.vcxproj:1041-1042,1070,1082`，并未链接同名 demo 几何实现。生产激光路径在 `Draw3.DrawingController.cpp:784-834` 也调用同一范围规划器，并在上传/清理成功后更新两项游标；无 GPU 失败时基准的更新顺序与它一致。
- **无 HWND / 配置副作用：静态通过。** `IdtMain.cpp:267-280` 的精确参数早退在配置路径处理及产品窗口初始化之前。probe 本身仅构造 `vector`、调用模块函数、读取 `steady_clock` 和写 stdout/stderr，没有创建 HWND、Host、RTS 或设备，也没有读写配置。无法由静态审查担保整个进程的全局初始化均无副作用，但本次新增入口没有这类动作。产品工程为 Windows subsystem（`Inkeys.vcxproj:346-347`），调用者须重定向 stdout/stderr 才能可靠保存原始输出。
- **计时边界：静态通过。** `PerformanceProbe.cpp:140-154` 在计时外生成点与时长；`:156-172` 做逐前缀检查及轨迹摘要；每块在 `:178-180` 先清零状态，再从时钟起点到 `:192-193` 测完整轨迹循环。计时内包含 `span` 构造、生产函数、游标写入及循环开销；不含生成输入、每块状态重置、`CheckRanges`、摘要、格式化和输出。3 个预热块、11 个计量块（`:20-21,176,200-207`）；每块从相同初态增长，短场景为 `96 × 128 = 12288` 次调用，长场景为 `4096 × 2 = 8192` 次调用。`:209-217` 的 `mean_ns_per_call` 是 11 块总耗时除以总调用数；`samples=11` 指块级聚合样本，不是单次调用的延迟分位数。
- **场景：对本函数有效。** `:31-35,143-154` 有 96/4096 点、固定 75ms 或 25/150/45/120ms 循环保留时长；时间戳按 120Hz 增长。变化时长可使原始保护起点前后移动，`PlanLaserIncrementalRanges` 用稳定游标保证已提交端不倒退。坐标和半径对此扫描函数无影响；“steady”仅表示保留时长不变，点数仍逐次增长。该夹具没有覆盖同一前缀多帧、末点替换、Reset/重连中途、等时/逆序/非有限时间、多 contact，也没有覆盖普通笔/高亮笔、`PlanLaserLayerDirty` 的 bounds、GPU coverage/dirty、上传和呈现。

## 可操作发现

1. **P2 — 计时结果摘要只约束末态，不覆盖计时区的逐帧输出。** `PerformanceProbe.cpp:174-199` 只在每块计时结束后混入各重复轨迹的最终 `stableCommittedIndex`，以及最后一次 `lastRanges` 的两个 count；预热与计量块都参与该 `checksum`。中间某一帧的范围即便错误、但末态相同，这个字段仍可相同。新增 `trajectory_checksum`（`:156-172,211-217`）混入每个前缀的全部五个字段，显著改善 A/B 语义比较，但它来自**计时外另一次**同一生产函数运行，并没有独立固定的正确值，也没有核对每个计时块。建议基线和候选逐场景显式比对 `trajectory_checksum`，在优化后用独立参考扫描检查完整轨迹及边界用例；若需声称计时区结果逐帧一致，再以计时区外的预分配结果回读或抽样核对，明确其额外测量开销。当前摘要不宜称为计时中每次调用的校验和。
2. **P2 — 汇总平均值不能代表长笔画末端或尾延迟。** `PerformanceProbe.cpp:183-215` 把从 1 点到 4096 点的不同长度调用混在一个均值中，`FindProtectedStartIndex` 从头扫描的工作量随当前前缀增长（`StrokeGeometry.cpp:228-232`）。11 个 `elapsed_ns` 是完整轨迹的块耗时，不是单帧耗时，无法由它们算出单次调用的 P95/P99 或末端 4096 点成本。建议报告中直称“完整生长轨迹的平均每次规划成本”，保存块级原始耗时，并在需要定位长笔画热点时另设固定长度/末端调用口径；不要将这个均值用于 Draw3 帧分位数或输入到成功 Present 的比较。

## 使用边界和建议

- `CheckBoundaryCases`（`PerformanceProbe.cpp:81-130`）覆盖空、单点、连接点、保留时长变化、负时长及重建；`CheckRanges`（`:46-79`）检查范围界内和游标单调。全轨迹的五字段摘要是对照指纹，不是正确性 oracle。若候选优化 `FindProtectedStartIndex`，尤其要用未优化参考实现复核严格 `<` 的相等时间边界、末点替换、保护时长倒退、Reset 和非单调时间；普通笔也复用该 helper，但游标策略不同（`StrokeGeometry.cpp:1311-1323`）。
- 保持 A/B 的相同场景、调用数、`trajectory_checksum`、Release|ARM64 配置及设备/电源条件；分别保存至少三轮的每块原始输出。Release 工程启用 Whole Program Optimization（`Inkeys.vcxproj:65-70`）；若出现异常巨大加速，应检查优化后二进制确实保留调用路径。末态摘要不能单独证明所有调用在优化后都未被约简。
- 尚未测量的阶段包括 RTS packet/队列/有效消费、modeler/prediction、L0/L1 几何及 dirty bounds、D3D 上传与 shader/GPU、合成、成功 Present、history/UInk，以及真实设备/光学延迟。本文件没有任何性能提升或热点归因结论。
