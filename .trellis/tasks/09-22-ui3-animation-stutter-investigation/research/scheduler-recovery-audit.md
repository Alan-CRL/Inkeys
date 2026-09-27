# Research: UI3 共享调度、动画时钟与失败恢复

- Query: 主栏动画极低概率变慢并跳变，关闭动态光影有改善，重启可能恢复；补充证词为同一运行期也可能时快时慢。检查共享线程、光影广播、时钟、退避和设备恢复的可达机制。
- Scope: mixed；以当前工作区源码为准，官方 Win32/DXGI 文档仅用于 API 语义。
- Date: 2026-09-22
- 阶段与权限: 仅调查；没有修改产品源码、现有测试、工程或 spec，没有启动主程序或 GUI，没有执行 Git 操作。任务创建不视为实施授权。

## Findings

### 1. 新发现：主栏鼠标光可以无差别唤醒可见分页窗，空 damage 反而走整窗重绘

这是不依赖任何 API 失败、能够随鼠标和可见窗口状态变化的已确认工作量放大路径，比“仅在错误后才成立的退避”更适合优先纳入动态光影调查。

完整生产链路：

1. `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:7804` 推进光照；`:7833` 发布包括鼠标屏幕坐标的共享光照快照。
2. `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:114` 的全局快照比较包含鼠标坐标；`:1899` 仅在整个快照未变时返回。`:1902` 遍历全部订阅 scene，`:1908` 应用新光照，`:1910` 无条件收集每个订阅 scene 的 hooks，`:1916` 无条件唤醒。
3. `Bar.Scene.cpp:193` 和 `:662` 已经计算光源影响与实际边缘的交集。如果鼠标旧、新影响范围均不与该 scene 相交，`resolved.damage` 可以为空；`:606` 对空矩形不设置 pending damage。这说明该 scene 已具备“这一光照变化不影响我”的信息，但发布者没有用它抑制 wake。
4. `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:819` 将 scene 的 wake 绑定到 `RequestSurface(index)`；`:1448` 更新光照订阅。`PageControl.cppm:768` 的条件是目标可见或退场过渡，范围不考虑光源距离。
5. `PageControl.cpp:1512` 只要 `shouldShow` 就在 `:1527` 调 `PresentScene()`，并未先检查 scene damage。
6. `PageControl.cpp:955` 到 `:959` 先对整个 target `BeginDraw/Clear/Render`；`Bar.Scene.cpp:2347` 绘制背景，`:2358` 遍历全部可见 widget，没有按鼠标光照 damage 裁剪 D2D 绘制。
7. `PageControl.cpp:973` 到 `:978` 在没有 business damage、末帧或调试更新时，将 dirty 回退成整窗；`:1037` 到 `:1055` 照常 GetDC、ULW、`ReleaseDC(nullptr)`。

因此，主栏附近一次鼠标光照变化可使远处所有当前可见、已订阅的 PageControl 窗口重绘并呈现，即使这些窗口的内容和光照贡献都没变。最多涉及四个分页 HWND；白板常用其中底部两个。已经隐藏且完成退场的窗口会取消订阅，不应说成“始终四窗”。

调度请求按位合并，并非每个 Raw Input packet 都排一个独立渲染任务。工作量应表述为“每个有光照变化的调度周期，可以多出至多四个串行窗口绘制/呈现”，而非无限队列。

**可证明的缺陷**：已能判定为空的局部光照变化仍触发全窗重绘/呈现。**未证明的现场归因**：尚无各客户端耗时记录，不能断言这些分页窗在用户现场可见，也不能断言它们贡献的时间已超过 50 ms。

### 2. 失败退避同时暂停动画，并逐帧丢失时间；放大倍数可达约 60

`Bar.RenderLoop.cpp:949` 每次 Bar 回调都增加 `presentAttemptFrameSerial`。`:968` 每次都 `animationClock.Tick()`。但 `:12931` 到 `:12933` 在退避期间立即返回 `Retry`，而真正的布局与动画推进位于 `:13058` 到 `:13064`。

`FrameAnimationClock::Tick()` 在 `Bar.FramePacing.cppm:104` 总是把基准设为当前时刻。退避回调领取的 dt 没有传入动画、也没有积累到下一次实际推进。故“调度器继续 60 FPS 回调”不等于“动画按 60 FPS 前进”。

已用生产 `Bar.PresentDecision.h` 做独立 C++ 确定性检查。假定共享周期恰为 1/60 秒、速度倍率为 1、每次实际尝试均以同类 ULW 失败结束、没有新 demand 或 epoch：

| 项目 | 结果 |
| --- | --- |
| 前八次尝试的回调序号 | `1, 2, 4, 8, 16, 32, 64, 124` |
| 第 124 次回调的墙钟时间 | 2.066667 秒 |
| 到此真正进入动画推进的次数 | 8 次 |
| 被退避门直接跳过的次数 | 116 次 |
| 累计交给动画的时间 | 0.133333 秒 |
| 在同样持续失败条件下累计推进 0.3 秒 | 需要到第 724 次回调，即 12.066667 秒 |
| 退避封顶后稳态 | 每 60 个回调只推进约 16.7 ms，约 1/60 速 |

以上量化的是**内存中的动画状态**，不是屏幕每秒显示几帧。模型所有尝试都失败，所以可见画面不会成功更新；现场若间歇成功，将在成功帧跨过这段期间未呈现的状态。每次完整成功在 `Bar.PresentDecision.h:375` 到 `:381` 清空退避，不存在“只要失败过一次以后永久 1/60 速”。

如果错误是随动画几何而变化的参数错误，这个门还会让几何离开错误区间变得非常慢：修复参数需要动画推进，而动画只在退避允许的尝试帧推进。是否存在这种错误由主调查的容量/viewport 验证决定，不能在本报告中假定已经发生。

### 3. 鼠标通知可以立即清空上述退避，造成输入相关的恢复节奏变化

`Bar.Main.cpp:199` 的 `UpdateRendering(false)` 仍在 `:214` 调用 `BarAtomic::wait.Notify()`，并于 `:215` 请求 Bar。`Bar.WakeSignal.cppm:22` 每次增加 generation。`Bar.RenderLoop.cpp:954` 到 `:955` 在退避门之前读取并观察 generation；`Bar.PresentDecision.h:286` 到 `:291` 遇到任意新 generation 就清空恢复状态。

同类错误期间每次回调前都给一个新通知的独立检查结果：124 个回调全部尝试，失败计数始终回到 1，重试间隔始终为 1，没有任何回调被退避门抑制。

所以这两条路径不能简单同时叠加为“鼠标越动，60 帧退避越让动画慢”。实际是：

- 没有新通知：持续错误积累退避，动画被门一起暂停并丢时间。
- 持续新通知：退避被反复打断，动画恢复逐回调推进，但错误尝试工作量也恢复到每回调一次，且仍然不能保证成功呈现。
- 同类错误一旦完整成功：退避立即结束。

它能够产生“输入时和停下时节奏不同”的状态变化，但仍需现场 GetDC/ULW/ReleaseDC/EndDraw 的错误证据才能解释用户问题。

### 4. 50 ms clamp 确实造成长帧慢放，但属于已有明确合同

`Bar.FramePacing.cppm:100` 到 `:107` 使用 steady clock，返回 `[0, 0.05]` 的 dt，超过部分直接丢弃；没有时间债务。

若所有帧都成功、实际间隔 100 ms，每帧推进 50 ms，即半速；200 ms 则约四分之一速。它不是第一个慢帧的来源。`.trellis/spec/native-desktop/rendering-and-ui.md:76` 和 `:90` 明确规定这个上限；不能作为普通错误随手删除，必须在获批后分别处理参数时间轴、弹簧积分和真实 idle。

动态光照还在 `Bar.Rendering.cpp:571` 到 `:573` 再限幅一次，因此仅改第一处也不能建立完整时间语义。

### 5. 确认 idle 时钟接线回归：当前在睡前 Rebase，而不是醒后

当前产品路径的唯一 `animationClock.Rebase()` 在 `Bar.RenderLoop.cpp:12865`，随后返回 `Idle`。共享调度器以后才在 `RenderPipeline.cpp:327` 进入等待，或者继续服务其他窗口；下次 Bar 被请求后在 `Bar.RenderLoop.cpp:968` 直接 Tick。没有任何醒后的 Rebase。

确定性时钟模型：t=16 ms 时返回 Idle 前 Rebase，t=2018 ms 再执行 Tick，得到 50 ms；若在 t=2016 ms 真正恢复 Bar 活动时 Rebase，下一帧只得到 2 ms。现有 `frame_pacing_tests.cpp:40` 到 `:51` 手工在醒后调用 Rebase，只验证 helper 能正确执行，不覆盖产品把调用放错位置。

这能解释 idle 后首帧明显跨步，但**不能单独解释持续运行中的长时间慢放**。修复应是每客户端记录上次结果是否真正 idle，在该客户端从 idle 恢复时重置基准；不能因其他客户端继续渲染就把 Bar 视为持续活动，更不能在一般 `Retry` 上重置。

### 6. 新 device epoch 的恢复触发被旧退避门挡住

`BarPresentDecision::ObserveDeviceGeneration()` 可以清除旧设备的退避（`Bar.PresentDecision.h:294`），但实际调用位于 `Bar.RenderLoop.cpp:7929`，晚于 `:12931` 的退避门。

因此共享调度器已恢复 epoch 并请求所有客户端时，若该请求没有同时改变 `BarAtomic::wait` 的 generation，Bar 仍可能先被旧退避门挡住，直到原来的 retry serial 到达才观察新 epoch。60 FPS 时最多多等约 59 个周期；共享线程本身更慢时墙钟等待相应更长。这是恢复延迟，不是永不恢复。

独立生产 header 检查确认：饱和退避期间先 `CanAttemptPresent()` 会拒绝；先观察新 epoch 就立即允许。获批后的最小调整应让当前 `FrameContext.epoch.generation` 在旧退避门前被观察，同时保留资源重建和全量呈现事务。

### 7. Settings 的同步 Present 会直接拖长其他 UI3 客户端的帧间隔

当前共享线程固定顺序位于 `RenderPipeline.cpp:33`，包含 Bar、StartupPreview、四个 PageControl、Settings、WhiteboardFreeze。客户端在 `:380` 同线程逐个同步调用。

`Setting.cpp:8427` 用 `settingImguiMutex` 包住整个 `settingSession.Resume()`。正常可见会话执行 `:8306` 的 `g_pSwapChain->Present(1, 0)`，并在 `:8313` 返回 Continue，所以每轮都会再进入。其 swap chain 为 discard、两个 buffer（`Setting.Base.cppm:62` 到 `:68`）。Microsoft 的 Present 文档规定该模式下 SyncInterval=1 与一次垂直空白同步，并明确警示 Present 可能等待消息泵线程。

这是一条具体的共享阻塞路径：Settings 的 Present/ImGui 耗时发生在本次 Bar 后面，但会延后**下一次** Bar 回调。不能将“Bar 自身绘制用时正常”当成主栏帧间隔正常。

还存在值得抓栈的锁依赖：窗口线程的 `ImGuiWndProc()` 在 `Setting.cpp:579` 也取得同一 mutex；渲染线程持锁调用可能依赖消息泵的 Present。recursive mutex 只能解决同线程重入，不能自动解决跨线程等待。此处只有 API 允许的风险链，尚未证明现场发生锁反转；真正死锁通常表现为整体冻结，和持续仍有运动并不完全吻合。

边界：隐藏会话在 `Setting.SessionState.h:114` 被标记 release，`RenderSettingFrame():8373` 执行 drain，`:8380` 返回 Idle。不能把已正常隐藏的设置窗口当成永远占用每帧的原因。

### 8. 已排除或降低优先级的猜测

- **没有隐式 Hardware→WARP 降级路径**：`RenderPipeline.cpp:503` 从一开始就明确创建 WARP；`:139`/`:144` 恢复沿用当前 backend。`PrepareBackend/CommitPreparedBackend` 在整个 `Inkeys/` 仅查到声明和定义，未找到产品调用。WARP 本身是软件光栅器，不能把“当前是 WARP”当成这次偶发状态的直接证据。
- **旧 waitable timer 回退不在当前 UI3 帧循环上**：当前节拍是 `RenderPipeline.cpp:335` 到 `:338` 的 `std::this_thread::sleep_until`。整个产品源码内未找到 `Bar.FramePacing` 的 `HighPrecisionWait` 或 `WaitForRemainingFrameTime` 调用，也未找到 `WakeSignal::WaitAndConsume/WaitUntilGenerationChange` 的产品调用。相关 spec 保留了旧入口描述。
- **当前调度 deadline 不累积超期债务**：每轮取当前 `frameTime` 再加 16.67 ms；上一轮过慢时不会额外补一个完整 16.67 ms sleep。忽略 OS 调度超时后，循环周期约为 `max(16.67 ms, 全部回调及控制任务时间)`，不能错误相加为“全部工作时间 + 固定 16.67 ms”。
- **没有按通知次数增长的渲染队列**：`RenderPipeline.cpp:168` 请求用 `fetch_or`，`:173` exchange 合并；WakeSignal 也只存代次。
- **共享 device 恢复失败不会继续向旧 epoch 提交**：`RenderPipeline.cpp:353` 到 `:363` 保留 recovery pending，失败时跳过所有客户端。反复 device loss/recovery 可以表现为间歇停顿，但需要错误/epoch 记录；不是 silent backend fallback。
- **普通 PPT 翻页并未直接在 UI3 回调里调用 COM**：`PageControl.cpp:1556` 的长按 repeat 虽然最终同步调用保存的 callback（`:549`），但产品初始化在 `Inkeys/IdtPlug-in.cpp:703` 到 `:711` 注入的都是 `QueuePptUiBusinessCommand` / `QueuePptUiPositionPersistence`。不能只见 `callback()` 就归因为 COM 阻塞共享线程。

### 9. 已执行的无 GUI 验证

- `research/scheduler-recovery-check.cpp` 直接 include 生产 `Bar.PresentDecision.h`，模拟生产的“观察通知→Tick→退避门→动画推进→失败完成”顺序；时钟明确标成依据 `Bar.FramePacing.cppm:100` 的独立模型，**未声称导入或执行生产 module**。
- 原生 ARM64 `cl.exe` 19.44 / MSVC tools 14.44.35207、Windows SDK 10.0.26100.0；`/std:c++20 /EHsc /utf-8 /W4 /D NOMINMAX`，只有单个研究 translation unit，没有新构建系统。
- 编译退出 0，无 warning；研究程序退出 0，所有 assertions 通过。输出保存于 `research/scheduler-recovery-results.txt`。
- 临时编译产物曾位于该 research 下的 `scheduler-recovery-build/`，执行完成后已删除，仅保留研究源码和输出。没有 HWND、D3D device 或 ULW 调用。该验证说明确定性状态机行为，不代表重现用户故障。
- 未构建主工程；研究阶段没有产品改动，且用户要求批准后实施。

### 10. 获批后建议的最小验证/修复顺序

1. 在共享调度批次记录每客户端耗时、请求 bitmask、FrameResult 和成功呈现计数，独立记录 Bar 原始 dt、实际推进 dt、退避跳过次数与共享 epoch。用秒级汇总/长帧触发，避免逐输入写日志。
2. 修复已知没有本窗口可见变化的光照广播请求：保留最新共享快照，但不唤醒无影响且无其他需求的 PageControl；PageControl 呈现门同时识别有效 damage、动画、强制重建/映射、调试末帧等真实需求。不能只在任意空 damage 时返回，否则会吞掉未分类的业务请求和恢复事务。
3. 给 Bar idle 恢复明确的一次性时钟重置；把 fresh epoch 观察移到旧失败退避门前。二者各有独立状态机回归用例。
4. 保持动画推进与呈现重试的时间语义独立；同类错误的退避不应直接冻结全部业务动画。至于长帧参数时间轴如何赶上墙钟、弹簧如何分步积分，需要先确定获批设计，不能简单删 clamp。
5. 装饰光照更新更新最新需求，但不要无条件清除同类错误退避。该修复与第 4 项应一起设计，避免修完通知后把已有“退避冻动画”放大。

建议回归：

- 两个已预热 scene，primary 保持不变，cursor 旧/新范围只影响 A；断言 B 不 wake、不呈现，A 有正确旧+新 damage。再测 primary 颜色变化、窗位置/缩放、显示/退场、epoch 更新仍能正确请求。
- 真实 Scheduler + 假客户端：Bar 持续 Continue，Settings 假回调模拟一次/持续长耗时，分别统计 callback interval 和 committed interval；不创建 HWND。
- 真实 FrameAnimationClock 接入客户端 idle 状态测试：其他客户端仍活跃时 Bar idle，下一次 Bar 请求也必须排除自己的 idle 间隔。
- 模拟连续 ULW 失败、间歇成功、每帧装饰通知、新 epoch 在退避中到达；分别断言动画推进、present 尝试、成功提交三组计数。

## Files Found

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | 共享 WARP epoch、唯一调度线程、请求位、节拍、恢复 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm` | Client/FrameResult/FrameContext 与请求 mask 合同 |
| `Inkeys/Inkeys/UI/Bar/Bar.FramePacing.cppm` | 动画 dt clamp、Rebase、旧节拍工具 |
| `Inkeys/Inkeys/UI/Bar/Bar.WakeSignal.cppm` | 通知 generation 与历史阻塞等待 API |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h` | 呈现成功事务、失败分类与指数退避 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 实际时钟/退避调用顺序、共享光照发布 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | UpdateRendering 通知入口 |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp` | 多 scene 光照广播、局部 damage 与 scene 绘制 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` | 分页订阅、每窗回调、空 damage 全量呈现 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm` | 可见性/订阅/续帧纯函数 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 同步 Present、ImGui mutex、会话回调 |
| `Inkeys/Inkeys/UI/Setting/Setting.Base.cppm` | Setting discard swap chain 资源配置 |
| `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h` | 可见性/occlusion/epoch 生命周期判定 |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp` | WhiteboardFreeze 同线程呈现与隐藏退出 |
| `InkeysHeadlessTests/frame_pacing_tests.cpp` | helper 的正确醒后 Rebase 测试，未覆盖实际调用时机 |
| `InkeysHeadlessTests/present_decision_tests.cpp` | 既有退避/新需求恢复测试，无动画时钟组合覆盖 |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | 串行顺序、注册注销、节拍、恢复控制现有测试 |

## External References

- [Microsoft Learn: IDXGISwapChain::Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)，访问日期 2026-09-22；discard 模式 SyncInterval 的垂直同步语义，以及多线程中 Present 可等待消息泵的注意事项。
- [Microsoft Learn: WARP Guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)，访问日期 2026-09-22；WARP 是软件光栅器。具体程序选择 WARP 的证据来自当前源码。
- 本地 MSVC 14.44.35207 `<thread>` 的 `sleep_until()` 实现读取指定 clock，通过 `_Clamped_rel_time_ms_count` 和 `_Thrd_sleep_for` 等待，醒后重新检查 deadline。只作为本机 STL 实现参考，不将当前 runner 的睡眠精度当用户机测量。

## Related Specs

- `.trellis/workflow.md`：Phase 1 调查结果必须持久化，研究代理与实施隔离。
- `.trellis/spec/native-desktop/index.md`：共享 UI3 管线及无窗口验证边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md:58`：idle 时钟合同；其中旧 `WaitAndConsume` 接入描述与当前共享 Scheduler 不一致，核心醒后 Rebase 要求仍明确。
- `.trellis/spec/native-desktop/rendering-and-ui.md:115`：共享串行调度器合同；不得在客户端直接 Sleep/等待帧期限，必须区分 target 与共享 device 恢复。
- `.trellis/spec/native-desktop/rendering-and-ui.md:946`：新 device epoch 由唯一管线线程发布，客户端资源在帧前重建。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1404`：Setting 的 ImGui IO/渲染线程边界与会话 mutex。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:31`：日志必须有错误上下文且避免逐帧刷屏。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:157`：D2D/GDI 提交事务，成功后才推进快照。

## Caveats / Not Found

- 这份调查没有故障现场 ETW、调用栈或每阶段耗时，不能把任何性能放大器写成唯一已复现根因。
- 没有假定“重启恢复”意味着必须存在永久累积状态；新证词支持按鼠标、窗口可见性、错误与工作负载变化的路径。
- 所有行号基于读取时的当前工作区；未用 Git 切换到用户引述的历史 commit。其他代理可能继续写研究文档，但本代理没有读取或改写他们的代码/任务文件。
- 容量、viewport 与 ULW 参数的可达失败由主调查负责；遮罩缓存、动态光照几何成本和输入传播由其他专题负责。本报告仅建立这些因素如何传递到共享节拍、慢放与恢复的机制。
- 第 1 节和第 7 节依赖相应分页窗/Settings 可见。只有主栏且其它客户端全部 idle 的场景，需要由 Bar 自身耗时或错误解释，不能套用多窗开销。
