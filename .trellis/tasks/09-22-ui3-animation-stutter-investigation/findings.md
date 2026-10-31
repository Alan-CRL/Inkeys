# UI3 动画卡顿调查结论（等待实施批准）

基线：`bugfix/animation` / `94e07b2599adab9de4286aa5fb7526e2c1c681f6`，2026-09-22。产品代码、既有测试和工程配置未修改。

## 结论

已经定位并用当前真实动画 module 验证一个确定的动画目标冲突，同时确认两条不依赖 API 失败的动态光照工作量放大路径。它们比“缓存容量有限，所以光影计算没有问题”的推断更具体。没有故障现场的帧耗时或错误记录，仍不能把其中某一项宣布为用户低概率严重卡顿的唯一根因。

用户补充：问题来自其他用户，重启后可能消失；关闭动态光影据称改善；同一次运行可能时快时慢。因此本调查不再以“慢状态必须持续到重启”为前提，也不把历史容量峰值列为默认主因。

最值得先修的路径是：**收起时重复覆盖描边目标 → 描边与面板缩放失去同步 → 本来应稳定的柔光遮罩键变化**。另一条独立路径是：**鼠标光移动 → 无影响的可见分页 scene 也被唤醒 → 空 damage 回退整窗重绘 → 多个客户端在同一线程串行消耗时间**。长帧限幅和失败退避会进一步改变动画的墙钟速度。

## F1：颜色块关闭动画每帧重启描边，已由真实 module 验证

源码链：

- `Bar.RenderLoop.cpp:3126,3131`：颜色块 1 的选中与未选中分支都 `ft.SetTar(1.0)`；颜色块 2–11 同样，至 `3416,3421`，共 22 处。
- `Bar.RenderLoop.cpp:3063,4196`：关闭时同一帧再提交 `ft.SetTar(60/370)`。
- `Bar.Animation.cppm:784,801`：不同目标重置 `startV/progress`。
- `Bar.RenderLoop.cpp:1325,4250`：进度被清零，因此每帧又套用批次剩余时长与 Back 续段。
- `Bar.Animation.cppm:419`：Back 的续段会从剩余段起点重新采样，边框反复停留在其初段，无法跟随面板原轨迹。

从当前源码重新编译 `Bar.Metrics.cppm`、`Bar.Animation.cppm` 和 `Bar.Animation.cpp`，由 [animation_target_probe.cpp](research/animation_target_probe.cpp) import 实际模块并执行生产调用顺序。无产品窗口、无 D2D。结果见 [原始输出](research/animation-target-probe-results.txt)：

| 0.4 s 关闭过程、60 Hz | 面板比例 | 现有描边宽度 | 描边 / 面板比例 |
| --- | ---: | ---: | ---: |
| 第 18 帧 / 75% | 0.776140 | 1.088690 | 1.402698 |
| 第 21 帧 / 87.5% | 0.526916 | 1.168635 | 2.217876 |
| 第 23 帧 / 95.83% | 0.297856 | 1.226507 | **4.117781** |
| 第 24 帧 / 结束 | 0.162162 | 0.162162 | 1 |

单目标对照的归一比例始终为 1，最大数值误差 `5.55e-16`。这是动画状态的实际执行结果，不是仅凭阅读推断；不等于运行了整个 RenderLoop 或复现了用户 GUI 卡顿。

光影后果：归一描边进入 `GetRoundedRectDiffuseMask()` 的四分之一像素键。11 个普通色块共享同一组键；不能按 11 倍计算 Gaussian。相同轨迹在 zoom=1 时从已预热键新增 6 个父遮罩键，zoom=1.3 时新增 7 个；单目标对照新增 0 个。固定同样时序第二次关闭、且这些键未淘汰时可全部命中，所以这是动作/时序相关的冷创建风险，不是每次永远 cache miss。详见 [光影报告](research/lighting-cache-audit.md) 与 [输入报告](research/input-animation-audit.md)。

最小修复：删除前面的 22 个中间 `ft.SetTar(1.0)`，保留 `4196` 的统一几何目标、`4250` 的批次同步以及可见性/换边的一次性重建；不改成逐帧 `SetDirect`。

边界：正常关闭最终会收敛。该缺陷能确定导致局部曲线和缓存键异常，但是否足以拖慢整栏，仍需要真实光影阶段耗时。

## F2：缓存命中后仍可能有大量光影提交；整图加速与当前平移不兼容

- `Bar.Rendering.cpp:1759-1764`：exact mask 仅允许严格 Identity，非零平移也立即回退。
- `Bar.RenderLoop.cpp:9750-9769`：主栏用 `-capacityOrigin` 平移把布局映射到实际 target；常态非零，没有在 Shape/PointLight 内临时恢复 Identity。
- `Bar.Scene.cpp:2337-2338`：PageControl scene 也有 outset 平移，且其路径未推进 exact mask 帧序号。
- `Bar.Rendering.cpp:1977-2014`：圆角与缓存半径相同走 3×3 分片，否则走 5×5；每个有效片调用一次 `FillOpacityMask`。例如 zoom=1.3 的 `4*1.3=5.2` 与量化半径 `5.25` 不同，即使几何静止也会走 25 片。

将当前两个绘制函数的原始函数体提取进 fake-context 计数程序并用 ARM64 编译执行，15 个受光矩形的调用数为：

| 输入 | 父遮罩几何键 | 柔光 FillOpacityMask 调用 |
| --- | ---: | ---: |
| zoom=1 | 1 | 135 |
| zoom=1.3 | 1 | 375 |

15 个对象的布局例子为展开绘制属性栏中 11 个普通色块和 4 个可见笔型按钮，鼠标位于面板中心。它们可以共用一个父遮罩，且第三光专属控件在光照不到时被空间相交测试裁掉。因此鼠标位置改变会改变实际绘制量；不能把“缓存命中”理解为“这一帧没有昂贵工作”。计数不代表完整主栏帧时间，也不代表每次鼠标移动都创建 375 个资源。

报告与原始结果：[光影报告](research/lighting-cache-audit.md)、[函数计数](research/lighting-slices-probe-results.txt)。

恢复 exact 对整数平移的支持可能有价值，但还需验证设备像素对齐、分数平移、缩放/弹性形变、半径量化和现有内存预算。它不是第一批可以仅删除矩阵判断的改动，也不承诺恢复后所有 25 片都能变成 1 片。

## F3：鼠标光变化唤醒无关的可见分页窗，空 damage 反而全窗呈现

`Bar.RenderLoop.cpp:7833 → Bar.Scene.cpp:1893-1916 → PageControl.cpp:819,1512-1527 → 955-959,973-978,1037-1055`。

scene 已计算旧/新光照与边缘的相交区域，无相交可以得到空 damage；广播仍无条件收集所有订阅 scene 的 wake。可见 PageControl 收到请求后先全 Clear/Render，空 damage 再回退整窗 ULW。这是确定的无效工作，不需要设备报错才能触发。

影响受可见性限制：最多四个分页 HWND；白板常用底部两个；全部隐藏并完成退场时不适用。调度器按位合并请求，不是无限输入队列。所有这些客户端与 Bar 共用一个串行线程，因此额外窗口耗时延后下一次主栏回调。

第一批可局限在 `Bar.Scene.cpp`：让私有光照处理返回“本次变化是否贡献实际 damage”，仅为 true 的 scene 发 hooks；保留最新快照、旧光擦除、primary 变化、首次订阅和其他业务唤醒。不要拿旧的 pendingDamage 是否非空，或矩形前后是否相等，替代“本次变化贡献”的判定。暂不泛化 PageControl 的呈现门。详见 [设计复核](research/pagecontrol-fix-design-review.md)。

## F4：时钟和恢复确实放大慢放、跳变，但不能提供第一个慢帧的根因

1. `Bar.FramePacing.cppm:100-107` 丢弃超过 50 ms 的时间：100 ms 帧间隔只前进 50 ms，约半速。现有 spec 明确规定上限，不能直接删除所有 clamp。
2. 更严重的组合在 `Bar.RenderLoop.cpp:968,12931-12933`：退避回调先 Tick，随后不推进动画就返回，领取的时间被丢掉。生产 `Bar.PresentDecision.h` 模型在持续同类失败、无新通知时，2.0667 s 仅推进 0.1333 s；封顶后约每秒仅推进 16.7 ms。模型全部失败，屏幕没有成功提交，不能写成“屏幕每秒显示 1/60 速度”。
3. 鼠标通知增代次会清退避：持续移动时又可能每帧尝试失败；停下后积累退避。两种分支互斥，不能同时相乘为“鼠标导致 60 倍退避”。成功提交立即清退避。
4. 当前 `Rebase()` 在 `Bar.RenderLoop.cpp:12865` 返回 Idle 前执行，下一次被唤醒没有 Rebase，闲置后第一帧仍会领取 50 ms；可解释首帧跨步，不能解释持续慢放。
5. fresh epoch 在 `7929` 才观察，晚于旧退避门，设备已恢复也可能先额外等待约 59 个调度周期。

这些恢复修正应一起设计动画推进、呈现退避和装饰通知的合同；只禁止光影清退避，会把现有“退避暂停动画”问题暴露得更明显。第一批记录数据，后续作为单独恢复改动处理。[完整恢复报告与量化](research/scheduler-recovery-audit.md)。

## 前序四个候选的复核

| 候选 | 当前结论 |
| --- | --- |
| ReleaseDC(nullptr) | 确认只读 DC 被声明为全量修改，应改空 RECT；但 API 语义不能证明每帧发生固定整图拷贝。 |
| capacity 只扩不缩 | 确认；还存在独立的 `displayCapacityZoom` 历史峰值，单缩 target 可能立刻又扩回。新证词使其作为唯一根因的优先级下降。 |
| 50 ms clamp | 确认慢放放大机制，非长帧来源；恢复门还有更严重的逐回调丢时间。 |
| 光影通知打断退避 | 确认，仅错误分支成立；请求按位合并，不是无限渲染队列。 |

新增本机 API 对照使用当前同类 WARP+D2D+GDI-compatible BGRA target，固定 256×128 脏区，只变 target 面积及 ReleaseDC 参数；每组预热 20 帧、采样 80 帧，以 nullptr/empty/empty/nullptr 顺序运行。1600×1200 到 4800×3600（面积 9 倍）各组总 P50 约 `0.09–0.17 ms`，没有稳定的容量或空 RECT 收益。见 [原始结果](research/dc-interop-probe-results.txt)。

该对照**没有 HWND、ULW 或真实光影**，只表明在本机不能由前两项直接推出百毫秒卡顿；不能据此排除用户机器的 ULW、其他 backend 或复杂绘制 flush 等待。仍可做空 RECT 语义修正，但不得宣传为已经解决主因。

API 核验：[ReleaseDC](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1gdiinteroprendertarget-releasedc)、[GetDC](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1gdiinteroprendertarget-getdc)、[Microsoft 分层窗口示例](https://learn.microsoft.com/en-us/archive/msdn-magazine/2009/december/windows-with-c-layered-windows-with-direct2d)。`GetDC` 会 flush；空 RECT 描述 GDI 没有修改源 DC，与 D2D dirty 或 ULW dirty 不是同一概念。

## 其他已记录风险与反证

- **capacity/viewport 集成缺少包含保证**：`9172-9179` 确保当前内容容量，`9276` 之后才求完整预留包络，`9670-9682` 生成 viewport/source，没有最后的 target 包含检查。生产 helper 探针既能构造负源坐标，也能在居中展开端点模型得到 capacity.right=1573、viewport.right=1753。后者由生产居中区间 helper 的独立极值组合得到，尚未执行整个 GUI 动画或验证 Windows 对该 ULW 的返回；属于边界风险，不能写成已复现的 ULW 失败。见 [几何探针](research/geometry_probe.cpp) 和 [输出](research/geometry-probe-results.txt)。
- 几何已经稳定但光影还活跃时，viewport 最终收缩仍等待 idle；这与永久 capacity 峰值是两种不同保留行为。
- 原始 bitmap/mask 缓存按数量有界，但父遮罩不是按总字节限额；大小增长或 same-epoch resize 会丢弃设备缓存。没有证实无界泄漏，也没有证实同坐标持续创建 Gaussian。
- optional exact 失败会永久回退到分片，但因当前常态本来就被矩阵 gate 拒绝，不能把该 latch 当作目前主栏快慢切换的首因。
- Settings 可见时同步 `Present(1,0)` 处于共享线程，可能拖长 Bar 周期；正常隐藏后不持续回调。记录每客户端耗时即可区分，不盲改 Present 或线程模型。
- Raw Input Grace 为固定截止时间，移动不续期；没有找到输入队列无界、鼠标修改全局速度或主光锁内长期等待的证据。
- seqlock 读取屏障与 `dt=0` 直接 Finish 是独立正确性风险，未证明与本故障相同；不混入第一批修复。
- 初始化默认 WARP，恢复保留 backend；没有找到隐式 Hardware→WARP 降档。旧 waitable timer helper 不在当前 UI3 调度路径，不应把其历史回退当根因。

## 批准后第一批范围与现场判定

第一批仅修 F1 目标冲突、F3 无效广播、只读 DC 的 ReleaseDC 参数，并增加限频阶段诊断。保留动画速度、光照质量、现有分片回退和各窗口成功呈现事务。完整范围与后续条件见 [design.md](design.md)、[implement.md](implement.md)。

最小诊断字段：共享批次各客户端耗时/请求原因；Bar 原始 dt/实际推进 dt；绘制、GetDC、ULW、ReleaseDC、EndDraw 与呈现锁等待；四阶段错误/成功提交间隔；target/capacity/viewport/source/zoom/epoch；父遮罩 miss/create 与分片调用数。只做内存计数，按秒汇总或长帧触发且限频，复用现有异步日志；不逐鼠标 packet 写日志，不增加上传。

现场判定：若新遮罩创建/分片绘制或分页额外呈现主导长帧，则对应 F1/F2/F3；若提交失败主导，则转 F4 和 source/viewport；若 Bar 很快但下一次回调很晚，则看其他客户端和调度；若仅历史容量异常而同场景耗时随容量变化，才进入容量回收实验。一次成功构建或某项探针通过不等于原始偶发故障已经消失。
