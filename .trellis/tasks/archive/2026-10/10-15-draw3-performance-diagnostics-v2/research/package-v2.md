# Research: Draw3 诊断包 v2 的脚本、兼容与离线归因

- Query: 在一次外机回传中补齐兆芯 Draw3 资源更新慢点、真实 Draw GPU 覆盖和有界 HW/WARP 无窗口对照，如何最小扩展现有 CMD/PS1 包、manifest、分析器和离线测试？
- Scope: internal（代码和用户提供的原始诊断只读；接口部分为设计建议）
- Date: 2026-10-15

## Findings

### 当前代码与规范位置

| 文件 | 作用与关键位置 |
| --- | --- |
| `.trellis/workflow.md` | 当前任务研究持久化、角色边界与实现/检查流程。 |
| `.trellis/spec/native-desktop/draw3-performance-diagnostics.md` | 普通启动零新增采集、32 MiB 新 payload、64 MiB 与旧计量合计、Win7 PS2、身份握手、有限前缀和 inclusive stage 合同。 |
| `.trellis/spec/native-desktop/build-and-compatibility.md` | 既有完整 Solution/架构/工具链和 Win7 声明与实际验证的区别；根 AGENTS 的明确构建指令优先。 |
| `.trellis/spec/native-desktop/errors-logging-and-resources.md` | 线程、D3D/COM/句柄与 shutdown 所有权。 |
| `Scripts/Draw3Performance/Run-Draw3-Performance.cmd:3` | `%~dp0` 固定找到同目录 PS1，保留 PowerShell 退出码，pause。 |
| `Scripts/Draw3Performance/Run-Draw3-Performance.ps1:17` | 硬件字段；六类独立 worker 在产品启动前退出，15 秒默认单项 timeout。 |
| `Scripts/Draw3Performance/Run-Draw3-Performance.ps1:141` | 校验 manifest/EXE/脚本，创建会话，启动产品、等待 owner、复制 log、调用分析器。 |
| `Scripts/Draw3Performance/Run-Draw3-Performance.ps1:159` | 默认 `$scriptDirectory/Inkeys-Draw3-Diagnostics`；显式 `-OutputRoot` 覆盖。必须保留。 |
| `Scripts/Draw3Performance/Performance.Common.ps1:11` | 随脚本 Add-Type 的严格 JSON/单行 CSV C#；不依赖现代 JSON/CIM cmdlet。 |
| `Scripts/Draw3Performance/Performance.Common.ps1:136` | JSON 最大 4 MiB；真正整数 `Int64` 检查；原子临时文件发布；流式 SHA256。 |
| `Scripts/Draw3Performance/Performance.Common.ps1:173` | launch/completion session/PID/FILETIME/EXE/output 不可变身份；离线允许移动改名。 |
| `Scripts/Draw3Performance/Performance.Common.ps1:196` | 等最终进程而非 bootstrap；只持读/同步句柄；timeout 不杀产品。 |
| `Scripts/Draw3Performance/Analyze-Draw3-Performance.ps1:42` | 流式 CSV、64 MiB 单文件、16 KiB 单行、重复列/非法数值拒绝。 |
| `Scripts/Draw3Performance/Analyze-Draw3-Performance.ps1:66` | nearest rank、少于 1000 样本不报 P99。 |
| `Scripts/Draw3Performance/Analyze-Draw3-Performance.ps1:125` | 工作/活动间隔/同目标成功呈现间隔、prior、top100。 |
| `Scripts/Draw3Performance/Analyze-Draw3-Performance.ps1:276` | 当前 GPU ready/频率/epoch/frame join；没有检查匹配 CPU 的 draw_calls>0。 |
| `Scripts/Draw3Performance/Build-Package.ps1:9` | 静态 PE machine；第 20 行仅宽/窄字面量 `--draw3-performance`；第 35 行 manifest schema/capture/raw 全为 1。 |
| `Scripts/Draw3Performance/Test-Draw3-Performance.ps1:23` | 可控 Frame/Input/Capture 合成 fixture；数值/身份/状态测试。 |
| `Scripts/Draw3Performance/Test-Draw3-Performance.ps1:168` | 无窗口 mock bootstrap/final、timeout 不杀 final、双流压力。 |
| `Scripts/Draw3Performance/Test-Draw3-Performance.ps1:215` | 静态 fake PE 包测试；确切 7 文件；最后 PS2 禁 API 和 AST 检查。 |
| `Inkeys/IdtMain.cpp:1255` | PerformanceLaunch.ParseArguments 在配置、单实例和窗口前。 |
| `Inkeys/Launch/IdtPerformanceLaunch.cpp:138` | 拒绝未知 `--draw3-performance*` 前缀；现 Probe 参数只允许精确两 argv。 |
| `Inkeys/Launch/IdtPerformanceLaunch.cpp:243` | launch/completion 的 schema_version=1 与进程/版本/edition/架构/UIAccess 身份。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Performance.cpp:751` | 当前 gpu.csv 11 列，无样本 draw 数与选样原因。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Performance.cpp:795` | 当前 session schema=1、frame/input/receipt seen/retained/dropped、预算和 GPU 环形占满数。 |

### 现场证据必须成为回归夹具的原因

原始目录：`D:/Personal/Downloads/Inkeys-Draw3-Diagnostics/draw3-20261015-042937-3b356c1567c74be8a83e0a68a1684fa8`。

- session：169/169/0 frames，460/460/0 callbacks，23/23/0 receipts；QPC 频率 14318180。payload 实际 26390832，新预算 33554432，legacy 上界 19995544。
- 26 帧 `draw_calls>0`，其余帧的低耗时显著拉低全部 `frame_work` P50（0.054 ms）。工作分布必须保留，但不能把全部循环 P50 的倒数称为实际 FPS。
- gpu.csv 9 行。序号 32/64/96/128/160/192/224/256 匹配的 CPU 帧全部 draw_calls=0；第 288 行没有 marker。极小的 GPU geometry 区间没有真实 Draw，不能证明 shader 很快。
- 第 36 帧：3 Draw、6 Map、上传 1648 B、墙钟约 414.19 ms、owner CPU 406.25 ms；upload_map 约 197.40 ms，而 copy 约 0.0037 ms。还缺 Map 资源种类、Unmap 与其他图形调用归因。
- 现有 analyzer `facts`（第 165 行）甚至没保留 draw_calls；v2 必须把它加入 join facts，旧数据也据此过滤空工作 GPU 归因。

### v2 总体脚本流程：一次启动、产品后测隔离

保持当前 CMD 名与默认目录，不用额外软件、Python、Excel或 WMF 升级。推荐仍可只交付 7 个用户文件：probe orchestration 放进 Run、验证/通用函数放 Common、分析放 Analyze；开发测试 fixture 不入包。

1. **先静态校验**：manifest 格式/整数类型、EXE SHA、架构及全部用户脚本 SHA；确认正式 v2 marker 与预期 argv/profile。不能运行旧 EXE 去试未知参数。
2. **硬件阶段**：复用六 worker，产品开始前完成；记录 query/version/exit/stdout/stderr/timeout，不在测产品时并行 WMI。
3. **真实产品人工短轮**：仍用 `--draw3-performance <root>`，依现有 UIAccess/最终 owner 身份等待；用户正常复现并退出。不要通过脚本改变产品 driver、算法、帧率或输入。
4. **后测门**：只在持有句柄确认最终 owner 退出，且没有本包产品残留时运行无窗口矩阵。`Wait-PerfFinalProcess` 的 launch timeout、权限失败和 capture timeout 均不能被解释为“产品已停止”。如果无法证明已停止，probe_status=`skipped_product_not_confirmed_stopped`，保留现场证据并结束；绝不 Kill 产品。
5. **资源对照矩阵**：每 case 独立创建一进程，不复用可卡住的 device/context；按 HW/WARP 和 profile 固定 case 清单顺序运行，避免并行抢 GPU。只终止此脚本刚创建并持有句柄的 probe；单 case 失败/unsupported/crash/timeout仍继续其他 case；若自己的 worker 都无法在 bounded cleanup 内退出，停止后续矩阵并标明原因。
6. **自动分析**：产品与 probe 分别记录完整性和性能，不用 probe 成功掩盖产品失败；随后同一 report 汇总可比较对照、失败/缺失、覆盖范围和下一步候选。自动保留全部 raw、版本、manifest、逐 case 标记和退出结果，用户只需回传整个目录。
7. Zip 维持当前 optional Archive 能力；Win7 PS2无模块时目录仍是完整交付物。CMD显式告诉用户“产品退出后还会有几分钟无窗口图形对照，完成前保留此窗口”，避免用户提前关 CMD。

### 建议冻结的 manifest/入口与版本接口

以下 CLI/marker 是主会话在研究期间确认的 v2 外部协议，其余字段建议由 native/script owners 在 design 中冻结；它们尚不是已实现能力。

- manifest envelope schema_version 可继续 1（含义只是包格式），新增 `capture_schema=2`、`diagnostic_package_version=2`、`capability_marker`、`resource_probe_command`、`resource_probe_schema=1`、`probe_revision=2`、`raw_schemas` 各文件版本。
- 不能仅在 EXE 搜到 `--draw3-performance` 前缀就称 v2。正式 marker 为 `INKEYS_DRAW3_DIAGNOSTICS_V2`，同一已引用常量由正式早分发路径使用并写入 probe 身份，避免 /Ox 去掉未引用字面量；Build-Package 只读扫描与 SHA。这个静态 sentinel 仍不是在目标系统实测通过的证明。
- 正式 case 入口：`Inkeys.exe --draw3-resource-probe <case_id> <absolute_existing_case_dir> <session_id>`，含 EXE 共5argv；backend 属于 case 清单定义，不能任意外部字符串驱动。精确数量、case 白名单、revision/版本和目录验证，错误均在任何窗口/配置/UIAccess前非零返回。root负责 IdtMain 早分发和工程登记。
- `Inkeys.exe --draw3-resource-probe-list` 输出 JSON case 列表（仅在完整 marker + manifest + EXE SHA 验证后调用），列表包含 schema/revision、case ID、backend、资源大小和固定工作量，collector 不用猜case、不发未知参数；list 必须纯CPU，无设备/窗口。`Inkeys.exe --draw3-resource-probe-test` 是纯CPU协议/数值probe，不能混入真实D3D测试。
- 现有 `IdtPerformanceLaunch.cpp:138` 对未知 `--draw3-performance*` 前缀硬拒绝；已选的resource命名避开它，仍建议在该ParseArguments前独立早分发，使错误case参数永不落入普通产品启动。
- `--draw3-performance-test` 等原来纯计量测试不启动 D3D；不能把真实 GPU microbenchmark 塞进此入口，让日常无窗口测试暗中依赖显卡。
- launch.json/completion.json 身份 schema 建议继续 1，只增 capture/profile/capability 字段；这样原来严密的最终 owner 协议无需改写。新 Host session/frames/gpu 按实际字段变更声明版本（推荐 session=2、frames=2、gpu=2，initialization/input 兼容可保持 1；新 resource/call=1）。
- collector只启动与 v2 manifest 匹配的新 EXE；Analyze 兼容 v1/v2。v1 不要求新 CSV，应显示 `detailed_graphics_coverage=not_collected_by_v1`，而不是把旧文件标坏或把缺值填零。v2 声称支持而少文件/少列，必须 incomplete。
- 对未知 schema/profile 明确 unsupported、不自动猜列、不尝试新参数，不重写原始文件。

### 建议 raw 数据接口（端点与身份优先）

使用 snake_case、CSV整数 QPC与原HRESULT，保持CRLF和UTF8。只有在分析时换算ms。以下为最小可用于后续严格归因的字段集合：

**`resources.csv`：每个匿名 resource_id/设备代际的描述**

`resource_id,device_epoch,resource_kind,role,byte_width,width,height,format,usage,bind_flags,cpu_access_flags,misc_flags,structure_byte_stride,created_qpc,destroyed_qpc,create_hresult`。

- role 应区分 stroke/ink vertex、composite rectangle、constant buffer、ULW staging、snapshot/texture 等实际路径；对象ID匿名，不写地址或 COM pointer。
- 同一资源重建必须换ID；Create失败也保留尝试记录与HRESULT，未创建的对象不能假冒有效 resource。
- 可用值/未采集/失败通过状态区分，不以0字节假造未知描述；描述在创建时捕获，不能每次 Map 前调用 GetDesc 增加热开销。

**`graphics-calls.csv`：每次已保留API/写入调用的时间与关联**

`call_serial,frame_serial,device_epoch,resource_id,related_resource_id,stage,operation,start_qpc,end_qpc,valid_interval,map_type,map_flags,subresource,offset_bytes,size_bytes,hresult,hresult_available,attribution`。

- operation 至少涵盖 Map、WriteCopy、WriteConstant、Unmap、Draw/DrawIndexed、UpdateSubresource、CopyResource/CopySubresource、Clear、关键 Bind/Set 与其他本轮未拆开的实际调用；每种 operation 的 kind/phase 列表写入 session。
- `attribution=frame|prior|initialization|unassigned`，显式处理 frame.start前的首笔工作。不能让 pre-frame 调用晚绑定成下一帧“工作时间”；需要 prior 对应序号或原始端点帮助 join。
- void API（Unmap、Draw、绑定）无 HRESULT，应 `hresult_available=false`，不要把虚构 S_OK 当作硬件成功。Map失败后的写入/Unmap不得伪造。
- CPU API耗时、memcpy耗时与整个更新序列 inclusive span不能简单相加；要么每行是互不嵌套叶调用，要么显式 parent/span_kind，并只计算非重叠区间并集用于 frame归因。跨frame边界的统计按交叠长度归属，原始端点不可裁掉。
- 同一 frame 的可解释时间=有效叶调用区间并集，`unattributed_work_ms=frame_work_ms-union_within_frame_ms`；若缺样/invalid/重叠不能声称精确解释率。prior独立，不混进frame分母。
- 不要求保留无限个快速调用。建议固定前缀 call 容量（可由native owner按实际sizeof选32768或65536），并继续维护按 role/operation 的固定聚合 count/total/max/fail/bytes，以免详细前缀满后连后续慢点都不可见；若追加top慢调用固定表须清楚声明与prefix不是两个可相加分母。

**session v2补充合同**

- `raw_schemas`、`capabilities`、`graphics_call_capacity`、`graphics_calls_seen/retained/dropped`、`resource_capacity`、`resources_seen/retained/dropped`、每种有效/无效/未知/失败计数；CSV行数与retained、seen=retained+dropped严格核对。
- 固定allocation账本包括新增字段、描述表、event/aggregate表；不得仅声称32MiB而忽略对象数组。当前已用约25.17MiB，不能再无条件加32MiB。保留32MiB新增payload/与legacy合计64MiB合同，具体capacity由`sizeof`和运行账本证明。
- `first_retained_call_qpc,last_retained_call_qpc,graphics_detail_policy,graphics_detail_threshold,detail_dropped_after_qpc` 等让报告明确保留范围与取样偏向。聚合分母和明细分母分别给出。
- completion.export_status需要与新stream的导出成功/seen/drop匹配。GPU ring occupied、unsupported、query failure与真正sample容量耗尽分开，不能把所有 skipped算同一种data loss。

**gpu v2最少补充**

- 选样应按真正 Draw帧而非每32loop；优先捕获首批真实绘制/首笔/恢复帧，然后按 Draw帧稀疏选样；仍使用固定8query槽、DONOTFLUSH，不能 spin/Flush/等待。
- 每GPU样本至少 `sample_id,frame_serial,device_epoch,sampling_reason,draw_calls_in_sample,geometry_draw_calls,composite_draw_calls,readback_calls` 及既有区间/status/frequency/HRESULT。stage没有对应Draw/Copy时，不能进入该阶段执行成本分布。
- session 给出 `draw_frames_seen,gpu_draw_candidates,gpu_samples_started,gpu_ready_draw_samples,gpu_slots_busy_skips,gpu_capacity_skips,gpu_query_failures,gpu_pending_at_stop` 与分阶段覆盖。具有ready tiny区间而draw=0，应明确标empty_work，不纳入shader候选。
- 协议需区分loop_serial与真实frame_serial；跨epoch、未保留frame、marker缺失保留原因。命令流区间仍含CPU提交空隙，不命名shader_busy。

### 独立资源 probe 的落盘与调度接口

建议布局：

```text
<session-root>/
  launch.json / completion.json / host-NNNN/...   # 原产品证据
  resource-probes/
    matrix.json                                 # 固定profile与expected cases，父进程独立写
    hardware-<case-id>/
      request.json                              # 本轮nonce/目标/版本/限制，父写
      probe-start.json                          # worker真实PID/FILETIME/EXE/case/adapter
      probe-result.json                         # worker原子完成、实际状态/退出码/计数
      calls.csv / gpu.csv / resources.csv        # 原始对照
      worker-status.json                        # 父观测exit/timeout/终止结果
      stdout.txt / stderr.txt
    warp-<case-id>/...
  report.md / summary.json
  graphics-call-summary.csv / top-long-graphics-calls.csv
  top-long-frames.csv / resource-probe-comparison.csv
```

- `probe-result.json` 固定 `schema_version=1,probe_revision=2`；request/started/result至少case_id、session_id、nonce或等价唯一会话身份、requested_backend、actual_backend、输出原路径、EXE版本/SHA或manifest关联、进程PID/FILETIME。结果保留adapter/vendor/device/driver/feature与逐operation耗时、全部resource descriptor和实际flags，不用WMI替代实际选择。
- 每个case独立目录，创建后不重用，不自动retry污染初次结果；若需要warmup/iteration写入同一个case内的显式phase。Warmup成本单列，不能把首轮异常去掉后只报“稳定”。
- 主会话边界：outer timeout每case例如20秒、总matrix deadline不超过8分钟；建议native内部deadline 5–10秒与固定最多8–16 iterations、最大case数32；具体值需要本次慢设备140ms/调用估算，实际profile固定并manifest记录。不能一个case数百次调用无界等待。超时是证据，不能替换成0ms或重跑到成功。
- 建议profile覆盖：生产等量大VB与两点小VB、当前NO_OVERWRITE/DISCARD更新、CB DISCARD、独立合成VB、DEFAULT+UpdateSubresource、读回staging Map、有限资源ring/复用差别；HW/WARP同资源大小/操作量/iteration才能比较。native owner决定可实现的最小独立case清单，脚本不复制D3D逻辑。
- 不需要每个backend×size×策略做完整笛卡尔积；有清晰case对比因素即可。每case调用row <=4096、单rawCSV<=4MiB、JSON<=4MiB、stdout/stderr各<=1MiB、全部probe磁盘预算例如64MiB，触顶写partial/limit。产品raw与probe预算分列。
- Start-Process需`-WindowStyle Hidden`；优先ProcessStartInfo UseShellExecute=false/CreateNoWindow=true精确argv与持有句柄。两个流必须同时持续排空；不能顺序ReadToEnd；有限保留后继续drain并统计discarded bytes，避免管道满使worker假卡死。已有产品Start-Process按文件重定向做过双流fixture，不能重构到更差路径。
- 终止只针对本次成功创建且持有句柄的probe，不能GetProcessesByName后批量Kill，更不能把Inkeys.exe同名视为可杀。Kill后最多再WaitForExit固定几秒，不用无限WaitForExit；若未退出，停止后续GPU对照避免争用。
- matrix状态complete/partial/skipped，与产品capture完整性独立。单case退出非零且result自称complete、wrongnonce、wrongbackend、missingstarted、completion缺失或schema不符都不可比较。
- 比较只在匹配相同case/profile/大小/操作量/已验证身份，且两backend成功的有效分母内计算ratio；WARP不是正常产品表现，不能用更快WARP直接认定driver Bug，软件worker CPU也不能拿单ownerCPU当全进程CPU。

### 自动分析应直接回答的问题

1. **实际绘制有多慢**：所有loop工作分布保留，另报draw_calls>0帧、实际API呈现帧、main/selection输出、tool/epoch分组的工作/active/成功呈现间隔，逐个给分母。不要无条件把P50倒数称FPS；若展示1000/interval，写“同链呈现间隔的等效频率”并给区间样本数。
2. **时间去哪**：resource role×operation汇总 count/total/P50/P95/max/fail/bytes；每帧top3具体调用及归因coverage；top100长调用含原host/row/call/frame/resource/epoch，关联当时帧work/ownerCPU/Draw/Map/输入数量/目标/dirty。归因不足明说，不把inclusive double count造100%。
3. **GPU是否覆盖慢帧**：有多少Draw帧、多少真实Draw GPU样本、阶段匹配数/最长被覆盖帧work；没有Draw覆盖时明确“不能评价GPU绘制/排除shader”。Tiny emptyloop不能主导结论。
4. **输入是否被长帧拖住**：仍保留Down/Move/Up完整证据；已发布未消费不能叫诊断丢包，实际callback间隙与frame内输入批次数单列。无需把本次v2扩大成触控板硬件原始坐标任务。
5. **同机对照排除什么**：小/大资源、不同Map/update路径、HW/WARP配对的实际细分耗时，先呈证据再给“当前路径对特定backend敏感”的候选；禁止直接从CPU/GPU型号下结论。
6. **还缺什么**：正常停机/数据prefix/drop/analysis上界、case失败/超时/不支持、GPUpending/真实覆盖、profile差异，做到外机一次回传后可精确判断缺口。

### 可执行的最小验证计划（不打开任何产品或GUI）

- 延用全部v1合成fixture，v2分析器须继续给旧包有效结果；用输入目录只读复制到任务允许的验证输出，不能改用户原始包。原兆芯摘要应自动显示draw与all-loop显著分离、GPU真正Draw覆盖0。
- 新v2fixture：同帧包含多个resource的Map/Write/Unmap，已知总100ms但父span嵌套，断言union不超总、非加和；prior调用不记进frame、无frame/epoch mismatch不补关联。
- 详细CSV seen/retained/dropped、重复call ID/资源ID、缺行、截断/过长行/非法时间/void API无HRESULT；drop后聚合继续而明细coverage变partial；zero是已测零，unknown是null。
- GPU fixture：matchedframe但draw=0且timestamps极小、真正draw匹配、错误epoch、onlycomposite、pending/disjoint/queryfailure；断言空GPU不入draw分布，首批选样nativeprobe另测。
- fake native probe用无窗口console/PowerShell fixture做created/started/result三层；success、failed、unsupported、hangtimeout、crash、wrongnonce/wrongexe/FILETIME/wrongbackend、少结果文件、超大双流。第一个case失败后第二case仍运行；所有者仍活时矩阵不启动；timeout只终止自己持有的worker。
- Build包fixture增加准确v2sentinel/profile、PE/EXE/每用户文件SHA、禁止unknown旧EXE发probeargv；旧literal-only PE不能生成v2包。7文件若保持则继续exact7；新增用户脚本必须登记hash和文件数，不能只放目录忘入ZIP。
- WindowsPS5.1与pwsh执行script offline tests，所有用户PS1保持PS2禁API检查、UTF8 BOM（PS1中文）/CRLF/CMDASCII与syntax AST；不宣称Win7PS2实机已验证。
- 原生编译与纯计量测试由main统一按根AGENTS执行；本研究不运行`--draw3-resource-probe`（它会创建真实D3D），不启动产品/RTS/任何HWND；即使是无窗口GPU测试也需把验证边界和结果写清。

## External References

本主题仅核对仓库代码、现有规范和用户原始现场文件，没有依赖外部资料或把未核对的新API行为当作事实。D3D具体case合法性、GPUtimestamp语义由native研究/实现依据正式API和现有生产调用再确认。

## Related Specs

- `.trellis/spec/native-desktop/draw3-performance-diagnostics.md`：核心合同，尤其普通模式隔离、预算、GPU、身份和PS2边界。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：构建与真实系统兼容的证据等级。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：资源/线程/句柄清理及不得擅杀产品。
- `.trellis/spec/native-desktop/draw3-integration.md`：产品独占Draw3 device/presenter/RTS所有权（probe独立且不更改产品选型）。

## Caveats / Not Found

- 研究时没有正式resource probe入口、v2sentinel、per-resource/call raw schema；CLI/marker/revision已按主会话确定的外部协议修订，但不能向用户报告已实现。
- 现有Script/Analyze能说明GPU的空loop问题，但缺少通用的probe parent/worker测量与schema；需要新fixture，不能只加README。
- 用户愿望是尽量一次取齐，不等于可以保证一次确认所有驱动内部或真实硬件问题。即使有微基准HW/WARP对照也不能代替driver内部堆栈或实机复测修复效果。
- 当前新payload已用26390832B，新增详细采集有实际预算压力。不能为详细而无限开数组/逐调用I/O；native接口冻结时必须交付准确sizeof和容量账本。
- 本研究只写此文件；没有修改源码/脚本/规范/用户现场文件，没有进行git操作，也未启动任何软件或GUI。
