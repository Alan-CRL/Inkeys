# Research: 剩余 GetDC / WARP 成本与 SVG 独立计时接缝

- Query: 冻结 timer/capacity 修复后，在 UI3 单 owner、默认双光、既有视觉与事务合同内，是否还有能以小范围实验验证的重复工作；怎样只测 SVG 阶段而不启用证明路径。
- Scope: internal；只读源码及现有验证结果，没有构建、测试、基准、GUI、Git 操作或源码修改。
- Date: 2026-10-28

## Findings

### 结论与证据边界

没有找到可直接删除、且已有证据能解释剩余约 11ms GetDC 的重复 flush / 主绘制事务。存在一个代码可确定的 CPU 微冗余（同一次 geometry 点光调用重复三次 GetBounds），但没有计量证明它构成当前瓶颈。优先补充独立阶段计时，再决定是否值得做小实验；不能把这些候选当成预算已经达标。

现有 `verification/final-verification.md` 的最终 Release 结果：热态 24/54 组通过 16.67/33.4ms 门槛，compute/draw/GetDC P95 组中位分别 1.356/1.655/11.299ms。该报告已经明确 GetDC 包含前序 D2D 工作提交，SVG 单独占比尚未测出。这里未重采样，不能将异步提交成本直接等同于位图拷贝带宽，不能将两个不同总体的 P95 相加/相减解释成本。

### Files found

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` | 主绘制事务、径向画刷、A8 预模糊缓存、点光和 SVG draw |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm` | 光照常量、渲染接口和资源成员 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 当前 frame 的计算/绘制/GetDC/ULW/ReleaseDC/EndDraw 及 proof owner |
| `Inkeys/Inkeys/UI/Bar/Bar.UI.cpp` | SVG 解析、lunasvg raster、D2D 上传及原有 stage timer |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h/.cpp` | SVG proof 状态、四阶段 timer 和 TLS scope |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm` | 固定数值 FrameDiagnostics、LightingDiagnostics、分段枚举 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | 当前 callback TLS、详细采样门控、timer |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h` | callback/batch 采样与汇总合同 |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp` | 独立 Page / popup 面板透明度层 |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp`、`Bar.RecallAttribute.cpp` | 属性面板 group layer 和资源创建门控 |
| `Inkeys/Inkeys/UI/Bar/Bar.DirtyRegion.h` | 光照边带损伤计算 |

### 1. 已排除：重复 Flush / 主 BeginDraw / 常态重复 Gaussian 创建

- `Bar.Rendering.cpp:462` 的 BeginFrameDraw 只做一次 BeginDraw、dirty clip 和 Clear；`:473` FinishFrameDrawCommands 只 pop frame clip；`:478` EndFrameDraw 才调用一次 EndDraw。
- 主路径在 `Bar.RenderLoop.cpp:9805` BeginFrameDraw，`:12467` FinishFrameDrawCommands，`:12573` GetDC(COPY)，`:12594` ReleaseDC(empty RECT)，`:12602` EndFrameDraw。没有夹入显式 Flush；清空 clip/layer 后 GetDC 是现有 Win7 Platform Update 合同。删除/移动事务边界并非消除一个重复调用。
- `Bar.Rendering.cpp:1830` / `:1843` 的 mask context BeginDraw/EndDraw 是先生成 source，再将 source 作为 Gaussian 输入生成 output；第一阶段结束前 source 还绑定 target，不能合并成一对。geometry 对应 `:2428` / `:2443` 同理。exact mask 的 `:1974` 是另一缓存生成路径，不是每帧主 context 的多余 EndDraw。
- rounded parent 在 `:1721` 命中直接返回；只有 miss 后才走创建，context `:1733`、effect `:1751` 都有成员为空条件。geometry 对应 `:2328` 命中快返。父缓存圆角 `:1868` 上限 24 且 FIFO 淘汰是事实，但没有当前三态热段发生 24 键以上抖动的证据，不能凭缓存大小先改 LRU 或加容量。
- `RenderPipeline.cppm:106` 已有真实 parent hit/miss/create/failure/ms、exact fallback、slice 计数。先按同一 callback/commit 样本判断 create 是否与慢帧相关，再谈创建优化；不能用 aggregate 命中率推定每个慢帧都在创建。

### 2. 光照与 layer：没有发现可无条件删掉的视觉 pass

- `Bar.Rendering.cpp:2542` 使用光椭圆与扩大的几何 AABB 相交测试；`:2553` / `:2557` 分别要求主光/鼠标光可见、强度及相交才创建/更新画刷和提交光照。`:2579` 的基础边框一次、`:2620` 附近每光一次柔光、`:2670` 每光一次硬边承担不同贡献。
- `:2594` 的 `1 - (1 - opacity)^2` 已把原两遍等色 source-over 合并为一次有效 opacity；再删“第二层”会改视觉。不能把主光和鼠标光合并为同一径向梯度：两个不同位置/半径/强度在 source-over 下通常不能用一个径向函数表达。
- 画刷颜色 stop 缓存 `:1189` 命中后只 SetCenter/Radius；`:1204` 最后一个 stop alpha=0、CLAMP。颜色变化确实可能增加键（上限 32 FIFO），但当前报告没有默认三态稳态颜色 churn 的数据。默认主题/笔色不变时不能推定每帧创建 stop/brush。
- Page popup 与根面板已经 `alpha < 0.9999` 才 PushLayer（`Bar.Scene.cpp:680`、`:887`），不应再实现一次“opaque layer 旁路”。`PushPanelOpacity` 已用明确边界包络（`Bar.Rendering.cpp:448`）。
- Eraser `Bar.EraserAttribute.cpp:421` / `:440`、Recall `Bar.RecallAttribute.cpp:281` 无条件 PushLayer 是局部候选，但函数入口先检查 panelVisible（Eraser `:404`、Recall `:272`）。它们不是默认无属性弹窗三态转场的确定负载；不为本任务顺手改它们。省略 alpha=1 layer 仍须核 AA/中间合成和成对 Pop 的像素合同。
- 矩形相交测试不排除光圈完全位于边框内孔、对边框零贡献的情况。`ResolveBarLightBorderDamage` (`Bar.DirtyRegion.h:264`) 已在损伤层排内孔。然而主光/鼠标光默认半径为 480/240 DIP（`Bar.Rendering.cppm:26`、`:28`），薄主栏/按钮通常容不下完整光圈，不能据此承诺默认转场有收益。任意 geometry 的内孔也不能靠 AABB 推断。当前不建议优先试这个 cull。

### 3. 唯一代码可确认的小重复：同一 geometry 三次 GetBounds

非 roundedRect 的 `DrawPointLightFrame`：

1. `Bar.Rendering.cpp:2532` 为 lightBounds 取 geometry->GetBounds；失败则整次返回。
2. `:2644` 为 diffuse draw 再次 GetBounds。
3. 调入 `GetGeometryDiffuseMask` 后 `:2303` 第三次 GetBounds。

这几个调用之间没有 geometry 写入；brush 属性和主 context primitive blend 不会改变该 geometry 的本地 bounds。最小候选是保存第一次的原始 bounds（在 lightBounds 扩张前），下传缓存查找并用于 diffuse destination，保留公开入口的空值/失败语义，不改 transform、cache key、mask、draw 次数。若其它调用者需要该 helper，可保留原有入口或只加内部可选已知 bounds，不大改接口。

局限：这减少 CPU COM/geometry bounds 计算，不减少 D2D draw 命令与 WARP fill 的像素；当前 compute/draw 已显著小于 GetDC，故预期上限很小。应先以 raw-only 数值计数统计当前真实 geometry 调用量、总 GetBounds 次数/时长；若默认 fixture 几乎全走 roundedRect 就直接排除，不做产品修改。

### 4. SVG：最小无副作用阶段计量接缝

现有 stage 钩子已经就位：

- parse：`Bar.UI.cpp:342`、`:547`、`:652`；分别包括尺寸/内容解析入口与 CacheBitmap 的 loadFromData。
- raster：`Bar.UI.cpp:559` 的 lunasvg renderToBitmap，包含内部几何工作；不能将它命名为纯 blit。
- upload：`Bar.UI.cpp:580` 的 CreateBitmap。
- draw submission：`Bar.Rendering.cpp:3027` 附近的 Ui3SvgStageTimer，包围 DrawBitmap；它只计提交 CPU，不包含后续 GetDC 中 WARP 执行的全部成本。

当前 API 不支持计时与 proof 解耦：`Bar.PresentationProbe.cpp:602` 要求非空 svgScope/probe 且 clocksEnabled 才读钟；`:87` 的空 SvgObservationScope 不安装 scope。把一个假的 probe 挂上去会进入 NoteCacheAttempt/ObservedBitmapProof/ObserveDraw、隐藏有限域 requirement 等完整观察分支（`Bar.UI.cpp:420`、`:470`，`Bar.Button.cpp:2272`、`:2314`，`Bar.RenderLoop.cpp:9784`、`:12468`、`:13051`），不能作为“纯阶段采样”替代。

建议只改诊断接缝，单独提交审核：

1. 在 `RenderPipeline.cppm:119` 的 FrameDiagnostics 加固定数值 SVG 阶段累加器：四个 ms、四个 calls、clockReads；没有对象绑定、资源 ownership、容器、锁或 COM 资源。按 callback 清零、随现有 raw callback copy 带出。raw schema 明确本轮新增列/版本，退出 owner 后再输出 CSV。
2. 在 `Bar.PresentationProbe.h:391` / `.cpp:602` 的既有 Ui3SvgStageTimer 加独立 raw-only sink 支持；可采用独立 TLS `SvgTimingScope` 指向当前 callback 数值槽（在 `Bar.RenderLoop.cpp:13039` 获取 diagnostics 后，只有 detailedCaptureEnabled 才安装）。它必须与 `CurrentUi3SvgScope()` 完全独立，不能让 timing scope 非空导致 proof 分支被启用。
3. 复用现有四类 timer 调用点，不改 CacheBitmap needUpdate、语义 tag、proof 策略、hidden requirement、IsDrawVisible/culling、clip、绘制次数、缓存容量。计数在 timer 构造时记录真实调用，不能从证明 lookup/create 计数外推。若 proof 计时与独立 timing 同时开启，读一次起止时钟后分别写各自数值，避免重复读钟和重复计算，原 probe counters 语义保持不变；没有任何 sink 时零读钟。
4. 若不希望加第二 TLS，timer 可读取 CurrentFrameDiagnostics 并以 detailedCaptureEnabled 为唯一授权，写固定字段；需评估 Bar probe 与 RenderPipeline module 的现有包含顺序。这比安装假的 Ui3SvgProbe 更窄。二者择一，不能同时实现两套计时 API。
5. 不在每 SVG/每帧执行 I/O，不加 Flush/EndDraw/CPU readback，不在 timer 内创建字符串/向量/函数对象。既有异常 sink 非 detailed 不应触发新读钟（参照 `RenderPipeline.cpp:303` 的新分段门控）。

Parse/Upload/Raster 缓存命中时应是零 calls。DrawSubmit 高只说明提交/观察路径 CPU 成本；即使低也不能排除 DrawBitmap 的 WARP 执行被计入 GetDC。SVG 调用前的 UTF 转换/替色不在已有 parse 钩子内，不能声称四段覆盖整个 Svg/CacheBitmap 函数；有必要时再加一个包含性的 SVG CPU total 字段，明确与四段重叠、不计入互斥总和。

### 5. 最小红绿 / 像素 / 分段实验

先计量，后微候选；本研究未执行下列实验。

| 实验 | 红 / 绿与判定 |
| --- | --- |
| 独立 SVG 时钟授权 | 无 detailed capture、仅异常 sink、空 diagnostics：clockReads/calls 均零；detailed + finiteObserver=null：四类真实执行能计量，CurrentUi3SvgScope 仍 null，proof/hidden 计数不产生。嵌套 scope/早退恢复原 TLS，双 sink 不重复读钟。用可注入数值时钟的无窗口合同覆盖，不能靠 wall-clock >0 断言。 |
| instrumentation 透明性 | 同一 Release fixture/默认双光、相同入口与成功 serial/source/viewport tuple，timing off/on 对照；资源/事务/geometry 门禁继续成立，BGRA 比较不因诊断新增像素差（期望 bit-exact；出现超出现有 baseline 数值舍入包络必须解释）。对采样额外时钟成本单独量化，不能把 instrumentation overhead 当新回归原因。 |
| 同口径短分段采样 | 使用现有生产 fixture，按六向、100/150/200%、固定 round/cycle 顺序，排除正在编译/其它基准。保留 callback/true commit 分母与 cold/hot。每 callback 输出 SVG 四段/calls、已有 light hit/miss/create/ms/slices/fallback、GetDC、总 batch；优先解释少量最慢实际 commit 样本，再扩矩阵，避免只报整体平均。 |
| 重复 GetBounds | 先证明 fixture 有足够非 rounded geometry 次数；候选只下传原始 bounds，红版 triple calls、绿版 single。要求 draw/mask/cache key/成功事务完全相同且像素不变；比较 Draw CPU 与 batch/true commit/GetDC。若没有稳定全段改善，就撤回微改；不因孤立 COM 时间变小宣称 R4 达标。 |

### 6. 如下一步越过小修边界

以下均不是本轮可默认执行的小改：

- 改左右对称 storage/source 锚点、source origin 或 backing 布局以继续缩小互操作面积：需要重新证明负原点/多 DPI/根动画/快速反转/失败重试/旧 bounds erase/ULW source 包含，不能只看新 target 面积。
- 将共享 WARP client 分批提交/交错 GetDC 或跨 callback 分阶段呈现：会改变单 owner 批次与成功快照的事务时序，需考虑跨 client 光照一致性、过期状态呈现和 re-request/failure；不是换个顺序就安全。
- 换呈现后端/互操作接口、增线程、默认硬件、画质/光照降级、缓存最终动画帧：范围和兼容性均超出当前 minimal diff/产品合同。
- 为拆分 GetDC 而在前面强加 Flush/EndDraw/读回会移动或增加同步成本，最多作为显式隔离实验，不能当无副作用计量接缝，更不能用新 GetDC 数字下降冒充总 batch 改善。当前没有必要先做它。

## External references

本主题只读内部源码与验证结果，未联网、未引入第三方版本/工具链变化。沿用主会话已核对的 Microsoft 结论：D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS 对 WARP 无效；本研究未重复验证。没有建议此 flag，也没有假定禁用 D3D 内部线程。若采用新的 interop/clip 实验，需要单独核对对应 API 官方合同并记录链接。

## Related specs

- `.trellis/workflow.md:358`：research 持久化；本次只写当前 research 文件。
- `.trellis/spec/native-desktop/index.md`：UI3 默认入口、单一共享 WARP owner 与各 client 所有权。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1132` 附近：一对主 Begin/End；GetDC 前 clip/layer 清空且不重复 Flush；COPY、ReleaseDC empty RECT；动态点光与 A8 几何缓存合同。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：TLS 当前 callback、detailed 门控、固定原始数值与 successful commit 分母，不把软件戳称光学可见。

## Caveats / Not Found

- 没有执行性能采样，所有未测候选均未证明收益；没有找到一处可直接保证剩余预算通过的安全小修。
- 默认薄主栏内孔 cull、属性面板 opaque layer、颜色缓存 churn 均不能由“代码可能发生”升级为“当前三态已发生”。
- 没有恢复已撤回的 mask-slices 方案，也没有更改 timerPeriod/默认 backend/光效/最终帧缓存。
- 文件行号是本次只读快照，其他 worker 后续修改可能移动行号；函数名/相邻逻辑用于定位。
