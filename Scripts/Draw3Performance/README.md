# Draw3 性能诊断包 2

本包收集 Draw3 的硬件/初始化环境、真实绘制帧、按调用位置与匿名资源代次区分的图形调用耗时，以及真实 RTS 输入到软件成功呈现的延迟。新版能力标记为 `INKEYS_DRAW3_DIAGNOSTICS_V2`，包 revision/capture schema 为 2，启动/完成身份 schema 仍为 1。用户运行 CMD 即会为本次启动开启性能采集，无须提前在实验选项里打开其他调试输出，也不会修改用户的性能输出配置。诊断包不自动输入、不更改帧率或绘制算法，结果保存在本机，不自动上传。

先完整解压与软件架构匹配的诊断包，正常退出所有已运行的 Inkeys。双击 `Run-Draw3-Performance.cmd`，硬件查询结束后软件会按原有 SuperTop/UIAccess 流程启动；需要系统权限提示时按正常方式处理。开始书写、复现问题，完成后从软件正常退出。**软件正常退出后，采集器还会顺序运行最多 8 分钟的独立无窗口 HW/WARP 资源对照，请在完成前保留 CMD 窗口。** 对照完成后自动生成中文 `report.md` 和 `summary.json`；CMD 会显示结果目录和退出码。启动器先退出不代表测试完成，采集器等待最终进程的身份与导出握手。

建议每轮用 **30–45 秒** 完成慢速书写、快速转弯、连续 Down/Move/Up 和问题最容易出现的工具，再留几秒空闲后写第一笔；不同工具、切页/撤销、间歇卡顿可以分别运行几轮。采集使用固定容量，达到容量后保留前缀且计掉样，不覆盖已有样本。高帧率/高输入频率会较快用满容量；长时间运行的分位数可能只代表前半程，报告会标为部分证据。偶发问题若发生在容量满后，需要用新一轮结果定位。软件崩溃/强制退出可能丢失尚未导出的内存数据，硬件和已落盘文件会保留。

可在终端指定 EXE（必须与包 manifest 的 SHA256 一致）及输出位置：

```cmd
Run-Draw3-Performance.cmd "D:\My Inkeys\Inkeys.exe" -OutputRoot "D:\Draw3 Results" -Zip
```

默认输出在 CMD 文件所在目录的 `Inkeys-Draw3-Diagnostics` 下（不受命令行当前工作目录影响）；可用 `-OutputRoot` 指定其他位置，每轮使用时间戳和 GUID 新建目录。`-LaunchTimeoutSeconds 180` 控制最终启动握手等待；`-CaptureTimeoutSeconds 0` 表示等用户正常退出。设置采集等待上限后超时只结束采集器，不杀软件。超时后软件可能仍在运行或稍后启动，先正常退出它再开始下一轮；文件导出齐全后可重新运行离线分析。已有实例不会被关闭或接管。产品仍在运行、最终停止状态未知、没有正常退出证明或发现同名产品残留时，资源矩阵自动跳过；缺少 completion 不能作为产品已经停止的证明。可以显式追加 `-SkipResourceProbes` 跳过后测，报告会保留跳过原因。

`-Zip` 在带 Archive 模块的 PowerShell 上生成 ZIP，Win7 默认 PowerShell 没有该模块时保留完整目录并记录不可用原因。也可手动压缩整个结果目录回传。不要只提供 `report.md`：原始数据和初始化文件是复核依据。可在 `observations.txt` 记录卡顿时间、工具、动作、空闲恢复/切页及体感现象。

用户采集器和分析器以 **Windows PowerShell 2.0 / Win7 已有 .NET API** 为兼容目标，无需 Python、Excel、WMF 升级或 JSON/Archive cmdlet。JSON 使用随脚本编译的严格解析器，拒绝重复身份键；硬件用 System.Management/WMI。开发端 `Build-Package.ps1` 和测试脚本需要 PowerShell 5.1 或更新版本。当前 Windows PowerShell 5.1 / pwsh 的离线测试不代表真实 Win7 PowerShell 2.0 或低性能目标机验证。

离线重新分析：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Analyze-Draw3-Performance.ps1 -CaptureRoot "D:\Draw3 Results\draw3-..."
```

主要文件：

| 文件 | 内容 |
|---|---|
| `collector-session.json` / `diagnostic-manifest.json` | 本轮参数、EXE/脚本身份、架构、版本与 SHA256 |
| `hardware.json` / `hardware-*.json` | 测试前 OS/CPU/RAM/GPU 驱动/显示/电源快照及逐项缺失、错误、超时 |
| `launch.json` / `completion.json` / `collector-status.json` | 最终进程 PID/创建时间/EXE、真实导出状态和启动器退出码 |
| `host-NNNN/session.json` / `initialization.json` | 每代 Host 的 QPC 频率、固定预算、seen/retained/dropped、真实设备/presenter/初始化结果 |
| `host-NNNN/frames.csv` / `input-events.csv` / `gpu.csv` | 原始 CPU 阶段、RTS callback 批次/内容 proof 和按真实图形工作候选采样的 GPU 命令流区间 |
| `host-NNNN/resources.csv` / `graphics-calls.csv` / `slow-graphics-calls.csv` / `graphics-groups.csv` | 资源描述/设备代次、Map/写入/Unmap/绑定/复制/清理/Draw/Present 细项、独立慢前缀及全程有界分组 |
| `graphics-call-summary.csv` / `top-long-graphics-calls.csv` / `frame-graphics-summary.csv` | 全程分组总量/最大值、有限明细分布、最慢 100 调用、逐帧 leaf 与明细区间并集/未归因残差 |
| `resource-probes/matrix.json` / `cNNN/*` / `resource-probe-comparison.csv` | 后测固定 profile、原始 request/start/result/stage、父进程退出/超时/双流账目、逐 case 原始 CSV 与可比 HW/WARP 结果；短 ordinal 目录避免重复长 case 名 |
| `summary.json` / `report.md` / `top-long-frames.csv` | 有效分母、分位数、完整性问题及最慢 100 帧相关因素 |
| `bootstrap.stdout.txt` / `bootstrap.stderr.txt` / `ordinary-logs` | 原启动器双流及本轮普通日志辅助证据；不作为最终 owner 唯一证明 |

`complete` 表示数据完整性验证完成，不代表性能通过。容量掉样、缺失文件、非法时间、失败握手会给出 `incomplete` 或 `failed`；无有效帧为 `no_data`。分析逐行读 CSV，最多 16 Host、65536 帧、262144 输入/GPU 行、每分布 262144 数值。达到分析界限也标部分证据，不无限缓存 CSV。百分位只对保留的有限有效样本计算；最近秩方法 `ceil(N×p)`，至少 1000 个有效样本才报告 P99。

帧的 geometry、ingress/Modeler、上传和 present/ULW 子段存在嵌套，不能求和。`prior_*` 单列 frame.start 之前的首笔/初始化工作，不加入 frame_work 或 owner CPU；down/consumed 计数已包含携入 prior，不能重复求和。慢帧表按 `max(frame_work, 各有效 prior 阶段, 活动间隔, 成功呈现间隔)` 排序，保留首笔慢和低工作时间的间歇卡顿线索；`diagnostic_peak_ms` 排序值不是总耗时。工作时间、连续活动帧间隔和成功呈现间隔分别统计，空闲后的第一帧不与前一轮连接；deadline/idle wait 记录在工作结束后的前一帧，不能仅凭当前帧的工作时间判定间隔异常原因。GPU v2 优先每代 frame 域前 8 个真实图形工作候选，再每 8 个候选稀疏采样；prior 前置工作独立按前 4 个、再每 16 个候选采样，不占用 frame 首批机会。固定 8 个非阻塞 query 槽；Map/绑定等候选仍可能没有 Draw。报告要求同帧/epoch、实际 Draw 数、完整 marker 和有效频率后才统计绘制阶段；空循环、缺 marker、pending/disjoint/query_failed 保持不可用。惰性开启会标记截取边界，命令流区间含 CPU 提交空隙，不能称为 shader 纯执行时间。分析器仍兼容 v1，每 32 loop 的旧数据按 CPU Draw 事实过滤，并明确缺少细项和逐阶段覆盖。RTS Down/Move/Up 与实际工具分别统计，`effective_tool` 来自实际准入工具，未准入为 unknown；批次包数保留，但生产既有策略只消费其最后包。消费延迟与有真实内容 proof 的成功呈现延迟分开，API 前 Map/Copy 准备失败与无 API 的 noop 分开，失败/noop/过期身份/无贡献不算完成，Laser/橡皮等不支持项保持不可用。

实际性能优先看 `render_frame_work`（`draw_calls>0`）、`primary_render_frame_work`、`ink_render_frame_work` 和真实 API 呈现帧的分布；全部循环 P50 可能被空循环拉低，不能取倒数声称实际 FPS。输出目标、工具 mask、设备 epoch 分组见 `render_groups`。输入中的已发布未消费属于生产链观测，不能称为诊断丢包；采集容量掉样单列。

新增调用存储为完整前缀 8192 条、独立 ≥1 ms 慢前缀 4096 条、2048 个固定分组、512 次资源创建尝试。完整/慢前缀可能包含同一次调用，离线按 call_serial 去重；后者不是完整全程 top-N，满后同样停止保留。全程分组继续维护 count/total/min/max/fail/bytes，容量满的分组另计。明细 P50/P95 只代表有选择偏向的保留样本，不能称为全会话分位数。逐帧固定 leaf 统计在明细饱和后继续；原始细项区间并集仅解释已保留、非嵌套且 accounting_valid 的片段，prior、跨帧未知、父子 span 不重复相加，非法计量时不报告精确残差。void API 没有 HRESULT，不补造 S_OK；未注册资源和缺失描述显式列出。

资源矩阵使用 `draw3-resource-v2-fixed`：20 个固定 profile × HW/WARP，共 40 个串行独立进程，覆盖 3.2 MB/小 InkData、4.8 MB Highlighter、48 B 常量（字段写/memcpy/volatile）、DISCARD/合法 NO_OVERWRITE、DEFAULT+UpdateSubresource、合成、2/3/6 Draw、1080p clear/copy 和小尺寸 staging readback。每个 case 冷轮 1 次、warmup 1 次、measure 4 次；原生总墙钟预算 8 秒，父进程单项上限 20 秒、矩阵上限 480 秒，挂住的驱动调用由独立 worker 外部截止。预算耗尽后剩余 case 记 skipped，不重跑或自动改变工作量。

只有本轮成功创建并持有的 probe worker 可以被超时终止；终止后最多等待 3 秒确认退出，未确认则停止后续矩阵。父进程并行持续排空 stdout/stderr，每流只保留前 1 MiB，之后继续读并记录丢弃字节；单个原始文件上限 4 MiB、调用最多 4096 行、矩阵磁盘预算 64 MiB。每个 case 使用独占新目录，并严格核验 session/case/PID/FILETIME/EXE/目录/backend/profile/工作量和真实退出码，单项 unsupported/failed/timeout/crash 不掩盖产品采集状态。产品退出后重新校验 EXE SHA 并在后测期间持有只读文件句柄，防更新替换 EXE 后收到未知参数。

矩阵使用独立离屏资源及内嵌 301/302 shader 合同，不复用产品 Host/文档/RTS，也不改变产品的资源创建、Map flags、设备选择或绘制算法。冷轮与 warmup 不从原始数据中丢弃。同为 48B 常量资源，`production_field_stores` 实际写 36B，memcpy/volatile 写 48B（包含 padding）；`case_definition.write_bytes` 保留准确字节数，跨写法不能声称完全等写入量。只有完整且身份/同 profile/大小/写入量/迭代/shader 身份匹配的 HW/WARP measure 数据才给 CPU 调用区间比值，GPU 未就绪不补零；WARP 更快只能提示 backend 敏感，不能直接证明驱动 bug。矩阵专有的一次 Flush/有界 drain 成本独立记录，不能当作生产路径行为或 shader 忙时。若不支持该路径，原始失败/阶段/adapter/flags 仍有诊断价值。

成功 Present 返回证明软件提交过程到达完成边界，不能证明原始输入坐标的像素已物理显示，也不是触笔到光子延迟。报告给出瓶颈候选及对应阶段/因素，WARP、ULW Map/拷贝、Modeler、大脏区等相关现象不能单独认定某个 CPU/GPU 型号为根因。`disabled_path_contract` 只记录正常模式的静态隔离合同，本包报告没有正常模式性能实测。

开发者为本次已构建的新 **Release EXE** 生成可交付包（不会运行 EXE，也不改常规发布 ZIP）。Release 更适合性能实测，Debug 与 Release 的优化配置不同（/Od 与 /Ox），可能影响耗时；面向兆芯等 x64 设备应交付 x64 包，ARM64 包仅用于 ARM64 设备：

```powershell
.\Build-Package.ps1 -ExePath ..\..\Build\x64\Release\Inkeys.exe
.\Build-Package.ps1 -ExePath ..\..\Build\ARM64\Release\Inkeys.exe
.\Test-Draw3-Performance.ps1
```

包生成器核对 PE 架构及精确 v2 能力标记，拒绝只有旧性能参数字面量的 EXE；并把 EXE、全部 5 个用户脚本/CMD/README 文件及其 SHA256、每 raw 文件 schema 和资源 profile manifest 装入单独的目录和 ZIP。这个静态检查不是实际运行能力证明；它避免对没有入口的旧 EXE 发未知参数而意外打开产品窗口。正式交付须使用本任务构建验证过的版本。源码目录未带 EXE/manifest，不能直接向任意旧 EXE 启动诊断。
