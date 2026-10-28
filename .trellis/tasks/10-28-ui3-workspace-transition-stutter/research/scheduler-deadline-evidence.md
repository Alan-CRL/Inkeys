# Research: 真实转场的帧截止时间超时

- Query：主栏成功间隔慢，是否全部由绘制耗时造成？
- Scope：internal，辅以Microsoft官方API说明；不改变产品行为或配置。
- Date：2026-10-28

## Findings

### 当前pilot的直接数据

实施代理的真实Scheduler六向pilot成功退出0，当前测试EXE哈希 `6797DE71E9DBFF0E00641577964EF46A4923EFA19F86E3837D8508B2F1B7238A`。只读解析当时 `TestResults/ui3-workspace-transition/current/transition-100-1-{metadata.json,batches.csv}`，按clockPeriod换算，相邻批次使用frame_ticks时间戳：

- 共305个批次，其中251组相邻记录的前批从frameTime至end耗时<15ms。
- 这些组的后批启动间隔P50=31.1341ms，P95=34.5268ms。
- 相对于 `prev.frameTime+16.666667ms`，启动超时P50=14.467433ms，P95=17.860133ms。

该筛选排除前批自己已超过一帧预算的情况，证明还有显著等待/调度延迟；它不能单独证明所有延迟都来自系统计时粒度，仍需相同负载下替换等待点的A/B对照。原始current目录会由实施代理归档为baseline，报告应引用最终保存路径及哈希。

### 生产接线与已有能力

- `RenderPipeline.cpp:736-739` 以batch起点设置16.666667ms截止，活动等待用 `std::this_thread::sleep_until`。没有证据说明其在绘制结束后固定再睡一帧。
- `Bar.FramePacing.cppm` 是不依赖RenderPipeline的独立module。已有 `WaitForRemainingFrameTime`、`FramePacingWaitPolicy::WaitableTimer`、线程私有可回收waitable timer、高分flag失败后的兼容回退及失败处理。应复用该能力，避免新增第二套timer或全局timeBeginPeriod。
- `Draw3.DrawingController.cpp:14767` 的 `TimerPeriodController::SetSelectionMode` 在选择态释放1ms period、绘制态才请求；离屏fixture未启动Draw3。因此主栏帧节拍不应依赖Draw3恰好处于绘制模式。

最小候选：仅在当前时刻早于deadline时，使用已有waitable timer等待剩余预算，保留最大60Hz、batchstart deadline和普通Idle无限event等待。选择0自旋尾段，默认不额外忙等；过期deadline直接执行，输入wake不允许提前突破deadline。失败/旧系统沿既有回退，不通过缩短动画或人为给测试申请period掩盖。

## External References

Microsoft的[Sleep说明](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep)指出等待受时钟粒度影响，超时后线程就绪也不保证立即运行；因此上面的源码归因必须结合本机A/B，不可承诺绝对实时。

[CreateWaitableTimerExW说明](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw)将高分辨率flag列为Windows10 1803起支持，老系统需要兼容路径。这里沿用本仓已有兼容实现，不据当前机器结果声称Win7实测通过。

## Caveats / Not Found

pilot不包含原生Window/Draw3协调或WhiteboardFreeze；实际视觉和CPU调度噪声另行验收。此发现解释活动帧节拍候选，不授权跳过首帧/背景/owner回执。
