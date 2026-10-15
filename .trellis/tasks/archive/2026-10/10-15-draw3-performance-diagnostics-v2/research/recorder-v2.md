# Research: Draw3 recorder v2、逐图形调用归因与真实工作 GPU 采样

- Query: 在现有 Draw3 性能旁挂中，以最小改动补齐按资源/调用/帧的耗时、未计量残差与真实绘制帧 GPU 证据；同时保持普通模式无新增采集工作和 32 MiB payload 上限。
- Scope: mixed（仓库 recorder/controller/renderer、现场 schema=1 session、Microsoft D3D11/Windows 原始文档）。
- Date: 2026-10-15
- Active task: `.trellis/tasks/10-15-draw3-performance-diagnostics-v2`。
- PRD 状态: 研究开始时只有目标和 TBD，以下为研究建议，最终合同由主会话 design.md 冻结；没有读取 implement.jsonl/check.jsonl。

## Findings

### 1. 读取的文件与归属

| 文件 | 内容及作用 |
|---|---|
| `.trellis/workflow.md` | 研究持久化、角色隔离、最小变更与最终验证流程 |
| `.trellis/spec/native-desktop/index.md` | Draw3 独立 device、owner、现有 GUI 验证边界 |
| `.trellis/spec/native-desktop/draw3-performance-diagnostics.md` | 当前 v1 完整数据/预算/默认关闭/输入 proof/GPU 非阻塞合同，v2 应修订该单一合同 |
| `.trellis/spec/native-desktop/cpp-conventions.md` | 中文注释、原编码/换行、owner 与模块边界 |
| `.trellis/spec/native-desktop/errors-logging-and-resources.md` | HRESULT/Win32 错误锁存、COM 生命周期及失败清理 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Performance.h` | 薄普通头、旁挂接口、nullable scopes、PerformanceMap |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Performance.cpp` | 固定容量采集、prior/frame 归属、query 池、导出及纯 CPU probe |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | owner frame Begin/Finish、geometry/composite marker、纯数值测试 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp` | 最直接的 Map/写入/Unmap/Bind/Draw 缺口实例 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | 既有 GPU readback marker 4/5 与 ULW 子阶段 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | owner 退出时 GpuStop 及最终导出寿命 |
| `Scripts/Draw3Performance/Analyze-Draw3-Performance.ps1` | 当前只接受 host session/initialization schema=1，GPU 解析和分析行预算 |
| `Scripts/Draw3Performance/Run-Draw3-Performance.ps1` | manifest capture_schema=1、最终 owner 握手、启动采集 |
| `Scripts/Draw3Performance/Build-Package.ps1` | manifest raw_schemas 均为 1，构包不启动 EXE |
| `Scripts/Draw3Performance/Test-Draw3-Performance.ps1` | v1 合成 CSV/JSON fixtures、严格类型/身份和容量测试 |
| `D:/Personal/Downloads/Inkeys-Draw3-Diagnostics/draw3-20261015-042937-3b356c1567c74be8a83e0a68a1684fa8/host-0001/session.json` | 现场 baseline payload=26,390,832 B，legacy upper bound=19,995,544 B；该文件仅作只读数据 |

### 2. 已直接确认的 recorder 合同与缺口

- nullable fast path 在 `Draw3.Performance.h:161-173`：关闭 scope 不调用 Clock，PerformanceMap 在 `:211-219` 为空时直接调用原 Map。不要将它改成全局开关、总是构造大描述对象或总是 GetDesc。
- `PerformanceCapacities` 在 `Draw3.Performance.h:71-79`：frames=16384、inputs=32768、receipts=65536、gpu=2048。constructor 在 `Draw3.Performance.cpp:292-314` 同时进行最大容量验证、sizeof 总额 static_assert、运行时 AllocatedBytes 账本和一次性数组分配。
- stage 为 inclusive duration；`Stage()` 在 `Draw3.Performance.cpp:545-560` 专门按真实 frame.start 拆 prior 和 frame 内段。不能把新逐调用记录全部套到“最后保留帧”，也不能将新 leaf 累计再加到旧 inclusive 总时长。
- `BeginFrame()` 在 `Draw3.Performance.cpp:514-525` 从 prior 转入起点前工作；`EndFrame()` 在 `:527-543` 只有 retain=true 才计 framesSeen/保留。新统计仍须处理原本没有保留的 owner loop、失败准备及前缀耗尽。
- 现有 `Upload()` 在 `Draw3.Performance.cpp:580-587` 仅计 map 次数/失败、成功 bytes、discard/no-overwrite，完全没有 resource/site/subresource/flags/单次耗时；`Draw()` 仅计次数。不能从这些 v1 字段推出资源级原因。
- `ApplyOperatorLayers` 在 `Draw3.Renderer.cpp:131-147` 对 inkDataBuffer 和 globalCB 分别 Map；仅 rectPoints memcpy 有 UploadCopy scope，常量逐字段写入、两个 Unmap 均未计时。`:149-178` 的大量 binding/unbinding 也没有计量。它们是剩余成本最直接的缺口。
- v1 session/init schema 在 `Draw3.Performance.cpp:764,796` 固定 1；CPU CSV 固定字段在 `:702`。schema v2 改动必须与 analyzer/manifest 同步，不得让旧分析器把新缺项当成 0。
- 现有 probe 在 `Draw3.Performance.cpp:849-958` 已能注入 QPC，验证 null、前缀、跨 Begin、device epoch、并发输入；GPU 只验“不创建 query”的纯 CPU 分支，尚无 scheduler/transport 状态覆盖。

### 3. 建议尽早冻结的 instrumenter 接口

不建议让不同 instrumenter 各自发明字符串计时或宏。建议由 recorder owner 在普通头集中提供如下**概念合同**（名称可按 design 调整，字段语义必须一致）：

```cpp
enum class PerformanceGraphicsOperation : uint32_t {
    Map, Write, Unmap, Bind, Copy, Clear, Draw, Create, Other, Count
};
enum class PerformanceGraphicsResource : uint32_t {
    Unknown, InkData, GlobalConstants, HighlighterData, LaserData,
    LayerTexture, BackBuffer, StagingReadback, Other
};
enum class PerformanceGraphicsPass : uint32_t {
    Unknown, Startup, Geometry, Snapshot, Composite, Present, Recovery
};

// site 为稳定枚举/注册编号，导出映射表给出原函数/具体 API。
// instanceId 为匿名会话资源编号，不将裸指针输出到包。
struct PerformanceGraphicsArguments {
    uint64_t instanceId, sourceInstanceId, bytes, byteOffset;
    uint32_t subresource, mapType, flags, argument0, argument1;
};

// Begin/End 仅在 nullable gate 内执行；owner 单写、noexcept、无 I/O/分配。
GraphicsToken PerformanceSession::BeginGraphicsCall(operation, resource, site, args) noexcept;
void PerformanceSession::EndGraphicsCall(GraphicsToken, bool resultAvailable,
                                        HRESULT result) noexcept;
// Pass scope 只设置上下文并恢复，不重复加入 leaf operation 时间。
// 模板辅助必须执行原 call 恰好一次；void/HRESULT 两种回传分别处理。
// 参数描述若需要运行时构造，应在 if(session) 后或通过惰性 builder 构造。
```

接口必须满足：

1. scope 正常关闭只存 session/token/起点等少量标量；完整 event 填写仅在 session 非空时发生。包装不重试、不省略、不调整 Map flags、Bind/Draw 次序或大小。
2. 计时边界紧贴原调用；HRESULT 在下一诊断 clock 前锁存；void API（Unmap/Bind/Copy/Clear/Draw）应 `result_available=false`，不能发明成功 HRESULT。API 未实际执行也不能造一个 0ms 调用。
3. resource class 区分用途，instance id 区分同类的真正对象；descriptor（ByteWidth、Usage、BindFlags、CPUAccess、MiscFlags、stride、texture width/height/format）在冷创建/登记时记录一次。每次 Map 不 GetDesc、不 QueryInterface。
4. 对 Copy 指定 src/dst 匿名身份、区域/实际 bytes 的可用状态；对 Draw 指定 vertex/index/instance count；Bind 的 site 必须区分 VS/PS/IA/RS/OM 及绑定/解绑，不能只叫一个总 Bind。
5. 累计 bytes 应明确是“实际写入 bytes”“请求/资源 bytes”“GPU copy estimated bytes”中哪一种，不能再次把 v1 Map 的 bytes 与后续 memcpy 相加当吞吐。
6. 每个 event 有单调 call_id、start/end QPC、frame_serial、owner device_epoch、pass/site/resource、结果可用状态和 invalid flags。trace id 的分配属于诊断 owner，不增加正常路径全局原子。
7. pass 上下文是可恢复的有界栈；辅助函数内部的调用继承当前 pass。外层 helper/group 的 inclusive span 必须独立字段，不能再作为同类 leaf 时间相加。若允许嵌套 leaf，则需要 depth/parent 或 exclusive 归因；更小实现是禁止 leaf 嵌套并将 wrapper 只放到 API 处。
8. 精确记录 End 前后调用；容量耗尽后仍更新真正该帧的 operation 聚合和全会话分组；不得因“没保留 event”就不统计这次调用。

### 4. 固定预算方案：frame/prior 聚合 + 调用前缀 + 独立慢调用 + 全会话分组

**预算单位必须校正**：现场 `allocated_payload_bytes=26,390,832` 是约 25.17 MiB（26.39 MB），距离 32 MiB 还余 **7,163,600 B**，并非只剩 5.61 MiB。

以下是可落地的保守账本示例，具体 row 的 sizeof 必须在实际 x64/ARM64 编译验证：

| 新增项 | 上限示例 | bytes |
|---|---:|---:|
| 每保留帧的新增 frame/prior 摘要 | 16384 × 256 B | 4,194,304 |
| 完整调用前缀 | 8192 × 128 B | 1,048,576 |
| 独立慢调用前缀 | 4096 × 128 B | 524,288 |
| 固定分组表/聚合 | 2048 × 128 B | 262,144 |
| 冷 metadata 预留 | 64 KiB | 65,536 |
| 与现有 payload 合计 |  | **32,485,680** |
| 到 32 MiB 的余量 |  | **1,068,752** |

- 两份 per-frame operation 摘要分别代表 actual frame 内与 prior 起点前的事实（各自 counts/ticks/observed/invalid），不可用全会话聚合补到某一帧。256 B/帧只是上限建议，常用 9 operation 的 64 位 ticks、32 位 counts 和 masks 能装入两份小摘要；不做 resource×site×operation 的巨大帧内笛卡尔积。
- event prefix 用于还原前期完整时序；slow prefix 用于 full prefix 塞满后仍保留异常，例如 >=1 ms 或 >=2 ms 的单调用。阈值应按 qpc_frequency 在冷阶段预计算并写 session，不对每次调用格式化浮点数。
- slow store 记录所有符合阈值的事件，允许与 full prefix 重叠，但用 call_id 导出 retention flags 或分析去重；也可在 prefix 已保留时不占用 slow store。必须明确口径并分别输出 seen/retained/dropped。
- 全会话 group 至少按 operation + resource + site + pass；采用固定容量直接索引或有界探测表，保存 count/sum/min/max/failures/invalid 和最大调用的 call_id/frame，不做热路径 unordered_map 分配。表满可并入显式 overflow/unknown 桶并报告；不可静默漏计。
- prefix 满后不覆盖，slow prefix 满后也要报告 seen/drop；不能承诺有限内存捕获任意长时间的所有细节。报告清楚区分“逐调用覆盖”“frame aggregate 覆盖”“全会话 aggregate 覆盖”。
- checksum/导出 JSON、CSV 的临时内存仍应按现有约束描述；payload 32 MiB 不是整个进程 RSS 保证。不能为此次方便提高预算或缩减既有 inputs/receipts 默认容量却不说明。

### 5. 残差归因与可信度

- 旧 Geometry/Composite/Present 仍是 inclusive。新 `graphics_leaf_qpc` 只统计互不重叠的 API/内存写区间；在同一 frame/pass 内相减才有意义。
- 可计算 `pass_wall - valid_nonoverlapping_leaf_wall` 的剩余部分，但名称应为 `unattributed_pass_qpc`：它还包含 C++ 遍历、参数准备、计时/聚合本身和未覆盖 API，不能叫“驱动其他开销”。
- 若当前 pass 根本没测量、scope 无效、叶区间越界或存在未消除重叠，残差应 unavailable/invalid；不能 clamp 到 0。阴性结果（没看到慢调用）必须带覆盖比例。
- 统计 `(pass_wall, leaf_wall, counts, observed, invalid, detailed_calls_retained)`；若做 source-site 静态覆盖清单，也应由实现者逐条核对剩余未包装 context API，供报告明确“未计量”范围。
- 每个 event 的 CPU 时间可选。保留既有 frame GetThreadTimes 作为默认 CPU 证据；若为了驱动忙等区分增加 per-call GetThreadTimes，必须有 availability 与诊断选项，报告两次系统调用的开销。
- QueryThreadCycleTime 可保留原始 cycle delta，**不得用 CPU 标称频率换算 ms**。微软明确禁止这样换算，因为 timer 与频率行为因 CPU 而异。GetThreadTimes 才是 kernel/user 时间单位证据。
- 诊断冷阶段可做 QPC/readCpuClock 空对测量作为参考，不从单个调用测量中盲目扣常数。不要声称“精确排除了所有采样开销”。

### 6. GPU 现状、采样策略和必需字段

当前根因链路：

- `DrawingController.cpp:3956` 在每个 owner loop 的 Begin 调 `GpuBegin(frameSerial)`。
- `Performance.cpp:648-651` 用 `frame % 32 == 0` 筛选，且 GPU Begin 发生在尚不知道有无 Draw 的时刻。空 owner loop 消耗候选；现场 9 样本未覆盖真正绘制帧，与此一致。
- geometry marker 0/1 在 `DrawingController.cpp:13551,14533`；composite 2/3 在 `:14562,14630`；readback 4/5 在 `TransparentPresentation.cpp:538,542`。
- `Performance.cpp:178-183` slot 有 marker mask，但 `:755-758` GPU 原始导出没有 mask、真实 draw_calls 或采样原因。缺 marker 与真实 0 区间难区分。
- `PollGpu` 在 `Performance.cpp:260-288` 每次只看一个 slot，GetData 全部 DONOTFLUSH，S_FALSE 立即返回。保留这个不等待合同；`GpuStop` 在 `:680-685` 只扫固定池一轮，剩余输出 owner_stopped。

建议改为以下职责，避免兼容层/Controller 与 Renderer 双方各自启动 query：

1. `GpuPoll()` 独立于选择；可以继续 owner 每 loop 只 poll 一个 slot。
2. `GpuWorkCandidate(pass)` 在首个**即将执行实际图形工作的 pass 内、有效参数验证之后、首个 Map/Bind/Copy/Clear/Draw 之前**惰性调用。每 frame 至多递增一次真实工作候选计数并启动一组 disjoint。
3. 首个实际工作帧必选；可前 8/16 个工作帧密集采样，再每 8/16 个真实 work candidate 采样，选择参数写 session。更小版本只有 first + every8，但本次只有20个绘制帧，建议保留一小段最初工作帧。
4. 初始 geometry marker 已过去才首次启动时，要在真正工作入口补该 pass 的 begin marker；其它缺段保持 mask 未置位。不能在 owner 顶部重新打一遍等价“假起点”。composite-only 帧可以只具有 composite 区间。
5. 候选属于可失败准备尝试，并不保证最终 Draw；采样 row 最终附实际 frame 的 draw/map/copy/clear count 和 selected reason。分析明确区分 sampled_work_frames / ready_draw_frames / failed_prepare_only。报告若 ready_draw_frames=0 应直接提示证据不足。
6. query 仍为 8 固定 slots，池忙就记 skip，不等待；容量2048耗尽后只更新 skipped budget 计数，不热扩容。记录 eligible/selected/started/skipped_pool/skipped_budget/ready_draw 等分母，不能把“支持 query”当作“采到了慢帧”。
7. 原 gpu row 增加 marker_observed_mask、marker_invalid_mask（如有）、candidate_index、selection_reason、CPU start/end QPC、frame_retained、frame_draw_calls/work mask、epoch。只有 ready、frequency>0、完整配对 mask、端点非逆序才参与时长分布。
8. 设备恢复时在 `CaptureGraphics -> AdvanceDeviceEpoch` 按现有合同退休 pending；不能将旧 query 套到新设备。每个 query row 的 device_epoch 不可事后混换。
9. 记录 query issue/poll 的诊断自身 CPU wall 合计、max、failure 状态，尽量不与业务 API leaf 共用分母。ZX 驱动可能 query 本身也慢；新包必须能够辨认这种干扰。

GPU 时间仍是 immediate context 命令流两端之间的区间：其中可以包含 CPU 在 Map/写数据时没有继续提交造成的 GPU 空隙，不能直接称 shader 纯执行。若要更细 Draw 周围的 query，可以另有 bounded 诊断选项；不要为了“更多指标”默认在每个调用前后加 query。

### 7. schema 与消费者同步

- 建议保持 launch/completion/硬件身份 schema=1（其语义不变），host session/init 与新增 graphics schema 明确标 2。manifest 的 capture_schema/raw_schemas 要与包2一致。
- 新 analyzer 应显式支持 host v1 和 v2；v1 图形细项显示 unavailable（`not_recorded_in_v1`），既有 framestage/input 延迟可正常计算；未知版本严格拒绝。
- 现有 `Analyze...ps1:105,117` 限定 1，`Run...ps1:149` 限定 capture_schema=1，`Build-Package.ps1:35` 原始版本映射全1，均须一起修改。
- 新 graphics CSV 各文件有独立 schema/列要求/解析行数和字节上限，不在 CSV 热写；保留所有 QPC/uint64 为精确十进制，PowerShell 分组 key 不用 double 四舍五入身份。
- completion 应传递 graphics seen/retained/dropped 或 session 内部分证据标志，保证“基础帧完整但逐调用前缀溢出”不会被错误解释为整份诊断完全详细；可由独立 coverage 字段表达，而不把符合设计的阈值筛选叫数据丢失。
- 输出说明明确包2、新指标口径、阈值/容量、可证明和不可证明的结论。原来的单次 CMD/同目录输出合同不需改变。

### 8. 必要的无 GUI 验证 seams

扩展已有 `RunPerformanceRecorderProbe`，避免新增构建系统或启动真实设备：

- null wrapper 的 call 恰好一次，元数据 builder、QPC、threadCPU、分配、query、stats 均不执行；通过 fake call 而非空 context 解引用验证。
- Map HRESULT 正确回传，void Unmap 不伪造结果，失败写入/未实际调用不造样本；有异常/早退时状态明确。
- operation/site/resource/pass 完整匹配；嵌套 pass scope 恢复；无叶重叠或 exclusive 计算通过定量时间线断言。
- 一次调用跨真实 Begin 的 frame/prior 按端点拆分，不写已封口旧帧；frame retain=false/outside frame/recovery epoch 均有明确归属。
- full prefix 满后 per-frame aggregates 和 group aggregates 继续；稍后人为长调用仍进入 slow prefix；slow 满与 group overflow 精确 seen/drop；同 call 双 store 不重复进统计。
- invalid/zero/backward QPC、加法溢出、负残差不生成有效时长；observed 和 invalid 与真实执行一致。
- CPU 时钟失败只使 CPU 列 unavailable，不影响有效 wall/HRESULT；cycles 不转 ms。
- GPU scheduler 提取为纯逻辑测试：31/100个空 loops不消耗work index；第1实际work必采、同frame第二pass不重选、后续interval依据work count。
- GPU query transport 可用私有函数表或最小 adapter 注入 fake Begin/End/GetData，检查 S_FALSE 一次返回、flags=DONOTFLUSH、每轮最多一slot、无Flush/spin；ready/disjoint/zero-frequency/missing-marker/nonmonotonic/error/full/stopped/replaced 均可测试。测试接口只存在 session 附件中，不加正常路径全局钩子。
- v1/v2 analyzer fixtures 验证缺细项、残差不可用、空GPU vs真实DrawGPU、partial stores、同call去重、严格schema/身份、容量和非法类型。现有 WindowsPS5.1/pwsh 两套离线脚本检查继续复用。

实现 recorder/controller/renderer 后，仍应按根 AGENTS 做完整 `InkeysRepo.sln Debug|ARM64`，采用现有 ARM64 MSBuild 与同 invocation PATH 规范化；交付 x64/ARM64 Release 包按既有生成脚本。上述数值测试不能证明 ZX 驱动实机、Win7、RTS 或真实呈现表现。

## External References

以下均为 Microsoft 原始文档，读取日期 2026-10-15；使用现有 D3D11/Win7 可用 API，无引入新库/SDK：

- [ID3D11DeviceContext::GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)：S_FALSE 未就绪；query 读取在 immediate context，不因要结果而同步等待。
- [D3D11_ASYNC_GETDATA_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag)：DONOTFLUSH 不推进命令；循环等 S_OK 可能永远不结束，支持本项目固定单轮轮询合同。
- [D3D11_QUERY](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query)：timestamp 用 End，需在 disjoint 内配对，使用 frequency/disjoint 判有效；disjoint 每帧最多一次。
- [Dynamic resources](https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources)：WRITE_DISCARD 丢弃整个 buffer，可在仍被GPU使用时给新内存区域；NO_OVERWRITE 保证和 SRV/常量支持需要特性核查。v2采集不自行改变这些生产flags。
- [ID3D11DeviceContext::Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)：记录真实 HRESULT、资源/映射参数及写入边界，不从单个慢Map反推唯一原因。
- [ID3D11DeviceContext::Unmap](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-unmap)：void 返回，结束CPU映射并恢复GPU访问；没有可伪造的S_OK。
- [QueryThreadCycleTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime)：原始cycle量，不能换算时间；Win7 include Windows.h 可用。
- [GetThreadTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes)：kernel/user 时间用100ns单位，duration与进程启动FILETIME身份不可混用。

## Related Specs

- `.trellis/spec/native-desktop/draw3-performance-diagnostics.md`（本次应由主会话经 update-spec 同步 v2 新合同，尤其 GPU sampling 与 payload/coverage/schema）。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`（失败路径、HRESULT/last-error 锁存、COM/query owner 生命周期）。
- `.trellis/spec/native-desktop/cpp-conventions.md`（普通头/模块边界、小改动/原格式/中文注释）。

## Caveats / Not Found

- 本研究只读源码、spec、现场 session 并写本研究文件；没有构建、启动EXE、创建D3D设备/隐藏HWND或进行GUI/Computer Use。
- 预算 baseline 来自现场 x64 v1 session，不是 ARM64 v2 的真实 sizeof；最终两个架构都应依靠编译 static_assert 和 AllocatedBytes 实账核验。
- 无法从 v1 session 恢复缺失的 Unmap/绑定/写入单调用时间；不能承诺 v2 一轮必然给出驱动内部函数根因。
- 明确2版包“尽量详细”的可交付边界是更完整的API/资源归因、真实GPU覆盖、对照证据与coverage；不意味着无限数据、无诊断扰动或shader纯时间。
- QueryThreadCycleTime 最初尝试的 processthreadsapi 路径不可访问，已核实正确官方页为 realtimeapiset，不属于需要升级SDK的证据。
