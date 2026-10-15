# Research: Draw3 帧流程、呈现与低扰动性能采样

- Query: 调查 DrawingController 正常 Run/PresentFrame、Renderer、TransparentPresentation、GraphicsInitialization 与 RuntimeMetrics；确定最小采样点、阶段嵌套、成功呈现证据、GPU timestamp 和默认关闭门控。
- Scope: mixed（产品源码/现有 Spec + Microsoft 官方 D3D11/DXGI/QPC 文档）
- Date: 2026-10-14

## Findings

### 结论

1. 可以复用 RuntimeMetrics 的 opt-in Session、固定容量、真实 Present 返回点与内容身份链，但现有数据不足以覆盖逐阶段性能和 Move/Up 延迟。旧四成本段明确是 inclusive；不能相加成总成本或直接拿 `geometryRasterSubmit` 判定 GPU 慢。
2. 最有价值的新增阶段是输入消费/模型处理/几何、Renderer 上传 Map/CPU copy/Draw 提交、backbuffer 合成、ULW Copy 提交/同步 Map/CPU rows/UpdateLayeredWindow，以及帧外 deadline/idle wait。ULW 的同步回读很可能把前面尚未完成的渲染成本集中到 Map 上；这是候选归因机制，尚不能据此诊断兆芯现场。
3. GPU timestamp 可作为稀疏异步补充；必须固定 query slots、延后读取、DONOTFLUSH、无忙等、无额外 Flush。GPU 命令流区间与纯 shader 执行时间不同，CPU 提交空隙也可能进入区间。
4. 性能开关应独立于已有启动/像素/光标/Win7 着色器诊断。仅为了拿初始化日志启用现有 StartupEnvironmentDiagnosticsEnabled 会同时增加运行期像素和 GPU binding 检查，需要拆分初始化日志与重诊断的 gate。

### Files found

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` / `.cppm` | 正常帧循环、模型调用、L0/L1/L2、内容 proof、Present 和等待；实际入口是 `Run()`，未发现名为 `normalRun` 的符号 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp` / `.cppm` | 独立设备借用、资源、shader、上传能力、size-dependent 与设备资源释放 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp` | 普通点/Shape/Highlighter 动态 buffer Map、上传与 Draw；cursor 重诊断 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererSnapshot.cpp` | trusted L2 snapshot 与平移 fallback 合成 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` / `.cppm` | DComp/ULW、请求输出身份、同步 GPU 回读和软件 Present observation |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp` / `.cppm` | Hardware-first/WARP-fallback、FL11.1→11.0 retry、DXGI adapter/factory |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp` / `.cppm` | QPC 工具、初始化环境日志、adapter/driver、稀疏 Debug frame 和输入日志 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RuntimeMetrics.cpp` / `.cppm` | schema2 有限样本、inclusive CPU/wall、Down landing、phase、离线 JSON |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` / `.h` | Session 启用/创建/预算/封口/导出与软件初始化 owner |

### 当前帧与互斥/嵌套关系

- `DrawingController::Run` 起于 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:7719`。预热 Laser 与 Shape 分别在 `:12572` / `:12581`，普通 while 在 `:12605`，帧起点在 `:12636`。尺寸、工具、输出、恢复、命令与导航处理可以发生在几何大段之前。
- 每帧 Begin (`:12637`) 与 `MetricsLoopExit` (`:12644`) 处理 break/continue。闲置/静止 Laser Hold 在 `:13221` 进入阻塞，`Finish()` 在 `:13231` 先执行，等待不混入 render wall。
- 唤醒后 Down 初始化可能发生在已 Finish 的帧之外；pending 成本随下一 Begin 携带，`priorIngressWallMs` 与 `frameCpuExcludesPriorIngress` 合同见 `:3895` 和 `Draw3.RuntimeMetrics.cpp:1111`。旧 stage 总量可能包含帧起点之前的工作，不应以简单 `frameTotal-sum(stages)` 推算 remainder。

| 旧字段 | 精确位置 | 口径和嵌套 |
| --- | --- | --- |
| ingress | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:9860`, `:9869`, `:9877` | command/dequeue/Down 初始化；ingressDepth 避免同类 scope 重复累计；可包含模型 Reset/Update 和 observer |
| modelPrediction | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2343` | 每次 Reset/Update/Predict wrapper；既可位于 ingress，也可位于 geometry；不是整个建模周边几何处理 |
| geometryRasterSubmit | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:13307` → `:14285` | 消费 snapshot、补停笔、预测、稳定 L1、L0、Up commit/history、Laser、cursor/dirty/proof；内部包含 ingress/model/particle 子段 |
| composite | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:14313` → `:14378` | backbuffer 基础层、particle、Laser、tip、cursor 与 proof freeze；包含 Laser particleDraw 子段；普通成功路径在 Present 前结束 |
| presentWallMs | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:7549` | `presentation.Present()` 整个调用；ULW 含同步 readback+rows+ULW，DComp 含 Present1 和已有日志；observer 在返回计时后 |
| frame.wallMs | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:14447` | frameStart→呈现后状态更新；不含末尾 deadline wait；另有 `threadSpanWallMs`，端点略有不同 |
| activeFrameIntervalsMs | `Inkeys/Inkeys/Drawing/Draw3/Draw3.RuntimeMetrics.cpp:710` | 活动 frame start 差值，含帧间等待；不是实际成功呈现间隔；当前只在仍有 physical contact 且非 eraser idle 分支记录 |

`MetricsStageScope` 构造/析构见 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2290`：诊断开启时每个端点读 QPC 和 GetThreadTimes，同一段 CPU/wall 一起累计。`Draw3.RuntimeMetrics.cppm:57` 和 `Draw3.RuntimeMetrics.cpp:232` 已声明父段含子段，不能相加。GetThreadTimes 是 render owner CPU，不能代表驱动线程/WARP worker 总 CPU，也不能通过 wall-CPU 精确区分 GPU 与调度。

### 建议最小采样点（新字段，不重定义旧数组）

| 层级/阶段 | 合适的真实边界 | 记录要点 |
| --- | --- | --- |
| frame.totalWork | `Draw3.DrawingController.cpp:12636` → `:14447` | 原 frameStart 和结束可复用；另存 frame/start/end 原始 QPC、帧身份、实际 result |
| control/recovery/resize/navigation | while 中 `:12663` 恢复与已有命令/导航分支 | phase/reason 与累计独立 span；恢复/重放/首次资源创建必须可筛出 |
| input.consume/process | `:9860`, `:13323` consumeLatestSnapshot | 登记实际消费 identity/sequence；队列等待与处理分开；模型子段仍标 inclusive |
| model.reset/update/predict | 现有 `MetricsModelCall` `:2343` | 拆三类计数/耗时；不要每次都增加 GetThreadTimes，QPC 细段+帧级 CPU 足够 |
| geometry.prepare/live/stored | `:13536` active loop，`:13770` predicted/real→几何，`:14077` 重建，`:14145` L0 | 业务模型/几何/提交交错，采用带 parentStage 的命名 span 或记录 exclusive 自身耗时；不得仅减一个全局 model 段 |
| renderer.upload.map | `Draw3.RendererPrimitives.cpp:74`, `:79` 等实际 Map | ink/constant/Shape/Highlighter map 分类；保存 HRESULT、Map 类型、调用数/字节数；只计实际调用 |
| renderer.upload.cpuCopy | `Draw3.RendererPrimitives.cpp:75` → `:77` | QPC 围绕 memcpy，不引入新 copy/scan；以已有 batchCount 记录字节数 |
| renderer.draw.submit | `Draw3.RendererPrimitives.cpp:89` 后绑定与真实 Draw | wall 是 CPU+driver 提交成本；记录 draw/instance/batch 数和 NO_OVERWRITE/DISCARD 次数 |
| snapshot.refresh | `Draw3.DrawingController.cpp:14304` | 当前落在 geometry Finish 与 composite 开始之间，是未覆盖成本；单列 |
| composite.base/overlays | `Draw3.DrawingController.cpp:14313` 之后真实基础层和 cursor/Laser | 合成与 overlay 子段可嵌套；父总量保留 |
| present.prepare | `Draw3.TransparentPresentation.cpp:476` → `:528` | EnsureWindowDib/尺寸/dirty clamp；记录 requested/effective dirty、full 与 target |
| ulw.copySubmit | `Draw3.TransparentPresentation.cpp:534` | `CopySubresourceRegion` CPU 提交时间；不能称 GPU copy duration |
| ulw.readbackMap | `Draw3.TransparentPresentation.cpp:537` | 同步 Map 的 wall 是已有 GPU 工作+copy+driver/调度的阻塞；保存 HRESULT |
| ulw.cpuRows | `Draw3.TransparentPresentation.cpp:544` → `:554` | 保留原 CopyAndInspect 的非诊断模板，只计现有行拷贝/alpha处理+Unmap，不额外扫描像素 |
| ulw.update | `Draw3.TransparentPresentation.cpp:591` | 更新时间；Win32 error 在 `:592` 已锁存，计时不得先调用其它 API 破坏 GetLastError |
| dcomp.present1 | `Draw3.TransparentPresentation.cpp:1038` | 精确 actual API attempt/HRESULT，与 empty dirty 的无调用 S_OK 区分 |
| frame.deadlineWait | `Draw3.DrawingController.cpp:14493` → `:14495`，动画 `:14518` | 记录 requested budget、实际 wait、wake 原因；不并入 work |
| frame.idle/holdWait | `Draw3.DrawingController.cpp:13230` → `:13294` | 下次激活重置连续序列；Hold idle 与真正长帧分开 |

上表省略路径前缀的 `.cpp` 均位于 `Inkeys/Inkeys/Drawing/Draw3/`；行号为研究时当前工作区。细阶段应只在一个新增 optional sidecar 存在时计量，用 fixed POD payload 和预分配容器，热路径不构造字符串/动态 map/vector。

每帧关联因素宜直接复制已有数值：tool/device、active/physical contacts、consumed packets 与最新 sequence、合并/拒绝/争用/drop 分母、real/predicted/L0 点数、model call 数、draw/upload 字节、dirty 像素/画布像素、fullPresent、activeMode+target+output/content revision、viewport recovery/replay/history/snapshot、laser/particle 状态、present result。不为统计扫描历史文档或逐像素计数。

### 软件成功呈现证据

- `PresentFrame` 在 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:7559` 返回后，`:7562` 采 QPC，早于 ready/observer。`:7566` 取得真实 LastPresentObservation 并与当前输出身份比较。应该沿用此返回点。
- `ControllerContentMetrics::FreezeCandidates` 依据已采纳 sequence、真实非 prediction 几何、当帧 raster、scene/content/output 与合成范围筛选（`Draw3.DrawingController.cpp:2853`）。`PresentReturned` 在 `:2888` 先独立记录真实 attempt，再核 currentFrame/outputMatches/compositeReady，`:2925` 才确认 landing。不能只从 `lastConsumedSequence`、成功 bool 或内容版本推定每个输入已绘制。
- 普通帧 `:14379` 只有合成成功且 raster 未失败才调用 Present；`:14384` 失败明确没有可见回执。
- ULW 在 `Draw3.TransparentPresentation.cpp:517` 空 dirty 会直接 true，没有 ULW API；DComp `:1034` 同样返回 S_OK 未调用 Present1。新诊断应单列 `apiAttempted/noop`，不能把该值计为真实新帧呈现。
- `Draw3.TransparentPresentation.cpp:1278` 当前 SUCCEEDED 包括非失败 DXGI 状态；原始 HRESULT 应附加保留，若出现 occluded 等状态单列。软件成功/权威内容 proof 仍不能声称光学可见。Present1 SyncInterval=0 允许队列中间帧被后帧丢弃。[Microsoft Present1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiswapchain1-present1)
- RuntimeMetrics 当前只对首次 Down landing 有可证据化集合；`Draw3.RuntimeMetrics.cpp:1042` 的 moveLatency/upFinalStableLatency 为 null。新增 Move/Up 必须另建按真实输入序号与当帧贡献的链；不能把 Down latency 数组改名后用于 Move/Up。RTS 链细节由输入研究单独负责。

### 初始化与硬件环境证据

- `GraphicsDeviceResources` 已持 driverType/featureLevel/adapter (`Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cppm:16`)；Hardware 请求 11_1/11_0，E_INVALIDARG 再仅 11_0 (`Draw3.GraphicsInitialization.cpp:63`)，Hardware 失败保留 HRESULT 后 WARP (`:98`)。性能报告记录实际选择及每次失败，不能只列机器上的 GPU 名称。
- `Draw3.Diagnostics.cpp:812` 已有 DXGI adapter Description/VendorId/DeviceId/SubSysId/Revision 与三类内存；`:833` CheckInterfaceSupport 获取 UMD driver raw version。Win7/旧驱动查询失败要保留 unavailable/HRESULT。
- `Draw3.Renderer.cpp:523` 检查 MapNoOverwriteOnDynamicBufferSRV，失败/不支持退到每批 DISCARD，普通 pen 路径为 `Draw3.RendererPrimitives.cpp:64`。应记录能力结果与实际上传计数，不改变 fallback。
- `Draw3.TransparentPresentation.cpp:833`, `:892`, `:922`, `:1000` 已有 swapchain 请求/实际 description、waitable 状态；DComp/ULW 路由保持。辅助 SelectionUlw 虽主 mode 为 DComp 仍实际走 CPU 回读（`:1262`），不能只按 activeMode 分组。
- `Draw3.Host.cpp:1512` / `:1541` 为真实设备/renderer/presenter 初始化 owner；`:1580` 等待 RTS 决策。性能初始化 span 应放在这里或现有 Initialize 内，startup 错误也应输出独立 session 状态。
- `Draw3.Renderer.cpp:643` shader 选择读取 INKEYS_DRAW3_WIN7_DIAGNOSTICS/INKEYS_DRAW3_WIN7_GUARDED_SHADER；性能启动不得为了采日志设置这两个 env，否则可能改变候选着色器。GPU resource/optional particle/JIT 预热状态宜记录，避免将冷启动混入稳态帧。
- 现有 Host metadata (`Draw3.Host.cpp:422`) 只有 graphics/driver/FL/mode/output/adapter 名称。CPU 型号、OS/进程架构、RAM、显示刷新率/DPI、电源等仍需诊断启动脚本/进程启动快照补充。没有证据可判定 KX-U6780A 的 1 FPS 原因。

### GPU timestamp 可行性与边界

未发现产品 Draw3 现有 CreateQuery/GetData timestamp 实现。当前使用 D3D11 immediate context，符合 GetData 只在 immediate context 读取的要求。[Microsoft GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)

建议作为新增诊断 sidecar 的 optional 子功能：

1. 开启后按真实设备 generation 预建小固定 slot 池；每 slot 一个 TIMESTAMP_DISJOINT 与少量 TIMESTAMP。timestamp 只 End，disjoint Begin/End，一帧最多一个 disjoint；CreateQuery 失败记 HRESULT 并禁用该 epoch 的 GPU 样本，CPU 采集继续。[Microsoft D3D11_QUERY](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query), [CreateQuery](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createquery)
2. CPU 细段每帧记录，GPU 仅稀疏采集（例如每 N 个真实渲染帧；N 写入 metadata），围绕 geometry submission、composite 与 ULW Copy 命令取 marker。固定池忙时跳过并计 dropped；不复用尚 pending 的 query，不额外刷新/等待。
3. 后续帧给少量老 slot 调一次 GetData(DONOTFLUSH)。S_FALSE 保持 pending，S_OK 才读数据；DONOTFLUSH 可能使尚未提交的命令一直 pending，所以禁止 while-until-ready。退出/设备恢复的未就绪 slot 明确 missing，不能追加无限 drain 或 Flush。[Microsoft DONOTFLUSH](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag)
4. 仅当 Frequency>0、Disjoint=false、timestamps 单调、设备/frame 身份一致时计算差值。Disjoint=true 记 invalid/unavailable，不写 0。[Microsoft TIMESTAMP_DISJOINT](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)
5. query 对应旧 renderer/device 的释放应与 `Draw3.Renderer.cpp:438` 及 presenter recovery `Draw3.DrawingController.cpp:12663` 同序，先失效旧 pending；不能把新设备查询结果挂回旧 frame。GPU slot 记 frameSerial/deviceEpoch。
6. GPU interval 只反映两个 marker 之间的命令流区间；若 geometry 段夹着大量 CPU 计算/Map/逐批提交，区间包含 GPU 等待 CPU 的空隙，不能解释为纯 GPU 忙时。稀疏 marker 的定位能力和扰动应写进报告，不能与 CPU wall 相加。
7. WARP 是软件光栅器；查询即使成功也应标记 backend=software，不能称物理 GPU 运算成本。不同 OS/驱动的 query 是否有效以实际结果为准。[Microsoft WARP Guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)

ULW 当前 Map 没有 DO_NOT_WAIT (`Draw3.TransparentPresentation.cpp:537`)。只观察其阻塞，不为诊断改变 Map flags/readback/presenter 行为。Microsoft Map 说明 GPU 忙时 DO_NOT_WAIT 返回 WAS_STILL_DRAWING；这支持将当前同步 Map 视为包含同步等待的 wall，而不是 copy 纯执行时间。[Microsoft Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)

### RuntimeMetrics 可复用程度与旧合同保护

- 可复用：Host `enableRuntimeMetrics=false` 默认 (`Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h:195`)；`PrepareRuntimeMetrics` 开关前返回 (`Draw3.Host.cpp:375`)；Session reserve/fixed budget (`Draw3.RuntimeMetrics.cpp:420` / `:440`)；完整 proof/attempt 分离；有限有效分母和 dropped/invalid；owner stop/join 后封口 (`Draw3.Host.cpp:445`)。
- 不可原样代替新需求：四 inclusive 旧数组；仅 Down；细 Renderer/ULW/wait/GPU 均缺；active intervals 不等于成功呈现间隔；Host 导出拒绝 startupFailed/runFailed (`Draw3.Host.cpp:476`)，性能包应能报告失败会话，不能为了新报告删除旧 API 拒绝条件。
- 建议保留旧 schemaVersion=2、旧 WriteJson、旧 phase（16 warmup/200 measured）与 MeetsStrictThresholds 的语义；新详细 payload/导出在独立 `performance` 版本化字段或独立报告实现，关联旧 frameSerial。特别保留旧 inclusive 数组、未支持 null、P99<1000 为 null (`Draw3.RuntimeMetrics.cpp:39`)、legacyThresholdMet 非 release verdict 与 create-new。
- 若扩展 RuntimeMetricsFrameSample，必须同步 `MaximumSamplesForBudget`、allocatedBytes/effectiveSamples 和 Host/Controller auxiliary 预算；不能仅添加数组而仍声称原32MiB。更稳妥是新可选固定 sidecar，旧 Session 构造无新字段分配。预算/样本裁剪不准影响真实输入准入或帧策略。
- `GetThreadTimes` 每模型调用会增加系统调用。新细粒度阶段尽量只 QPC，帧级或稀疏线程 CPU；已有旧 MetricsStageScope 若保留，其开销应报告为诊断成本，不能把开启诊断后的帧绝对值当无扰动基准。

### 正常模式门控与诊断成本

- 进程启动一次确定性能开关与输出目录，Host 默认无 Session、新性能 sidecar=null、GPU query 池不存在。模块热路径不得读取 env、格式化、计数、建容器、启动线程。单个可预测 null gate 后才开始诊断函数/scope；避免默认栈构造大 POD。
- 现有 `DrawingControllerMetricsState::Prepare` 在 session=null 时 `FitsBudget` 直接 false (`Draw3.DrawingController.cpp:3146`, `:3840`)；MetricStageScope 的 null masks 在 ReadMetricClock 前返回 (`:2309`, `:2318`)。这是可以沿用的门控模式。
- 保留业务本来需要的 QPC；当前 `PresentFrame` 的 `GetQpcTimeMilliseconds` 在 `:7551`/`:7563` 本来就无条件执行，帧起点/帧限速也已有 QPC。验收目标是禁用时零新增诊断时钟/统计/query/分配/I/O，不应声称产品正常路径完全无 QPC。
- `LogFrameTiming` 在 Debug 已每250ms输出 (`Draw3.Diagnostics.cpp:655`)；正常 Release 编译为空。旧的 Debug 计数/日志属于现状，不在本任务顺带重构。
- StartupEnvironmentDiagnosticsEnabled 本身为 atomic 开关 (`Draw3.Diagnostics.cpp:847`)。它既启用 init adapter 日志，也启用每秒 ULW 像素统计 (`Draw3.TransparentPresentation.cpp:539`) 和 cursor 全 binding/removedReason 查询 (`Draw3.RendererPrimitives.cpp:258`)。性能模式不能粗暴复用这个全局开关；应让 init success 日志接受单独性能启动 gate，重诊断保持原开关。
- 诊断 wall QPC 频率启动缓存；使用差值而不是绝对 double 先转换。跨线程相差±1tick的先后不确定，输入关联若近边界应记录该量化范围，不能用 clamp(0) 伪造精确阶段。QPC wall 不是 thread CPU。[Microsoft QPC](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)
- 主 render owner 不写盘/console、不排序、不计算 percentile。结束后离线输出/分析；如要运行中容错文件，必须独立固定队列和低频 writer，并明示其预算/drop，不让诊断队列 backpressure 产品线程。

### Related specs / 验证建议

- `.trellis/spec/native-desktop/draw3-integration.md:95`: 独立 Hardware/WARP、唯一 Draw3 owner；`:412` 是产品 DComp→ULW 的有效路线；`:427` 是 RuntimeMetrics 有限证据合同。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:31`: 日志包含操作/错误码，避免每帧/packet 输出；资源必须沿实际 owner 顺序释放。
- `.trellis/spec/native/runtime-and-rendering.md:18`: RTS 回调不碰 D3D/模型，Move 合并、控制/终态 wake；`:1093` dirty rect；`:1101` 某些 mode 排序文本属于较旧 demo描述，产品路线以 native-desktop/draw3-integration 为准。
- `.trellis/spec/native-desktop/draw3-shaders.md` 与 `.trellis/spec/shaders/cpu-gpu-contracts.md`: 不更改 shader布局、alpha、上传与绑定合同。
- `.trellis/spec/native-desktop/build-and-compatibility.md` + 当前用户 AGENTS: 真编译用完整 InkeysRepo.sln Debug|ARM64、原生 MSBuild、同 invocation PATH workaround、至少5分钟；本研究未运行构建或 GUI。
- 建议无窗口检查：关闭 gate 的假时钟/query/allocator 调用数为零；exclusive/inclusive 累计口径；失败/empty/noop/occluded 呈现分母；容量/drop/非法时钟；query S_FALSE/disjoint/failed/deviceEpoch；输入证明失败不成功；离线报告长帧与idle恢复筛选。纯逻辑测试/编译不能验证 GPU查询和真实屏幕表现。

## Caveats / Not Found

- 未发现名为 normalRun 的产品符号；本研究把它解释为 DrawingController::Run 的正常帧路径，测试 probe 不应拿来替代产品采样。
- 未对 KX-U6780A、Win7、低端硬件、真实 WARP/ULW/GPU执行测量；无 GUI、无构建、无 Git 操作，只写本任务 research。
- 新开关的 Settings/启动脚本/RTS 元数据接线属于其它研究负责范围；此处没有批准具体接口名称。
- 原 API successful-return/内容 proof 不是屏幕像素光学可见；GPU interval 不是 CPU提交流水线的全成本，也不是物理GPU忙时。
- 故障退出与强制结束可能无法 owner join 后正常导出；新诊断包必须保留独立运行状态/日志并明确 partial，不能借修改旧 WriteRuntimeMetrics 严格合同掩盖。
