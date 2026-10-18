# F-004 Bar idle 动画时钟独立复审

范围：只读复核当前工作区 `Bar.FramePacing.cppm`、`Bar.RenderLoop.cpp`、`Bar.Animation.cpp/.cppm`、`frame_pacing_tests.cpp`、`animation_tests.cpp` 的实际 diff，以及生产 Scheduler、Bar 布局/动画和内容切换调用链。本审查不改产品代码，也未执行构建、性能采样或 GUI。

## 结论与发现

**P2，已由主任务补丁处理，待补丁后构建复验：关闭动画时，idle 唤醒首帧可能提交旧图标或文字。** `Bar.FramePacing.cppm:106-110` 让首次唤醒 `Tick()` 返回 0；普通 Value/Color/Pct 在 `Bar.Animation.cpp:130-141,193-202,232-242` 会按 `animationEnabled=false` 立即到目标。原先 `SetAnimationOptions(false)` 只把内容时间线依赖的 speedRate 设为 `1e12`（`Bar.Main.cpp:239-246`），而 SVG/Word 的 `AdvanceContentTransition` 在 `Bar.UI.cpp:288-305,744-761` 调用 `BarUiKeyframeTimelineClass::Advance(0, 1e12)`；时间线因 `dt<=0` 保持活动，首帧仍绘制旧内容，下一帧才替换。生产可达入口包括 `Bar.Button.cpp:864-874,918-924,939-948` 的绘图、橡皮和图形按钮内容变化，以及 `Bar.RenderLoop.cpp:5610,5646,5904,5925` 的推进调用。

二轮补丁在 `Bar.Animation.cppm:612-656` 的 `AdvanceLocked` 中，对全局动画关闭直接走私有 `FinishLocked()`，该函数与原非正 duration 完成分支保留相同 generation、`reachedKeyframe`、`finished`、progress 和 active 语义。生产中的该类时间线仅用于 SVG/Word 内容切换；集中处理也覆盖 `Bar.Scene.cpp:1070-1081`，未改变动画开启时的零 dt 保留语义。`animation_tests.cpp:727-739` 新增开启状态下零 dt 保留、切换到关闭状态后零 dt 完成的断言，并恢复全局开关。审查发现的无调用公有 `LockedView::Finish()` 已由主任务删除，未留多余公开接口。

选项并发边界：`SetAnimationOptions` 分别写全局 enable 和 speedRate 后请求 Bar 下一帧（`Bar.Main.cpp:239-246`）；`AdvanceLocked` 的 enable 读取是原子操作，不产生数据竞争，但同一渲染帧可与早前读取的 speedRate/Value enable 暂时混用。该分离选项边界原已存在；本补丁在观察到关闭时即完成内容，下一次请求将继续采用最终配置。若需严格同帧一致性，应另定义成对选项快照，不属于 F-004 最小补丁。

## 主栏 idle / retry 路径判断

- `Bar.Main.cpp:209-226` 将状态变化通知到 Bar 请求位；共享 Scheduler 在 `RenderPipeline.cpp:297-305,480-530` 合并请求、idle event 等待并限制下一批至 60 FPS。其他客户端回调不调用 Bar 的时钟。
- `RenderLoop.cpp:983-987` 在每次实际 Bar 回调取 `Tick()` 和 raw 诊断值；只在 `CalculateDirtyAndDrawPresent` 确认无呈现需求并返回 Idle 前（`12941-12945`）调用 `SuspendForIdle()`。其下一次 Bar 回调记录完整 raw 间隔但动画 dt 为 0；再下一次按活动帧间隔推进。`Rebase()` 不在普通 Retry、失败退避和连续动画帧中调用，符合规范。
- 新目标由 `SubmitTargetsAndLayout` 建立；`AdvanceAnimationsAndDeriveLayout` 对未完成的 Value/Color/Pct 用 `changed || active` 建立 `needRendering` 并标记 dirty（`RenderLoop.cpp:5354-5380,5549-5595,5885-5940`）。合法 `dt=0` 的 `BarUiAdvanceAnimation` 保留当前值、目标、progress，返回 `{false,true}`；强制替换、Once、无效 target/duration/speed 与关闭动画的普通值替换先于该分支执行。`animateWhenDisabled=true` 的 Color/Pct 则按独立实时速度在下一活动帧推进。
- `PrepareLightingAndDemand` 把 `needRendering` 转为 present demand（`RenderLoop.cpp:7870-7923`）；成功呈现返回 `FrameResult::Continue`（`13240-13259`），Scheduler 的 `DispatchState::Complete` 只续该客户端（`RenderPipeline.cpp:312-345`）。下一批至少有一个 Bar 回调，时钟取得正的帧间隔，动画可推进。完成后仍有一次无需求回调，走 Idle 并重新挂起；未见因零 dt 而无限自续。
- backoff 门在 `Tick()` 之后（`RenderLoop.cpp:13037-13045`），跳过时返回 Retry 且不调用 `SuspendForIdle()`；布局交接、present/资源失败也继续 Retry/DeviceLost（`13209-13259,9236,12669-12685,12929-12938`）。这保留既有失败等待的时间语义；不把退避误当 idle 重新归零。退出信号在 Tick 前返回 Idle（`13003-13008`），不推进动画。

## 验证状态与边界

- `frame_pacing_tests.cpp:40-69` 已覆盖活动帧、50 ms 限幅、负时间、显式 Rebase、长 idle 的首次 raw/动画 dt 分离及第二帧 16 ms。`animation_tests.cpp:914-969` 已覆盖普通 Value/Color/Pct 的零 dt 保留、后续轨迹、关闭动画的即时替换及 Color 的 `animateWhenDisabled` 正 dt；新内容时间线测试覆盖关键完成分支，但现有 headless 工程未编入 `Bar.UI.cpp`，故不能仅凭该单测声称 SVG/Word 整条生产调用链已运行验证。
- 主任务报告补丁前第二轮 `Debug | ARM64` Solution 与完整 `--no-window` 通过；补丁后的首次 Debug 构建退出码为 0，但移除无调用方法发生在构建途中，故最终增量复编和 `--no-window` 仍由主任务执行。本复审未独立运行。真实窗口长 idle 后展开、属性面板/粗细预览、禁用动画切换按钮图文仍需人工确认。Release 构建或性能对照不能替代这些视觉检查。
- `Bar.Scene.cpp:1034-1047` 还有独立的每 Surface `lastFrame` 时钟；PageControl/Whiteboard 使用它，长 idle 后可得到限幅后的 50 ms。F-004 当前代码只修 Bar 主栏 `FrameAnimationClock`，因此不把本结论外推为所有 UI3 Surface 都消除了首帧 idle 跳步；若任务验收范围包括这些 Surface，应另立目标与确定性测试。
