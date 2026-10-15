# Draw3 性能诊断合同

## 1. Scope / Trigger

**【直接确认：源码合同】** 仅覆盖生产 Draw3 的硬件/初始化、帧计算与呈现、RTS callback 到真实内容软件 Present 返回。实现为 `Draw3.Performance.h/.cpp` 及现有 Host、RTS、ContactInput、Controller、Renderer、Presenter 的可选旁挂。包2另加 `Draw3.ResourceProbe.h/.cpp` 的独立离屏资源对照，只在明确的 probe CLI 中运行。它不改变绘制算法、输入准入、最后包合并、帧率、shader、Map flags、硬件优先/WARP 回退或 DComp → ULW 路线。

正式入口是 `Inkeys.exe --draw3-performance "<绝对且已存在的本次会话目录>"`，或显式打开默认 false、NoUpload 的 `Experimental.Inkeys3.ConsoleOutput.Performance` 后下一次启动。后者在 `log/Draw3Performance` 创建唯一新目录；前者只作用本次进程，不回写配置。不同时启用 `StartupEnvironmentDiagnosticsEnabled`、WIN7_DIAGNOSTICS 或其他像素/着色器调试。

**【边界】** 软件成功 API 返回不是屏幕光学可见、原始 RTS 坐标像素或触笔到光子证据。真实 Win7、兆芯及其他低端机的现场表现须由用户运行诊断包采集，不能由数值测试/编译推定。

**【分支维护约定（2026-10-15 用户确认）】** `diagnostic/performance` 仅用于诊断，不合入 `dev`。后续需要继续性能诊断时，从 `dev` 同步最新代码到该诊断分支，再适配、构建和验证诊断功能；不要反向将诊断分支合入 `dev`。

## 2. Signatures / Ownership

```cpp
struct HostStartOptions {
    // 其他既有字段保留；空目录关闭性能旁挂。
    std::wstring performanceOutputDirectory;
};
PerformanceExportStatus GetDraw3PerformanceExportStatus() noexcept;
int RunDraw3PerformanceProbe() noexcept;
int RunPerformanceRecorderProbe() noexcept;
bool TryRunDraw3ResourceProbeEarly(int argc, wchar_t** argv, int& exitCode) noexcept;
// 图形旁挂：arguments 构造器只在 session 非空时执行；void 不伪造 HRESULT。
PerformanceGraphicsCall(session, operation, role, site, argumentsBuilder, hresultCall);
PerformanceGraphicsVoid(session, operation, role, site, argumentsBuilder, voidCall);
std::unique_ptr<PerformanceSession>
PerformanceSession::CreateHost(const std::wstring& root) noexcept;
void PerformanceSession::ExportHost(bool startupFailed, bool runFailed,
    std::uint64_t legacyPayloadBytes, bool unavailable = false) noexcept;
```

- Host 唯一持有 Session；Renderer/Presenter/Controller/RTS 只借用指针。正常模式为 nullptr，无新增采样数组、clock、计数、GPU queries、文件/控制台输出或线程。轻量 inline 空指针门不得绕过；不在全局原子开关或每点复杂 POD 上建立正常路径开销。
- RTS callback 通过原子索引占有唯一固定槽，Contact writer 在实际 release commit 前调用 noexcept publication 回调，锁存 generation、even sequence、source QPC、admission。禁止发布后重新读 snapshot 猜身份。
- ContactInput 的传统 header 边界只借用函数指针；独立 MTd Headless 不需链接 Performance/JsonCpp/D3D TU，也不能用测试宏禁掉真实采样接缝来让构建通过。
- receipt/frame/GPU 为绘制 owner 单写；先停止 RTS 生产者，再等待 drawingThread join（包含 owner 内 drawing 析构），之后才导出。首次启动失败和 ULW replacement 各有独立 `host-NNNN`，不得覆盖前一代文件。
- 图形资源元数据只在冷创建注册时读取描述，导出匿名序号、device epoch、角色和代次，不输出对象地址；owner 单写、资源退休不复用旧身份。`PerformanceGraphicsSite` 字面量用 consteval 生成 site ID，空指针门之前不得执行热路径 hash。
- `PerformanceExportStatus` 的 enabled 与 hostRuns/exportedHostRuns/failedHostRuns/startupFailedHostRuns/runFailedHostRuns/unavailableHostRuns、frames/inputs/receipts Seen/Retained/Dropped、proofOverflow、payloadBudgetBytes 为进程累计。failedHostRuns 只指导出失败；与业务初始化/运行失败分别计数。包2追加 graphics/slow calls、resources 的 Seen/Retained/Dropped、graphicsGroupCallsDropped 和 graphicsSiteCollisions。`graphics_detail_complete` 与基础 `capture_complete` 分开，前者要求导出成功且 full/group/resource/site 均无缺口；独立 slow 前缀掉样而 full 完整时不单独判缺。

## 3. Data / Lifecycle Contracts

### 启动与完成

SuperTop/UIAccess 可退出初始 launcher、经 token 创建最终 owner；必须沿现有两层 legacy Split/LaunchState 传递显式 argv，保留中文/空格/引号。不能只依赖环境变量或 stdout 重定向，也不能绕过 UIAccess。`--draw3-performance-test` 和 `--draw3-performance-launch-test` 在配置、单实例及 HWND 初始化前返回。

最终 owner 独占 `.draw3-performance-owner.lock`，CREATE_NEW 写入临时 JSON、flush、原子发布 `launch.json`。schema_version=1，包含 session_id、final_pid、ISO process_start_time 与精确十进制字符串 process_start_filetime_utc、output_directory、exe_path、版本/edition/架构、source、ui_access。早期失败另写 `launch-failure-PID.json`，不是完成。

`completion.json` 仅在 StopProduct 及所有 Host 导出后写，携同一身份、真实 exit_code、status/reason、normal_shutdown/export_succeeded/capture_complete/samples_dropped、log_file 与 export_status。launcher exit 0、无 crash、空数据均不能单独证明成功；强杀/异常路径不能伪造 completed。固定重启参数不继承本次命令目录，持久选项下一轮另建新目录。

### 固定预算与原始文件

每 Host 默认 frame=16384、callback=32768、receipt=65536、GPU sample=2048；新增 payload 上限 32 MiB，与旧 RuntimeMetrics 分开、合计合同不超过 64 MiB。保留前缀，满后只计 seen/drop，不覆盖、不热扩容、不在 callback/owner 热路径格式化或 I/O。高帧率下容量更快耗尽；240 帧/s 的帧前缀约 68 秒，报告必须给实际保留起止与掉样。推荐单轮 30–45 秒，不保证偶发问题总能在前缀内。

`host-NNNN` 导出 `session.json`、`initialization.json`、`frames.csv`、`input-events.csv`、`gpu.csv`。包1均为 schema 1；包2 session/frames/gpu 升2，initialization/input-events 保持1，并新增 schema 1 的 `graphics-calls.csv`、`slow-graphics-calls.csv`、`graphics-groups.csv`、`resources.csv`（空时也必须存在）。launch/completion 身份合同仍为1。manifest/session 的 raw_schemas 使用实际文件名含扩展名。字段 snake_case。session 含频率、status/exported/unavailable、容量/账本、seen/retained/dropped、invalid_intervals/proof_overflow、保留区间、导出区间、GPU 状态和计量口径。`disabled_path_contract` 是静态合同字符串，不能写虚构的正常模式实测零计数。

### 包2图形叶调用与资源

十类操作为 Map、WriteCopy、WriteConstant、Unmap、Bind、Copy、Clear、Draw、Present、Other。每条记录包含 call_serial、frame_serial、device_epoch、resource/related_resource/site ID、role/pass、原始 QPC 端点、有效性、真实可用 HRESULT、Map flags/type、subresource、offset/size 和参数。void API 的 hresult_available=0；未知资源 ID=0 明确不可用，不影响时间证据，但不能用来声称具体资源实例。常量生产字段写入保留原语句及实际36B，48B缓冲区描述与实际写字节分开。

完整调用前缀8192、独立慢调用前缀4096（阈值1ms，QPC向上取整）、分组2048、资源创建尝试512，固定容量不热扩容。slow 与 full 可能同 call_serial，分析必须去重。前缀满后 all-call 分组及帧摘要继续；分组包含 count/total/min/max/invalid/failure，不从汇总伪造全量百分位。各容量账本、首次/最后保留 QPC、首次丢明细 QPC、分组/站点碰撞/算术溢出与未知资源分别公开。

帧追加 graphics_ 与 prior_graphics_ 摘要，含 leaf/invalid masks、count、十类操作 duration/count。只将非嵌套、同设备且可归属的叶区间计入；跨真实 Begin 拆 prior/frame，已封口帧不得被后续调用回写。跨帧、嵌套、时钟逆序或溢出保留原始事实并标 accounting_invalid，不能用总工作减叶和虚构精确的未覆盖耗时。原 inclusive stage 只作为独立参考。

payload_components 按 sizeof×capacity 导出真实数组与 Impl 账本，总新增 allocation≤32MiB。导出期 JSON/缓冲分配和驱动 query 内存不在该 payload 定义中，不能把它描述为整个进程 RSS 上限。

### CPU 帧与等待

frame 起止/真实 API 返回是端点；stage 的 `*_qpc` 是累加 duration ticks。按 observed mask 和 invalid mask 判断是否有效，未知值不能作为 0ms。stage 位顺序：Control、Ingress、ModelReset、ModelUpdate、ModelPredict、Geometry、Snapshot、Composite、UploadMap、UploadCopy、DrawSubmit、Present、UlwCopySubmit、UlwMap、UlwRows、UlwUpdate、DeadlineWait、IdleWait。

Geometry 含 model/upload/Draw，Ingress 可含 Modeler，Present 含 ULW 子段；均为 inclusive，**不得相加造总耗时**。`prior_*` 保存 frame.start 前初始化/唤醒的工作和独立 masks/counts，不纳入 frame work 或该帧 owner CPU。跨内部 Begin 的 scope 按真实 frame.start 拆段；wait 单独附到已封口帧。thread_cpu_ms 只代表 owner，不代表所有 WARP worker。

frame 保存 active_chain/idle_boundary、physical count、canvas size/dirty rect、reason flags、command/down/consume/terminal、effective tool mask、upload bytes/draw/map/失败/flags、device epoch、output target/revision/content revision。恢复中设备换代时，诊断必须同步尚未封口当前帧的 device_epoch，已封口旧帧不回写；跨设备成功呈现间隔断链。正常工具枚举 Pen=0、HardPen=1、Highlighter=2、Eraser=3、Laser=4、SolidLine=5、DashedLine=6、OutlineRectangle=7、FilledRectangle=8。

present_attempted 为软件调用，present_api_attempted 为实际 Present1/ULW。只在实际 API 且 HRESULT=S_OK 时确认新的成功呈现；非 S_OK/失败/noop 不作成功证据，同时不更改生产原 SUCCEEDED 决策。ULW 失败必须保留 attempted=true；GetLastError 在诊断 clock 前锁存。

### RTS 内容关联与延迟

每行一个 Down/Move/Up/Cancelled callback 批次；保留 packet_count 及生产既有最后包策略。callback_entry_qpc 在 decoder 前，source_qpc 是既有快照时钟，published_qpc 在 release commit 前，callback_end_qpc 在真实 callback 返回。record_id 为匿名会话编号，不输出指针；尚未解析设备的 callback.device_type=UINT32_MAX，表示 unknown，不默认归为 Touch。

消费按完全匹配 record/generation/sequence/source/admission join，端点包括 consume/process_end/adopt/raster/stored/present_return，process_end 为实际 initializeStroke/consumeLatestSnapshot 返回，可晚于 adopt。callback_outcome 与 publication_succeeded 独立，坏 Up 实际发布 Cancelled 仍要保留。

成功必须通过生产 assembler 的非预测实际几何、Live 当前栅格或 Stored 权威 L2 链、当前 scene/raster/output/frame/content、实际合成和成功 API 全部 gates。Up 需 Stored 终态；deferred Up 的先行 Live 不可冒称稳定完成。首次已确认端点/proof 冻结；成功 Up 回收 pending，不删历史 receipt；失败 Stored 保留到真实 retry/失效。receipt 满时一次实际消费不能双计 seen/drop，frame 消费事实独立于采样成功。

Pen/HardPen/形状/Highlighter 可有真实内容 software Present proof。Laser/橡皮及无可证明贡献项显式 proof_unsupported，仍保存 callback/publication/consume 与帧耗时。published_not_consumed、consumed_no_geometry、adopted_no_raster、rastered_no_present、present_failed、source_invalidated、cancelled、invalid_interval 分别保留；不得用邻近帧、lastConsumedSequence 或有内容标记补成功。

### GPU 与初始化

包1每32个 loop 稀疏候选，可能完全错过实际绘制；不得用旧空循环 GPU 行证明 shader 很快。包2 owner loop 只轮询/准备，首个真实图形叶工作才惰性开始 query。每 device epoch 帧域首8次工作优先、之后每8次候选；prior 域首4次、之后每16次，分母和序号独立，prior frame/loop=0，不能借用已封口帧。

8个固定 timestamp/disjoint slots。GetData 使用 DONOTFLUSH、固定池单轮、S_FALSE 即返回；生产路径不 spin/Flush/等待，不加业务 copy。输出 sampling_domain、candidate/domain_candidate、reason、observed/clipped marker masks、真实 Draw 数、work mask、CPU QPC/epoch/frame关联和 frame_retained。`gpu_ready_draw_samples` 只含 frame 域，prior ready draw 独立计 `gpu_ready_prior_draw_samples`。ready/pending/disjoint/query_failed/invalid/owner_stopped/device_replaced 分别输出，池忙/容量/替换时pending分别计数。draw=0、无匹配帧/epoch、缺频率或marker不能进入 GPU draw coverage。

惰性首 marker 可能截取阶段后半段，clipped mask 必须在分析中说明。query issue/poll 与元数据/记账自身耗时单独导出，不能从原 API duration 任意扣除。

只在 ready、frequency>0、端点单调时计算区间。这是 immediate-context 命令流区间，含 CPU 提交空隙，不是 shader 纯执行；WARP 按 software backend 解读。

冷初始化记录真实 graphics/presenter/RTS aggregate span、各失败 HRESULT、actual driver/feature/adapter/vendor/device/memory/driver version、presenter mode/size、Map NO_OVERWRITE capability、实际 model_dpi_scale/model_dpi/target_fps、已解析 Display snapshot 的目标 DPI/topology/generation/pixel size。细项无 start 时只报告真实结果；目标 refresh rate 显式 unavailable。脚本 WMI 显示列表不能冒充 Draw3 实际目标。

### 诊断包与离线分析

`Scripts/Draw3Performance` 的 CMD → PS1 先校验 manifest、PE 架构、EXE/脚本 SHA，再建唯一目录。六个独立隐藏硬件 console worker 在软件启动前完成，各自 timeout；只可终止自己的 worker。双流持续排空，最终等待 PID/FILETIME/EXE/output 身份与完成握手。软件 timeout 不 Kill，不接管已有实例；本轮 log_file 需约束在原软件 log 目录下。

用户脚本兼容目标为 Win7 PowerShell 2/.NET；用严格 C# JSON 解析和 System.Management，不能依赖 PS3 JSON/CIM 或 Archive。ZIP 不可用保留目录并报告。开发 Build/Test 要求 PS5.1。包装只静态检查新 EXE，不运行未知参数；用 Release 性能包，不能修改常规发布 ZIP。包2必须具备精确能力 literal `INKEYS_DRAW3_DIAGNOSTICS_V2`，manifest package_revision/capture_schema=2、probe_revision=2、profile=`draw3-resource-v2-fixed`，仍只交付7个用户文件。

应用正常退出、身份持有进程已确认停止后，默认顺序执行40个离屏 worker（20 profiles×hardware/WARP）；可显式 SkipResourceProbes。只允许通过已验证 EXE 的 list 获取清单，执行前重新 SHA256 并用 FileShare.Read 持有文件，禁止产品更新替换后向未知 EXE 发新参数。目录使用 c001..c040 防止旧系统 MAX_PATH；身份仍含完整 case_id。每case20s、matrix总≤480s；双流各≤1MiB且持续排空、单raw≤4MiB、matrix≤64MiB，超限保留部分证据。仅持有的自建worker handle可终止；不能按进程名/PID猜测后终止产品。产品未确认停止/异常退出/worker终止未确认时停止后续matrix并明确原因。

### 包2独立离屏资源对照协议

早期 dispatcher 在配置/单实例/UIAccess/窗口前接管 `--draw3-resource-probe-list`、`--draw3-resource-probe-test` 和 `--draw3-resource-probe <case_id> <absolute_existing_case_dir> <session_id>`；同前缀未知/大小写变体非法参数均退出2，不得落入普通 GUI。list/test 是纯CPU。session ASCII `[A-Za-z0-9_.-]` 长度1..128；目录已存在且绝对，独占 CREATE_NEW lock，start/result原子新建不可覆盖。start/result identity 必须与父持有进程的 PID、精确FILETIME、EXE、session/case/profile/backend/输出目录一致，exit与status对应。complete 时每个 phase/iteration 的原始 Draw 次数须与 case 工作量及 gpu.csv 的 draw_calls 交叉匹配；缺行不能纳入完整对比。

每case原生预算8s、cold1+warmup1+measure4、固定最多4096调用；API若在驱动中挂住由外部父期限处理。hardware失败不静默回退WARP。只创建离屏D3D设备/纹理，不创建HWND/swapchain/Host/RTS、不读写用户配置。对照路径显式 `offscreen_embedded_301_302_contract`，记录实际shader资源/字节/FNV身份，EXE SHA提供包加密身份，不能声称完整生产Host重放。

20 profiles覆盖3.2MB/256B墨迹结构化buffer、4.8MB荧光笔、大小合成buffer、48B常量、DISCARD/合法NO_OVERWRITE、有无Draw、32B/4096B实际写入、字段/memcpy/volatile、DEFAULT+UpdateSubresource、2/3/6次提交、1080p clear/copy与小目标readback。NO_OVERWRITE先检查cap，首DISCARD后分配未复用的独立范围；禁止DONOTWAIT与DISCARD/NO_OVERWRITE非法组合。常量字段36B和memcpy/volatile48B对照必须披露工作量差异。

原始文件 probe-start/result/stage.json、calls/resources/gpu.csv 保留CPU墙钟/线程与进程CPU、资源实际描述、各API、迭代、adapter/feature/cap/HRESULT/失败阶段、GPU状态与drain。probe仅在有限batch后显式一次Flush并有界EVENT DONOTFLUSH轮询，**必须 HRESULT=S_OK 且 BOOL ready=TRUE 才完成**；这项同步独立标注，绝不引入生产路径。complete/unsupported/failed/partial 的exit分别0/3/4/5，缺测不填0。

离线分享/改名不要求当前目录等于原 launch 绝对目录；原 immutable 身份之间仍严格相等，采集启动时必须验证原路径。JSON 关键版本/exit 为真正整数，不接受 bool/string/小数；重复身份键、同 publication 换 callback token 不得重复进延迟分母。

分析逐行限界：16 Host、65536 frame、262144 input/GPU/每分布，最慢 100 帧。输入消费批次数按“批/帧”给 mean/p50/p95/max_batches，不混入毫秒分布。分别计算工作时间、连续 active_chain 间隔、同目标/revision/epoch 成功呈现间隔、Down/Move/Up 消费与已证明呈现延迟。nearest rank=ceil(N×p)，有效样本至少 1000 才报 P99。idle 断链；容量或分析上界掉样标部分证据，缺失/非法端点不填零；候选相关性不等于设备型号根因。

## 4. Error Matrix

| 条件 | 行为及证据 |
|---|---|
| 正常模式空 root | 不创建 Session，不执行新增采集工作 |
| 显式 argv 非法/目录不能独占 | GUI 前非零退出，早期失败原由 |
| 持久选项创建失败 | 本次退回普通启动，记录失败，不写配置 false |
| DComp 初始化失败、随后 ULW | 独立 Host 代际和真实失败结果 |
| RuntimeMetrics/Controller/QPC 不可用 | unavailable/status 与累计握手明确，不能 complete |
| callback/receipt/frame 满 | 保留前缀、seen/drop、预算/区间可核查 |
| pending proof 槽满 | proofOverflow 单列；不补成功或扩大热存储 |
| Map/提交准备失败 | preparation failure，与 noop 分别计数 |
| API 失败、非 S_OK、noop | 保留真实结果，排除成功呈现分母 |
| 时钟未知/逆序 | invalid interval，排除相关分布，不 clamp0 |
| GPU 不支持/disjoint/pending | unavailable 或状态原因，不能计 0ms |
| 导出失败/强杀/崩溃 | incomplete/failed；无握手不能虚构完成 |
| launcher 退出但 final 未握手 | 继续等最终身份；超时保留结果、软件不强杀 |
| 原始文件缺失、身份或 schema 不合法 | 分析 incomplete/failed，保留原始文件 |
| full/slow/group/resource 饱和 | 各自账本与覆盖范围；full/slow去重，聚合不造精确P95 |
| 实际GPU样本没有Draw/缺marker | draw coverage不足，不能用空循环时间排除GPU |
| probe不支持/设备失败/超时 | unsupported/failed/partial和阶段原样保留；不回退、不把缺失写0 |
| probe结果身份/EXE hash不匹配 | 拒绝纳入对照，保留异常证据 |
| 产品未确认正常停止/worker停止不确定 | 跳过或终止后续matrix，不终止产品 |

## 5. Good / Base / Bad Cases

- **Good**：真实 Move 发布序号与消费完全匹配，有本帧非预测 raster + 合成 + 同输出成功 API，可统计 callback → consume 和 callback → software Present；保留原始 proof。
- **Base**：使用 Laser/橡皮或建模暂无可见贡献，仍可分析输入消费与计算/帧耗时，呈现延迟显示不支持/无贡献；不使其看起来 0ms。
- **Bad**：上一帧有内容、本帧 noop 或失败；输入被覆盖/过期/取消，或保留前缀耗尽。禁止给它们补成功及用样本 P95 声称全程性能。

## 6. Tests / Assertions

完整 `InkeysRepo.sln Debug|ARM64` 使用动态定位原生 ARM64 MSBuild、同 PowerShell invocation PATH 规范化、至少 5 分钟超时；依赖 PptCOM 不单独构建 Inkeys。复用仓库已有串行/已装依赖参数，不改工程绕过环境失败。

只运行已确认的 GUI 初始化前入口：`Inkeys.exe --draw3-performance-test`、`--draw3-performance-launch-test`、`--config-boundary-test`、`--draw3-resource-probe-test/list` 和 `InkeysHeadlessTests.exe --no-window`。计量 probe 需覆盖 off 无 clock/分配/query、负时钟/mask、固定容量/并发发布、真正 assembler Live/Stored/失败/noop/过期输出、96 次连续笔画的槽回收、receipt 满不双计、真实 initializer prior/跨 Begin。launch probe 覆盖两层 Split、中文空格、独占/同身份、真实失败与 drops/unavailable 完成分类。包2增加14种完成分类（full/slow/resource/group/site账本分离）、leaf精确计量/嵌套/逆序/跨Begin、固定容量及资源代次、假query S_FALSE/marker/独立域调度与真实payload账本。resource纯CPU probe验证40case协议、精确拒绝、写入偏移和EVENT完成状态；不能用CPU探针冒充D3D驱动执行。

脚本 `Test-Draw3-Performance.ps1` 覆盖有效长帧/idle、失败/noop、输入去重/未知/逆序/部分证据、严格 JSON 类型/重复键、分享改名、硬件 timeout、双流排空、进程身份、CMD exit 和包 7 个用户文件+SHA。包2 `Tests/Test-V2-Fixtures.ps1` 与 fake worker 另测v1/v2分析、partial/缺文件/invalid/重复call去重、frame/prior GPU覆盖、40case身份/超时/大输出/继续、短目录和ZIP stream SHA。用 WindowsPS5.1 与 pwsh 验证，但不能据此声称真实 Win7 PS2 已通过。

验收保存命令、自然退出码、第一有效错误与日志。`git diff --check`、BOM/CRLF/EOF、i18n sync/check、项目登记镜像必须通过；HEAD 在 core.autocrlf=true 时为 LF，原工作树 CRLF 不算漂移。不启动真实/隐藏 HWND、RTS/GUI 或 Computer Use，除非用户另行明确授权。已静态确认无HWND的独立CLI离屏D3D probe可在父超时内测试，必须与真实产品GUI测试区分记录。

## 7. Wrong / Correct

| Wrong | Correct |
|---|---|
| 发布返回后读 latest snapshot 作为本 callback | 实际 writer release 前锁存 key，完全身份 join |
| 借旧 Down-only 指标当完整 Move/Up | 新旁挂保留各 phase，旧 RuntimeMetrics 语义不变 |
| 统计只发生在成功保留槽之后 | seen/facts 独立；retained/drop 分别核对 |
| 空闲初始化写上一封口帧或丢弃 | prior buffer + 跨 Begin 拆段，wait 单列 |
| Up 成功仍占 pending 槽 | 回收旁挂槽，历史 receipt 保留 |
| failed ULW 被 fail lambda 改为未调用 | actual attempt 单调事实 + 原 HRESULT |
| geometry + model + upload + present 子段求和 | inclusive 单列、真正帧端点作总耗时 |
| GPU S_FALSE 反复 Flush/等待 | DONOTFLUSH 单轮，显式 pending |
| launcher exit0 或目录里有 CSV 就 complete | final PID/FILETIME/EXE + 完成握手 + 全文件/计数 |
| 被移动的结果硬比当前绝对目录 | 离线比原身份，采集时比真实启动位置 |
| 打开旧 EXE 探参数、默认 Debug 性能包 | 静态能力/manifest校验，正式 Release 新 EXE |
| 自动勾其他调试、热路径逐点写日志 | 独立显式开关、有界内存、停机后导出 |
| full+slow当两个调用分母 | 按call_serial去重，slow仅补全前缀后异常 |
| full满后仍声称明细全程P95 | 公开覆盖/掉样，all-call只给count/total/min/max |
| 把prior GPU当绘制帧、孤立ready当shader很快 | 域/epoch/frame/Draw/marker完整匹配才纳入 |
| EVENT GetData S_OK就已drain | 同时检查BOOL ready，S_OK/FALSE有界继续 |
| hardware失败后静默换WARP再比较 | 每case backend固定、unsupported/failed独立 |
| 常量字段36B与memcpy48B声称单因素 | 披露实际字节和其他差异，只给有限解释 |
| EXE更新后沿用旧hash发新argv | 执行前重验SHA并持读锁覆盖matrix |
