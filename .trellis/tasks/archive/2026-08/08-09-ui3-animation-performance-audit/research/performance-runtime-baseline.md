# UI3 性能、光影与调度基线

## 证据规则

- “已确认”表示可由当前源码或指定提交直接复核。
- “静态风险”表示控制流或错误路径值得修复/验证，但没有 GUI 运行证据时不得宣称用户可见故障已经复现。
- “待测假设”不得直接驱动大改；Phase 0/2 必须用 headless measurement、固定计数或真实源码边界证实。

## 已存在且必须复用的优化

### Device、整帧租约与恢复

- UI3 当前默认使用 WARP epoch；Hardware 只有 prepare/commit API，静态搜索未发现产品调用点：`IdtD2DPreparation.cpp:190-200`。
- UI3 帧租约由一个 mutex 串行化；Interactive 等待，Cosmetic 在交互 waiter 或锁竞争时跳帧：`IdtD2DPreparation.cpp:98-113`。
- generation 变化时在租约内重建 context、target 与 GDI interop；新资源完整成功后才替换，device-bound cache 统一清理：`Bar.Main.cpp:3641-3682,9705-9724,1297-1343`。
- target 是显示器尺寸减一行的 GDI-compatible premultiplied BGRA bitmap；呈现链为 D2D -> `GetDC(COPY)` -> `UpdateLayeredWindowIndirect`：`Bar.Main.cpp:3652-3673,12399-12418,16540-16545`。

### 光影 cache 与裁剪

- gradient brush 按颜色和 light source 缓存，位置/半径只更新属性，容量 32；solid brush 为单对象改色复用：`Bar.Main.cpp:1855-1936`。
- rounded 与 geometry diffuse mask 各缓存 24 项，key 量化到 0.25 px；Gaussian effect 和 mask device context 长期复用：`Bar.Main.cpp:2313-2475,2563-2726`。
- rounded mask 使用九宫格，动画期间单独保持 Gaussian 外扩；cache miss 才生成 A8 预模糊 mask，失败后本 device session 停用 diffuse，不回退到逐帧实时 Gaussian。
- 光源先与扩展后的控件 bounds 做圆相交测试，未命中时不取 brush、不画 hard/diffuse：`Bar.Main.cpp:2782-2815`。
- PointLight dirty 外扩已包括 `6 * zoom` 和 3 px 抗锯齿余量：`Bar.RenderingAttribute.cppm:13-51`。
- 旧/新可见边界并集同时用于 D2D clip、透明 Clear 与 ULW `prcDirty`：`Bar.Main.cpp:9901-9930,12373-12413`。

### SVG、PNG、几何与文字

- SVG 动画通常缩放已有 raster bitmap；只有颜色变化、稳定尺寸变化或放大超过 1.35 倍时重栅格：`Bar.Main.cpp:3195-3257`。
- PNG 解码像素跨 generation 保留，只重上传 D2D bitmap：`Bar.UI.cpp:474-522,576-587`。
- superellipse 已有局部 path/translated geometry 单槽 cache：`Bar.Main.cpp:3007-3103`。
- Fine Dial 文字有 64 项 layout cache：`Bar.Main.cppm:452-453`、`Bar.Main.cpp:2248-2311`。

不得把 Phase 3 做成“重新增加上述 cache”。必须测 key 稳定性、hit/miss、create/evict、动画终点 miss 和剩余成本。

## Idle、wake 与 frame pacing

- 正常 idle 不持续 60 FPS。若没有 layout/animation、第三光、render-once 或 debug 请求，render thread 进入 `BarAtomic::wait.WaitFalse()`：`Bar.Main.cpp:9679-9688,12439-12462`、`Bar.Atomic.cppm:17-48`。
- `UpdateRendering()` 更新共享状态后执行 `BarAtomic::wait.Store(true)`：`Bar.Main.cpp:15937-15952`。
- `renderOnceFlag` 在 frame decision 前用 `exchange(false)` 消费；`sustainFlag` 会让拖动期间持续以 Interactive frame 重绘：`Bar.Main.cpp:9679-9695,16352-16401`。
- `HighPrecisionWait()` 在大于 2 ms 时睡到剩约 1.5 ms，再用 QPC + `YieldProcessor()` 自旋；不足 2 ms 时全部自旋：`Bar.Main.cpp:1246-1291`。
- `BarUiDebugModeEnabled` 会持续渲染并强制全屏 dirty，显示的 FPS 还包含锁帧等待，因此不能作为正常路径基准：`Bar.Main.cpp:9687-9693,9903-9904,12457-12461`。

## 当前 drawing-aware 行为

- Draw2 的 RAII guard 只发送配对的 activity 通知：`IdtDrawpad.cpp:541-554,578-601`。
- Bar 窗口线程只在 Started 时做一次检查；若光标位于 UI3 接收窗外，则让第三鼠标光 Dormant。第一主光、颜色过渡与普通动画没有 drawing gate：`Bar.Main.cpp:16194-16231,16469-16486`。
- Ended 只把 activity count 安全减回零，不重新激活第三光，也不请求渲染：`Bar.Main.cpp:16477-16486`。

归档 `07-26-ui3-lighting-render-performance` 中“整段绘图 quiet/150 ms 恢复”的旧设计已经被后续实现修正，不是当前合同。新任务不得重做 Draw2 调度，也不得重新引入动画期间关闭第三 diffuse 的临时方案。

## 静态控制流风险

以下项目需要在 Phase 0/2/5 验证并在证据充分时修复，不能在实施前把“静态可疑”写成已复现用户故障：

1. `PrepareFrameLighting()` 在取得 Cosmetic 租约前已消费 cursor serial、推进 fade 并更新 animation state；租约失败后直接 `continue`，没有 pending-present 标记。最后一次第三光更新可能没有后续 wake 来呈现：`Bar.Main.cpp:1823-1852,9679-9702`。
2. idle wait 的谓词只看 `AtomicWaitClass::value`；退出只改变外层 `offSignal`，当前路径未见同时唤醒 wait。线程 detached，退出方最多等 Rendering 状态 5 秒：`Bar.Atomic.cppm:33-48`、`Bar.Main.cpp:3892,12439-12442,16511-16529`。
3. `original = current` 在 `GetDC` / ULW / `EndDraw` 完整成功前提交；ULW BOOL 和 `ReleaseDC` HRESULT 未检查，GetDC 失败只记录。失败后下一帧可能缺少真实旧边界：`Bar.Main.cpp:12373-12434`。
4. 只有 `EndDraw == D2DERR_RECREATE_TARGET` 会清 generation；GetDC/ULW 失败没有同等级恢复。generation 重建失败在活跃状态下仍可能逐帧重试：`Bar.Main.cpp:9706-9721,12393-12434`。
5. `BarFormatCache::GetFormat()` 在确认 `CreateTextFormat` 成功前调用 `newFormat->SetWordWrapping()`；`Word()` 也未先检查 `textFormat`：`Bar.Format.cppm:64-80`、`Bar.Main.cpp:3384-3421`。
6. `AtomicWaitClass` 的 bool handoff 需要覆盖“producer 在 consumer 清零窗口内再次 Store(true)”以及 shutdown 场景；应以事件风暴测试确认是否存在 missed wake，而不是只凭常规路径推断正确。

## 待测性能假设

按优先级调查：

1. 第三光或单个属性动画仍从 `Rendering()` 顶部执行完整 target/layout 和全量动画扫描，直到 `Bar.Main.cpp:9679` 才决定实际渲染原因；“每帧做多少无关工作”可能高于光影本身。
2. 全 widget scan、57 个私有动画值和多次 `IdtAtomic` load/store 的成本，以及多字段状态快照的一致性。
3. `HighPrecisionWait()` 的 1.5 ms spin tail。理论成本不能代替 QPC + `QueryThreadCycleTime` 实测。
4. WARP 全屏 target、`GetDC(COPY)`、ULW/DWM copy 的同步成本。`prcDirty` 对系统 copy 的收益必须分段计时。
5. color picker footer 在活跃循环中多次 `MeasureText()`，每次创建 `IDWriteTextLayout`：`Bar.Main.cpp:3427-3458,9539-9561`。
6. superellipse 单槽 exact-float cache、text-format 每帧淘汰策略和 Raw Input Grace 期的剩余 CPU 成本。
7. 容器 lookup 仅是候选；只有测量显示显著，才考虑 cached reference 或 enum-indexed storage。

## Headless measurement seam

- curve、timeline、Value/Pct/Color/keyframe、target no-op 和 interruption 可在无窗口 harness 中直接测正确性与吞吐。
- 构造与真实 map 规模一致的 widget set，分别测 active/inactive/full-scan 与候选 active-list；保留实际对象数和属性数。
- `PrepareFrameLighting()` 不直接调用 D2D，可用固定 dt、cursor serial 和合成状态做纯状态 benchmark，并测试 Cosmetic skip 的最终呈现标记。
- `BarRenderingAttribute` 的 rect union/outset 与 mask key 量化应抽为纯函数做 property test。
- 可用现有 WARP 初始化创建离屏 bitmap target，无 HWND 测 cold/warm `Shape/Superellipse/SVG/mask`；不得把离屏结果冒充 ULW/DWM 数据。
- `HighPrecisionWait()` 单独执行多轮 QPC + `QueryThreadCycleTime`，记录 overshoot、sleep、spin 与线程周期。
- `AtomicWaitClass` 做 request coalescing、producer storm、consumer clear-window 和 shutdown 测试。
- 当前静态搜索未发现 UI3 Bar/lighting/wait 的自动化测试；Phase 0 必须记录新增 harness 的可删除边界。

真实 `UpdateLayeredWindowIndirect`、主观动画流畅度、第三光视觉和 Inkeys2 同机对比需要用户最终手工验证。自动执行阶段必须继续 Phase 0-5，并把这些项目标成 `MANUAL / NOT VERIFIED`，不得伪造数据或因而暂停。
