# Draw3 ContactInput benchmark 独立审查

审查对象：`InkeysHeadlessTests/draw3_contact_tests.cpp` 与 `animation_tests.cpp` 的未提交 benchmark diff；生产实现为 `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp[ m ]`。本次只读审查，未运行构建或基准。

## 结论

- **生产路径与无窗边界：通过静态检查。** 测试工程将生产 `Draw3.ContactInput.cpp/.cppm` 编入目标，benchmark 导入该 module 并直接调用 `ContactInputCoordinator`。`main` 在 `--draw3-contact-benchmark` 时提前返回，不进入窗口测试。benchmark 内没有创建 HWND；构造 coordinator 会调用 `GetSystemMetrics` 和 `CreateEventW`，后者仅创建内核事件。
- **基本生命周期：通过静态检查。** 每轮建立 coordinator，发布 Down，取出并保存 generation handle；20,000 次串行 PublishMove + TryReadSnapshot；发布 Up、读取终态，再 Recycle。失败返回非零，最终 X 与 Up phase 被检查。构造/销毁、Down/Up/Recycle 均在计时区间外。
- **测量范围：仅为生产输入协调器的单线程 Move 发布与快照读取平均成本。** 没有 RTS 回调、实际 producer/consumer 并发、队列积压、modeler、几何、GPU 或成功 Present。不能作为 Draw3 全链路延迟、有效采样率或屏幕可见 P95 的证据。

## 修补后复核

- **位置与 sequence：静态通过。** 每次串行 PublishMove 后读取的 X 必须等于该次 index，且 sequence 必须严格递增；读取失败会使整轮返回非零。这个断言证明本合成串行轨迹逐次读取了新快照，仍不代表并发生产者/消费者的逐样本交付。
- **Up QPC：静态通过。** Up 的 QPC 为 `iterations + 2`，严格大于末个 Move 的 `iterations + 1`。Up 坐标仍为 `(1,1)`，与最后的 `(19999,0)` 不连续；仅在输入协调器微基准范围可接受，不应用来评估轨迹/modeler。
- **无窗、生产模块及范围：结论不变。** 修补仍只改两处测试文件，入口仍提前返回，未创建 HWND。主 agent 另行报告完整 Debug|ARM64 构建及三轮基准退出码 0；本复核没有重复运行。

## 统计输出最终复核

原 P2/P3 均已修复：当前输出使用 `median_run_mean_ns` 和 `max_run_mean_ns`，准确表示各 20,000 次操作轮均值的中位数和最大值；`run_mean_samples_ns` 来自排序前复制的 `runMeans`，保留轮次顺序。排序后的 `samples` 仅用于 median、max 和噪声计算，且复制发生在计时区间外。静态复核通过，未见新的可操作问题。

输出为单行 `BENCH_DRAW3 ... run_mean_samples_ns=`，字段可解析且包含迭代、预热、轮数、轮均值 median、轮均值最大值与按采样顺序排列的样本；它不是 CSV/JSON。`QueryPerformanceFrequency` 检查存在。源码 diff 只增两处 benchmark 入口与所需头文件，未见无关实现改动。

**最终结论：此微基准作为生产 ContactInput 串行发布/读取成本基线可用，静态审查通过。** 它不能代表 Draw3 全链路或真实设备尾延迟。本结论为独立静态审查；主 agent 报告的 Release|ARM64 完整构建和三轮基准退出码 0 属于另一次执行证据，此文件没有复用它作为审查者亲自运行的证明。
