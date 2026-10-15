# Research: Draw3 图形调用归因与无窗口资源对照

- Query: 在不改生产绘制算法、资源大小、Map flags 或后端选择的前提下，补齐 ZX C-1190 长帧中 Map、常量写入、Unmap、绑定、复制/清理、Draw 的归因，并以独立无窗口基准区分资源/更新方式/硬件后端成本。
- Scope: mixed；主产品 `Inkeys/Inkeys/Drawing/Draw3`、早期 CLI、同一 EXE 的独立 D3D11 离屏对照；不含 RTS/坐标诊断扩展。
- Date: 2026-10-15

## Findings

### 1. 已确认的现场边界

- 现场源目录：`D:/Personal/Downloads/Inkeys-Draw3-Diagnostics/draw3-20261015-042937-3b356c1567c74be8a83e0a68a1684fa8`。`host-0001/initialization.json` 报告 ZX C-1190、硬件 driver type=1、feature level=45312（11_1）、1920×1080、动态 SRV NO_OVERWRITE=true；不可把原始诉求中的 KX-U6780A 型号直接代入本包。
- 已有诊断确认主画布 Map 约占帧工作 47.8%、owner CPU 约占墙钟 98.2%，每多一组绘制约多出 140ms；这些是排查线索，不是已确认的驱动根因。
- 原始 `gpu.csv` 的 9 个候选序号为 32、64、96……288；对应可见 marker 为几乎空的 geometry，composite/readback marker 为 0。不能用这些时间说明实际绘制很快。
- 既有 `PerformanceMap()` 只按帧累计，不区分资源、调用点和子步骤；大部分常量写入、所有 Unmap 和多数 state setter 尚未计量。因此不能把剩余一半时间简单命名为 shader 时间。

### 2. Files found

| 文件 | 用途/与本轮的关系 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cppm` | InkPoint/shape/CB 布局、固定容量、Renderer 方法和资源角色 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp` | 合成矩形上传、目标/清理/复制、固定和尺寸资源创建、真实 shader 选择 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp` | 普通/硬笔、形状、荧光笔、光标的 Map→写→Unmap→绑定→Draw→解绑 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererLaser.cpp` | Laser 常量、coverage/rect/dot/粒子提交；须避免“全 Draw3”统计实际漏项 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererSnapshot.cpp` | 可信 L2 快照纹理 copy、48B b3 CB 和 shape20 合成 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp` | 笔画完成/历史恢复的额外 CB Map、copy、clear、Draw，原旁挂未逐项覆盖 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.LaserParticleSystem.cpp` | 独立 b0 compute Map/Unmap、UAV绑定和 Dispatch，若本轮不接线须显式列出范围缺口 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp/.cppm` | hardware→WARP 初始化及 feature-level 重试；不能用其自动回退实现 hardware/WARP 对照 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | Present1、ULW copy/readback/row copy/Unmap/UpdateLayeredWindow，维持实际 API 成功合同 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 帧/阶段/内容证明所有权与 GPU marker；不得改变循环/算法来方便采样 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | Session 借用、设备/Renderer/Presenter 初始化与 owner 停止后导出 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Performance.h/.cpp` | 可选 Session、固定存储、现有 Stage/Map 包装及 GPU query 池 |
| `Inkeys/IdtMain.cpp` | 所有 probe 在配置/单实例/HWND 前退出的正式接缝 |
| `Inkeys/Launch/IdtPerformanceLaunch.cpp` | 既有 `--draw3-performance` argv 验证，不要混用新独立 probe 的身份 |
| `Inkeys/resource.h`, `Inkeys/Inkeys.rc` | 已嵌入 pixel=301、vertex=302 的正式 SHADER 资源，无需新 FXC 工具或源代码编译 |
| `Inkeys/Inkeys.vcxproj`, `.filters` | 只需为新普通 TU/header 登记项目项，不引入新工程/依赖 |

### 3. Code patterns / 确切热点

下文简写 `D3/` = `Inkeys/Inkeys/Drawing/Draw3/`。

| 位置 | 直接确认事实 | 必须补的维度 |
| --- | --- | --- |
| `D3/Draw3.Renderer.cppm:75`, `:83`, `:232` | InkPoint=16B，固定容量200000，所以墨迹 SRV buffer=3,200,000B | allocated_bytes 与 actual_write_bytes 分列，不能把小上传当小资源 |
| `D3/Draw3.Renderer.cppm:121`, `:128`；`Draw3.Renderer.cpp:631` | HighlighterPrimitive=24B，荧光笔 buffer=4,800,000B | 角色及 stride 区分 |
| `D3/Draw3.Renderer.cppm:389`, `:400` | GlobalShaderConstants=48B，字段 width/height/shapeType/bufferOffset/color/operatorKind/padding | 原生产逐字段写入独立计时，不在此次采集中改写成 memcpy |
| `D3/Draw3.Renderer.cpp:543` | globalCB dynamic + CONSTANT_BUFFER + CPU_WRITE；laserStyleCB=112B | CB 描述、版本与映射方式 |
| `D3/Draw3.Renderer.cpp:616` | Ink dynamic + SRV + CPU_WRITE + STRUCTURED，stride16 | 完整原 descriptor/NO_OVERWRITE capability；生成 probe 基线沿用 |
| `D3/Draw3.Renderer.cpp:126` | 每次 ApplyOperatorLayers 上传两个 InkPoint（32B），却 DISCARD 同一 3.2MB InkData，再映射 globalCB | 路径=operator_resolve；两次Map、两次写、两次Unmap各自独立，不改变 m_bufferHead=2 |
| `D3/Draw3.Renderer.cpp:149` | 合成经过 OM目标、IA、VS、VS-SRV、VS-CB、PS、PS-CB、5个PS-SRV、sampler、blend、RS、Draw、解绑 | 每类setter及解绑需要独立调用计量，批总 span 不能替代热点归因 |
| `D3/Draw3.RendererPrimitives.cpp:66` | 普通笔 NO_OVERWRITE 时按 head 追加；cap不足或环形满才 DISCARD | map_type、byte_offset、batch_count、vertex_count、ring_head_before/after |
| `D3/Draw3.RendererPrimitives.cpp:84` | 每批 globalCB 逐字段赋值；此前未计 UploadCopy | `constant_write` span 覆盖写入本身，不能只加 copiedBytes 数字 |
| `D3/Draw3.RendererPrimitives.cpp:127`, `:197`, `:357` | 荧光笔始终 DISCARD；形状复用两个 InkPoint 槽；光标也会上传 InkData/globalCB | caller_path/role 能区分真实笔迹、形状、cursor/暖机，避免“每 Draw 140ms”混淆 |
| `D3/Draw3.Renderer.cpp:208`, `:215`, `:235` | 目标切换、双RTV、ClearRTV 都是调用 | target_role、format、尺寸/dirty，不新增热路径 GetDesc/GetResource |
| `D3/Draw3.Renderer.cpp:111`, `:294` | 脏矩形 CopySubresourceRegion 和 Laser bake CopyResource | source/destination role、box/面积、提交耗时；不能解释成 GPU 完成耗时 |
| `D3/Draw3.RendererSnapshot.cpp:244`, `:253` | shape20 全局 CB+snapshotCB 都映射，snapshotCB也48B | role=snapshot_constants，不能混成墨迹buffer |
| `D3/Draw3.InkHistoryGpu.cpp:569`, `:578`, `:653`, `:696` | 历史/缓存合成有直调 Map/Unmap/Draw，绕过旧 PerformanceMap/Draw计数 | 最少给其单独 span/operation 计数，否则报告声明 coverage=renderer_only |
| `D3/Draw3.RendererLaser.cpp:443` | DrawInstanced 直调原来未加 Draw计量 | 与普通Draw分列，instance_count，不冒充0 Draw |
| `D3/Draw3.TransparentPresentation.cpp:539`, `:545`, `:558`, `:603`, `:1068` | ULW copy、READ Map、row helper内Unmap、ULW API；DComp Present1(0,0) | readback角色/RowPitch、Unmap单列、真实HRESULT/API attempted不被scope覆盖 |
| `D3/Draw3.Renderer.cpp:410` | resize释放路径已有解绑和 Flush | 只计原有Flush，不能向正常绘制新增Flush |

### 4. 建议的生产旁挂接口边界

此处为设计建议，最终以任务 design.md 为准，不是现有函数。

- 在既有 `PerformanceSession*` 空指针门后，提供 `PerformanceGraphicsCall`/`PerformanceGraphicsScope` 的小型 inline 包装，关闭时直接调用原 API。关闭分支不得先求 GetDesc、读QPC、构造大数组、查角色表或访问全局原子。
- operation 与 caller_path 独立：例如 `map/constant_write/unmap/vs_bind_srv/ps_bind_cb/draw` 和 `stroke/highlighter/shape/operator_resolve/cursor/snapshot/history/laser/readback/initialization`，否则相同 Map 角色在不同流程无法解释。
- 每条独立调用至少包含：单调 event_id、owner thread身份、host/device_epoch、frame_serial（0表示prior/cold）、path/pass/batch编号、op、resource_id/role、start/end QPC、真实HRESULT及 result_known、map type/flags/subresource、buffer allocated/write bytes/offset、Draw顶点/实例数、slot/count、目标尺寸/box。void setter没有HRESULT，不能伪造 S_OK。
- descriptor 仅在真实资源创建/初始化/替换时登记固定 POD：资源匿名 id+epoch+role、尺寸/格式、usage/bind/cpu/misc/stride、创建结果/生命周期；按实际传入描述，资源指针只作内部匹配且不导出。多RTV/多SRV用明确group_role和slot/count，或固定小数组角色ID；避免只记第一元素。
- 单帧/会话内的资源/op/path聚合可保留 count、sum、max、first/lastQPC和invalid；原始序列固定前缀+固定慢调用topN。如果原始槽耗尽，聚合必须仍有独立有界账本并明确丢样，否则慢设备偶发后段又失去证据。
- “frame work - 已覆盖互不重叠call durations”只能作为未归因余额；必须排除 stage 包含关系、prior、跨Begin分段及invalid。不能 geometry+Map+write+Draw重复相加。
- 真实 shader 选择冷记录应包含resource id/guarded-or-original/实际bytecode字节数或SHA、CreateShader结果与span；不能只靠 EXE 版本猜最终使用字节码。`Draw3.Renderer.cpp:651` 选择过程不可因采集开启而变化。
- 初始化补充真实 CreateBuffer/CreateSRV/CreateTexture/RTV/Blend/Raster/Sampler/Shader 的类别、角色、参数和span，只有成功创建后才登记资源，失败保留首HRESULT。Host/Presenter拥有初始化生命周期，不在普通 Draw wrapper 中做lazy I/O。

### 5. GPU 候选的图形侧约束

- 现有 `D3/Draw3.Performance.cpp:648` 在 `frame % 32` 才 Begin，所以大量空loop可耗候选，慢设备却没有实际 Draw覆盖。
- 建议首个真实 geometry/composite/copy/clear 工作点启动候选，保证最早N个真实工作帧，再按真实工作帧计数或时间限频；8 slots/非阻塞 GetData 保持。
- 要在首个相关命令前插 timestamp，不能执行 Draw 后发现本帧有Draw再补一个“开始”；每个样本输出draw/map/copy/clear计数、marker mask、frame关联和是否仅提交/空工作。
- 不在真实绘制中为了拿query结果加 Flush/等待。ready/disjoint/pending/query_failed/device_replaced/owner_stopped各有状态；缺marker不变成0ms。
- 跨 CPU 长提交空隙的 GPU timestamp 区间是命令流经过时间，不是 shader active time。独立 probe 可在预备完成后只包一小批Draw，并补一次完成证据，以帮助分辨纯提交与执行。

### 6. 独立无窗口 probe 的最小实施方案

根协调的协议（2026-10-15确认）：

```text
Inkeys.exe --draw3-resource-probe-list
Inkeys.exe --draw3-resource-probe <case_id> <absolute_existing_case_dir> <session_id>
Inkeys.exe --draw3-resource-probe-test
```

- list 输出 JSON 静态 case 列表，test 只验证参数/矩阵/纯CPU结果协议，绝不创建 D3D device；两者在任何配置/单实例/HWND前返回。
- 正式case也同样早退出路径；只 `D3D11CreateDevice`、离屏texture/RTV/SRV/CB、embedded shader；不能调用 Window/Host/TransparentPresentation/Product/RTS、CreateSwapChainForHwnd、CreateWindow、隐藏测试窗口。
- capability literal `INKEYS_DRAW3_DIAGNOSTICS_V2` 静态嵌入；包构建只查PE/hash/literal，不能运行未知EXE探参数。
- 建议新增 `Draw3.ResourceProbe.h/.cpp` 普通TU，只借 Win32/D3D11/JsonCpp/WRL及已有InkPoint合同；根负责 IdtMain include/dispatch 和vcxproj/.filters登记。不新建工程，不改 shader 源码/构建链。
- 用 D3D11CreateDevice(nullptr,HARDWARE/WARP,...BGRA_SUPPORT) 分别测；11_1列表 E_INVALIDARG 才11_0重试，hardware失败输出 unavailable/failed，不能静默WARP。若未来显式adapter枚举，非空adapter必须 DriverType=UNKNOWN。
- **不能复用 `InitializeGraphicsDevice` 作对照**：`GraphicsInitialization.cpp:103` 硬件失败自动WARP，污染分母；其设备/adapter信息读取模式可以复用。
- **不泛化生产 `InkRenderer::Init`**：`Renderer.cpp:541`→`CreateSizeDependentResources`要求swapchain，后者 `:363` 明确拒空。为 probe 改生产 Init/允许null属于不必要行为变更。
- 最小离屏pipeline可直接加载 `resource.h:75/76` 与 `.rc:358/360` 的301/302，配48B GlobalShaderConstants镜像+static_assert、InkPoint stride16、BGRA8+R16F双目标、实际blend/sampler/raster描述。合成需准备L0/L1的Add/Retain SRV；非相关未绑定资源仅用于未选shader分支。
- 这条小pipeline属于 comparison_path，报告明确“复用正式shader字节码与资源合同的离屏对照”，不是实际Host/输入笔迹重放。不宜因方便而跨模块直接写Renderer公开实现字段；native/platform-and-resources.md将其标为非稳定实现暴露。如必须调用真实方法，应置Renderer实现单元并以窄函数给probe调用，避免给产品新增通用测试初始化接口。
- 自行生成固定两点、128/256点；无需新modeler或输入回放。这一轮关注资源/driver，真实计算耗时由旁挂保持。

### 7. 有区分度的 case 矩阵

case_id 固定白名单，backend进入case身份；建议不用所有维度笛卡尔积。下面每项是需要的对照问题，实施可合并静态case定义。每case独立进程/资源，cold和warm分列；例如cold1、warmup2、measure8（快case可16），以case及全矩阵deadline限制，而不是永远跑满次数。

| 优先级 | 对照组 | 最少参数与意义 |
| --- | --- | --- |
| P0 | ink SRV large/small + DISCARD + no Draw | 3,200,000B vs256B，stride16，只写32B；Map/写/Unmap分别测；验证整块资源成本 |
| P0 | 相同large/small + DISCARD + with Draw | 同样32B、正式VS/PS胶囊或合成分支；与noDraw配对辨别in-use/同步成本 |
| P0 | large SRV NO_OVERWRITE | 仅cap支持；先DISCARD初始化，然后每次写未用32B区域、shader offset同改；noDraw/withDraw配对 |
| P0 | globalCB48原式字段赋值 | Map DISCARD/每字段写入整体span/Unmap；noDraw/withDraw；不要把原式换成更快写法后失去反例 |
| P0 | globalCB48本地POD+memcpy | 相同值、相同map/Draw；区分逐字段/写结合/RMW效应；至少另有RAM memcpy基线 |
| P0 | 正式合成形态 large/shared vs tiny/separate矩形buffer | 固定两个点、相同shader/纹理/CB、只变几何buffer身份/大小；仅对照case改变，不改产品 |
| P0 | 几何+合成 1/2/3/6次提交 | 每call原始值与总值均保留，检验现场近线性~140ms倍数是否在离屏复现 |
| P1 | write size | 大SRV固定32B vs4096B（必要时64KiB），资源大小不变；分配大小效应与带宽效应分离 |
| P1 | highlighter SRV4,800,000B stride24 | 写48B/noDraw+shape3Draw；与ink large共通或专用成本 |
| P1 | DEFAULT+UpdateSubresource | 另建DEFAULT SRV/CB，分别与dynamic Map对照；CB全48B/null box，不对dynamic调用该API |
| P1 | 连续discard vs不重叠ring | 同样工作/迭代数，记录每次offset/有效cap，不许覆盖GPU仍读的NO_OVERWRITE区域 |
| P1 | single RTV / 双RTV | BGRA8单目标 vsBGRA8+R16F、blend disabled vs实际MAX/MIN，识别MRT/状态或着色器分支成本 |
| P1 | target size | 64×64 vs1920×1080（有界硬上限），几何/上传量固定；小rect vs全屏rect，不混为不同case |
| P1 | Clear与copy | BGRA8/R16F clear；全纹理 CopyResource vs64×64 CopySubresourceRegion；提交/完成时间分列 |
| P1 | staging READ | 有界GPU→staging copy→Map READ→行拷贝→Unmap；不调用ULW，也不冒称实际present速度 |
| P2 | CB其他大小/专用写法 | 112B laser、48B snapshot；写0/非0或严格write-only store对照可独立case，帮助排除compiler/RMW；禁止读取write-mapped数据 |
| P2 | CB NO_OVERWRITE | 需MapNoOverwriteOnDynamicConstantBuffer；noDraw简单，withDraw需独立未用区间+Context1 offset绑定或先确认完成；不在48B同一区域不断覆写 |

推荐每个实际case都运行hardware和WARP配对，且记录实际adapter，不能仅把case字符串当后端事实。原生产shader失败、格式/feature/cap不支持分别输出unsupported及HRESULT，不能fallback替代case。WARP快/硬件慢只能缩小到硬件driver路径，不能直接证明driver bug；本矩阵可为下一步修复择路提供证据。

### 8. probe 计量、完成和有界策略

- 输出 `probe-result.json`，schema_version=1、probe_revision=2；含case_id/session_id/final_pid/process_start_filetime_utc/exe_path、requested_backend/actual_backend、adapter/vendor/device/driver/feature、device flags、full resource descriptors、shader资源/实际字节码身份、caps及各cap query HRESULT、尺寸/写入量/offset、实际 iterations、QPC frequency/起止和owner CPU。
- per iteration 保存 map/write/unmap/IA+VS+PS+OM+RS+sampler各op、Draw/Copy/Clear提交、显式Flush（仅probe）、event_completion_wait、gpu timestamp/disjoint；返回void的调用写result_known=false。first error/creation stage、removed reason、case status与实际已完成样本数必须保留。
- `unsupported`=明确cap/feature/格式不支持；`failed`=执行失败；`partial`=至少部分样本/期限退出；`timeout`由父supervisor判定；不可输出通用PASS掩盖case未运行。
- 单个驱动调用没有用户态可强行取消的安全期限。**每case独立进程**才可隔离挂起；父只等待启动返回的精确Process/HANDLE+时间/EXE身份，超时仅终止自己创建的probe worker，绝不按名称或产品PID杀进程。
- 先写小份case-started记录（含身份/定义），结束后原子发布result；case开头/测量批次边界可更新checkpoint，避免每个Map热路径刷盘。超时无result仍有started证据。控制脚本的stdout/stderr双流持续排空，不在热循环大量printf。
- 建议单case15–30秒、全矩阵约180–240秒（参数上界固定），串行运行避免多case互相争抢GPU；总deadline耗尽明确not_run_budget，已有case证据保留。20–50对照定义应优先P0，不把几十次超时累积为十几分钟。
- 离屏场景没有Present自动推动提交。probe可在一批命令后明确一次Flush，再以EVENT query做有界DONOTFLUSH+Sleep(1)轮询；Flush时间和completion wait单列并标probe_only。不能把新增Flush模式偷偷混入“产品原调用”样本，也不能把Flush返回当执行完成。
- cold包括D3DCreate/资源/Shader创建和首次Draw，warm统计另列，避免把首次JIT和每次固定成本混合。每case记录thread CPU总量；逐调用QPC足够第一轮归因，逐call GetThreadTimes会放大本来很短的调用成本，应仅在明确需要时单独标记instrumentation口径。
- 队列query失败或deadline到时，GPU时长unknown/partial。不得为了拿数据在应用内spin；独立worker的有界wait也要报告方法/次数。
- 不建议默认启用D3D debug device：现场可能缺SDK layer，且会污染性能。若做debug对照只能新case，单独标flags、missing layer不阻断主矩阵。

### 9. External references / 确证技术语义

均为 Microsoft Learn primary docs，2026-10-15访问；保持D3D11/11.1兼容，不依赖D3D12或新OS必需接口。

1. [ID3D11DeviceContext::Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)：write mapping可能使用write-combined内存；即便看似写0也可能由编译器生成读改写，读mapped内存会有明显成本。**这只是本轮需测的合理假设，不是对现场CPU汇编的已确认结论。** 诊断必须保留原写法并与纯写对照。
2. [D3D11_MAP](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map) 与 [D3D11_FEATURE_DATA_D3D11_OPTIONS](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options)：dynamic SRV及CB的NO_OVERWRITE分别查两项能力；开发者负责不写仍在使用的数据；不能根据11_1名字跳过真实cap查询。
3. [D3D11_MAP_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map_flag)：DO_NOT_WAIT不能与WRITE_DISCARD/WRITE_NO_OVERWRITE并用。因此不能靠加此flag给这两种Map建立超时。
4. [UpdateSubresource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource)：目标不能是dynamic/immutable；CB不能部分更新，应null box；返回只代表已捕获CPU源数据，资源争用可使更新经过额外拷贝。
5. [D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice)：null adapter+HARDWARE/WARP可分后端，无需窗口/swapchain；非空adapter用UNKNOWN；不识别11_1列表时E_INVALIDARG。
6. [Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)：提交异步且有额外成本；完成要EVENT/query证明，不能将Flush时长当完整GPU执行。
7. [GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)、[D3D11_QUERY](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query)、[TIMESTAMP_DISJOINT](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)：S_FALSE仍未就绪；timestamp只用End，disjoint Begin/End、frequency有效且非disjoint才可换算；EVENT只End，S_OK且BOOL=true证明此前命令完成。

### 10. Related specs

- `.trellis/workflow.md`：本角色只研究并持久化，执行/最终质量检查归主会话dispatch。
- `.trellis/spec/native-desktop/index.md`, `cpp-conventions.md`：最小diff，现有module/头边界，中文注释及BOM/CRLF。
- `.trellis/spec/native-desktop/draw3-performance-diagnostics.md`：default-off、32MiB新增/64MiB总预算、owner生命周期、真实输入proof、inclusive stage、非阻塞GPU、完成握手与Win7PS2包合同。
- `.trellis/spec/native-desktop/draw3-integration.md`：唯一Host、DComp/ULW、独立设备、输入和重建所有权。
- `.trellis/spec/native-desktop/build-and-compatibility.md`, `errors-logging-and-resources.md`：完整Solution、真实错误/资源释放、不可由编译宣称目标驱动通过。
- `.trellis/spec/native-desktop/draw3-shaders.md` 及 `.trellis/spec/shaders/cpu-gpu-contracts.md`：resource301/302与48B/16B布局、premultiplied-alpha、MRT和解绑不变量。
- `.trellis/spec/native/platform-and-resources.md`：生产Renderer公开字段不是可任意扩散的稳定跨模块API；禁止为诊断重构Init。

### 11. 建议归属与可测试边界

| 责任 | 文件/接缝 | 验证 |
| --- | --- | --- |
| telemetry implement | Performance.h/.cpp、Renderer*.cpp及必要窄header；协调Controller GPU候选/Host冷记录 | fakeclock/固定容量/aggregate+top慢/invalid/prior/null gate的CPU探针；调用覆盖静态审计 |
| resource probe implement | 新 ResourceProbe.cpp/.h（或明确内部窄函数）；独立case definitions/arg validation/result | `--draw3-resource-probe-list`、`--draw3-resource-probe-test`纯CPU；错误参数/不存在目录早退出，不落普通启动 |
| root integration | IdtMain、vcxproj/.filters、总体预算/协议/spec | Solution Debug|ARM64及Release架构构建，no-GUI probes；保留既有配置/启动/内容proof |
| package implement | CMD/PS1调度、逐casedeadline/身份与继续、分析/report/manifest | fakeworker（成功/坏JSON/卡住/错身份/exit失败）无需D3D；v1真实包与v2合成包离线回归 |
| runtime verification | 经源码确认不创建任何HWND的正式离屏case可作为命令行测试单独选择；不调用产品窗口路径 | hardware/WARP实际结果只能说明本机；ZX C-1190仍需用户运行新版包回传 |

## Caveats / Not Found

- 尚未执行任何实际D3D/GUI/窗口probe；本研究没有修改生产代码、脚本、项目配置或原始回传。
- 无窗口不是无驱动风险，单个D3D call可卡住；case子进程/父deadline是必须的隔离边界。不能在产品进程内起超时线程后detach遗留COM资源。
- 不能保证本次补强后绝不需要下一轮；若成本在内核驱动/固件或特定调度条件，仍可能需要厂商符号/ETW/GPUView。当前方案目标是一次覆盖最有区分度的资源、调用和后端对照。
- 动态结构化SRV descriptor应沿用现场成功创建的原描述；本轮没有静态判断其跨所有驱动均合法或快的充分依据，必须保留Create结果/实际cap。
- 原shader实际选择可能受Win7/WARP守卫策略影响；probe若仅加载301须明确original_embedded身份，不把它称为guarded生产路径。当前ZX Win10 hardware基线正是original；ARM64包也要记录其真实选择。
- 真实Present光学延迟、DWM合成、用户输入时序不能由纯离屏基准替代；仍由正式旁挂分别记录。
