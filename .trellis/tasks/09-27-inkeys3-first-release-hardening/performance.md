# 性能基准与口径

## 2026-09-30 计量工程续接（不构成性能胜出）

- UI3 U04-R raw recorder已Debug/strict Headless/独立实码通过；成功时刻仍为callback-end代理。B1真实四API后的提交戳与七职责分段已取得四条确定红，正在实现；B2/B3/F整体设计仍NEEDS_REVISION，必须冻结有限目标signature、SVG实际ready proof和exact私有输入/runner后才能采三轮。
- Draw3 U1固定32MiB记录器、64pending、失败保留、seen/retained/drop/invalid分母、工具分群/schema2/null小样本P99及create-new已红→绿+独立GREEN；M16 Live/Laser数值反例亦CLI0。Host仍未传会话，正常Controller仍未产生权威栅格proof，当前这些是记录合同测试，不是Down到像素/成功呈现性能数据。
- 下一U2/U3合同见integration-and-release-check/research/draw3-content-and-host-contract.md：lastConsumedSequence不等于模型采纳，currentContentRevision不等于逐笔内容，失败后rasterState也不等于L2成功；必须用实际adoptedSequence/authoritative stamp。Laser正式landing若producer不足要明确excluded，而其frame/生命周期仍单独调查；不得拿其它工具外推。
- 当前仍不能声明UI3/Draw3相比H0、指定Canary或Inkeys2完整性能门槛通过。已有CPU子段数据/撤销的候选原样保留；后续Release真实模块采样与on/off语义/像素复验不与编译/扫描并行，Win7/真输入/光学/HC-H2身份仍单列。

## 四个基准

| 基准 | 版本/状态 | 可比性 |
| --- | --- | --- |
| H0 | chore/publish HEAD 8b156fca59f0337a6afc6d722941666fcf143080，tree 56758776f075f5b4ae659108a190111a29f2c59a，任务创建前空工作区 | 当前代码基准；运行数据未验证 |
| HC | 强候选：2026-08-11 GitHub Actions Canary run 31487748238，SHA 82f7b7c02080c253661514d31b55f3a827382e1d，ARM64 artifact ID 9100206152，archive SHA-256 b0f47b32886c1ad8b20b9aad8160c2fccb1408dc199afbdd6a081d831da432c2 | 来源可追溯；用户实际安装包及同机对照未验证，不能称唯一 HC |
| H2 | 最新正式 Inkeys2 Release 20260713a，tag 0d9751b96f063d9aa5f46f8d905a8e153e9c91dd，ARM64 ZIP SHA-256 584b08c6b8af12e8a2c0966c9d39a42cc30d62ea0265629434b525c079ec426e | 公开 Release 身份已核；实际运行/配置未验证 |
| HF | 完工时 HEAD 加全工作区内容指纹 | 未验证 |

2026-09-29 本地对照二进制补充：H2 正式 ARM64 ZIP 已下载且本机 SHA-256 与上述发布记录相同，内层 `Inkeys.exe` SHA-256 `2300b276aac3402e87b5c6a11ca39a81f2b06f7643f14f8ab85acd830f2635a5`；HC 候选 run31487748238 ARM64 artifact 由 `gh run download` 解包，内层 EXE SHA-256 `81a3dbb26a845308ea2aafe7869844b389968e31f3798aee2e0987f947a6d07e`，原 artifact ZIP 散列未本机复算。两者 PE 均 ARM64 GUI。隔离启动只证明两旧版本各自创建 FloatingWindow/Drawpad 等 HWND，脚本向自有旧 Drawpad 发 WM_CLOSE 未在22秒内自然退出，随后仅强制本任务 PID；没有相同效果/输入轨迹/成功 Present 的帧样本。当前 HF Debug 隔离 GUI 10秒 idle 可见 Bar、Select 双画布隐藏且 UI3 Bar WARP/ULW 成功呈现，物理命中被其它 PID 遮挡，定向自有 Bar 消息也未完成模式切换。以上均**不能**用于 HC/H2/HF 流畅度排序；用户所指 Canary 安装包身份仍待确认。

2026-09-29 真实 Host 隐藏窗口持久化资源快照：Release|ARM64 两种 presenter 轮次各自的 `Draw3HiddenPptCommands/<PID>-<QPC>` 唯一根分别保留 8 个 UInk 版本/约18.6 KiB 全部文件；`Draw3HiddenPptPersistence/<PID>-<QPC>` 分别保留 7 个 UInk 版本/约25.4 KiB 全部文件（本轮 PID10396）。这些是保存/冷载/重排场景结束后的**单次磁盘快照**，对应 F-044 为保护最后有效索引而不自动GC旧版本的容量取舍；既不是显存/句柄泄漏速度，也不能推导长时间每小时增长。三架构隐藏测试现在可运行真实输入→成功 Present 功能断言，仍缺把某笔 Down/Move/Up 与对应成功帧关联的生产观察 token；精确测量门见 `integration-and-release-check/research/draw3-hidden-end-to-end-benchmark-design.md`。没有该关联时拒绝报告伪造的 Down→可见像素 median/P95/P99。

## 优化前冻结的门槛

- 同设备、系统、驱动、供电、DPI/分辨率/刷新率、构建类型、renderer/presenter、主题/效果和输入轨迹才定量对照。Debug 与 Release、不同设备或不同效果不可互作胜出证据。
- 每场景至少三轮，先 warm-up；分冷启动、首次、稳态、长期。每轮保留原始样本、成功 Present 数和样本量；统计 median/P95/P99、长帧、活动帧间隔、实际动画时间、线程 CPU、可得 GPU、内存/显存/句柄、cache hit/miss/create/evict。样本过小的 P99 标不可可靠估计。
- median 超出轮间噪声且超过 5%，或 P95 超出轮间噪声且超过 10%，视为关键场景实质退化；落笔、Up 尾延迟和输入语义独立门禁。临界/噪声高时增加轮数，不挑最好一轮。
- UI3 场景：主栏展开/收起/拖动/吸附/快速反向、属性栏/Fine Dial、动态光影、SVG/path、设置竞争、idle、resize/DPI、设备重建。callback、attempt、success、idle、合法 Retry 分开。
- Draw3 场景：Down→首次成功 Present、Move 积压/延迟、Up→最终稳定、慢/快/折返/停动/多接触、已开放工具、擦除、历史/页面切换、长期文档；采样/消费/呈现率分开。软件计时不冒充光学延迟。
- 用户已在 2026-09-28 允许 GUI/脚本交互（禁用 computer-use 工具）；仍需记录实际窗口/成功 Present/输入环境。静态或 headless 只支持狭义结论。

## 环境与原始数据

本机注册表/PNP/API 显示 Win11 ARM64 25H2 build 26200.9457、Snapdragon X1E80100、Qualcomm Adreno X1-85 驱动 31.0.137.0；另有 OrayIddDriver 虚拟显示驱动 17.50.13.330。当前单屏 1440×960、96 DPI、60 Hz，Balanced、AC 供电。当前可能经 Oray 虚拟显示，不能外推实体屏幕体验。H0/HC/H2 实际 renderer/presenter、当前 Draw3 成功呈现链、主题/效果和可比整帧性能未验证；当前隔离 GUI 仅有下述 UI3 WARP 首帧观察。完整环境证据见 research/baseline-sources.md。原始数据存任务约定的忽略目录，文档记命令与摘要；尚未有前后可比整帧结果，不能报告性能改善。

### 2026-09-28 隔离 GUI 首次呈现观察（单样本，非性能对照）

在 `TestResults/release-hardening/gui-smoke-42e499f90f304c13a23075287cfac5ef` 中启动复制的 Debug|ARM64 EXE，隔离日志 `log/idt1790602081244.log` 的首个 UI3 诊断显示 Bar `ulwAttempt=1/commit=1`、`backend=WARP`、`callbackMs=37.459`、`drawMs=13.698`、`getDCMs=18.601`、`ULWMs=0.828`、rounded mask `hit/miss/create=0/1/1`。主栏实际 HWND 可见，`PrintWindow` 可取局部图像；这是一帧首次创建与成功 ULW 的观察，不能计算 median/P95/P99、完成时间或 CPU/GPU 稳态成本，也不能比较 HC/H2。`CopyFromScreen` 在命令 runner 原桌面报 invalid handle；输入桌面线程可读指针，但 runner 启动的窗口在另一桌面，脚本拒绝在命中其它进程 HWND 时继续点击。另一次显式输入桌面启动的 EXE 约14秒后自行受控退出，未记录有效输入/呈现样本，原因待查。真笔 Down→首次成功 Draw3 Present、长期动画/光影、缓存趋势及 Win7/HC/H2 同机仍未测。

## H0 UI3 算法层初始样本（已执行，非整帧）

2026-09-27 同一 Debug|ARM64 H0 可执行文件串行运行三次 InkeysHeadlessTests --no-window --benchmark，三轮均退出 0。每个 BENCH 在一次运行内先 warm-up 3 次，再测 11 次并输出该轮 median/P95/noise；这里的表值为三轮 median 的中位数。原始输出在忽略目录 TestResults/release-hardening/ui3-h0-benchmark-1/2/3.log，汇总为 ui3-h0-benchmark-summary.tsv。

| 生产算法项 | 迭代/轮 | 三轮 median 中位数 | 三轮 median 范围 | 最大轮内 noise |
| --- | ---: | ---: | ---: | ---: |
| same_target_noop | 20000 | 395.785 ns | 365.995–520.58 ns | 94.49% |
| inactive_value_scan_57 | 20000 | 9972.17 ns | 9476.07–10002.9 ns | 46.20% |
| one_active_value_scan_57 | 20000 | 11571.4 ns | 10630.3–12320.7 ns | 26.82% |
| value_custom_keyframe_advance | 20000 | 2066.28 ns | 2006.34–2185.95 ns | 45.16% |
| color_continued_advance | 20000 | 2818.94 ns | 2745.43–3054.51 ns | 57.76% |

这 19 项 microbenchmark 仅触及生产 UI3 动画算法，未经过完整 Bar、动态光影、SVG、GPU 或 ULW。程序未保存 11 次内部原始样本，轮内 P95 只有 11 个样本且近似最大值，不能据此给可靠 P99、整帧 P95 或性能胜出结论；噪声高的项需增加采样或改测量方法。

## H0 Draw3 生产输入协调段样本（已执行，非完整笔迹）

新增严格无窗口的显式 --draw3-contact-benchmark 入口，调用已编进 InkeysHeadlessTests 的生产 Draw3.ContactInput 模块；每次先发布 Down、按序进行 20000 次真实 PublishMove+TryReadSnapshot，逐 Move 断言位置和递增序号，最后发布/读取 Up 并回收。每轮 3 次 warm-up、11 个测量块，三轮串行且审计 Git 扫描已暂停；独立 review 修补后的原始 33 块数值在 TestResults/release-hardening/draw3-contact-h0-reviewed-1/2/3.log。原始 draw3-contact-h0-1/2/3.log 的 harness 未逐 Move 断言、Up 时间戳倒退，保留为被取代样本，不用于后续对比。

| 指标 | 三轮 median 中位数 | 三轮 median 范围 | 三轮最大 run mean 范围 | 轮内 noise |
| --- | ---: | ---: | ---: | ---: |
| PublishMove + 一致快照读取及逐 Move 语义断言，每 Move 平均 | 2860.34 ns | 2826.68–2888.05 ns | 2927.33–3156.22 ns | 7.07–14.94% |

此计时包含单接触点生产发布与消费读取；每个 run mean 是 20000 次 Move 的平均每次耗时，不是整个块耗时。不包括真实 RTS 解码、modeler/prediction、几何、GPU 上传、Present、history/persistence，也不能表示 Down→像素可见或光学延迟。33 个 run mean 样本不是逐输入延迟分布，P99 不报告。后续 Work 3 必须独立补全生产 Host 的其余阶段与资源趋势；本表不能替代。

## H0 Release|ARM64 算法与输入子链（后续可比口径）

完整 InkeysRepo.sln Release|ARM64 构建退出 0，Draw3 VS/PS/UpdateCS/EmitCS 四个 shader 编译输出重新生成。停止其他基准与 Git 扫描后，同一 Release 测试二进制分别串行跑三轮。原始 UI3 日志为 TestResults/release-hardening/ui3-h0-release-benchmark-1/2/3.log，19 项汇总为 ui3-h0-release-summary.tsv；Draw3 为 draw3-contact-h0-release-1/2/3.log。每轮内部仍是 3 次 warm-up + 11 次 measured；只报告 run mean 和其轮间变化。

| Release 子链 | 三轮 median 的中位数 | 三轮 median 范围 | 最大轮内 noise |
| --- | ---: | ---: | ---: |
| UI3 same_target_noop | 11.015 ns | 7.86–11.025 ns | 47.03% |
| UI3 inactive_value_scan_57 | 172.685 ns | 148.62–175.445 ns | 68.26% |
| UI3 one_active_value_scan_57 | 194.265 ns | 189.225–194.335 ns | 369.65% |
| UI3 value_custom_keyframe_advance | 117.36 ns | 116.305–119.835 ns | 39.06% |
| UI3 color_continued_advance | 207.42 ns | 206.475–209.975 ns | 58.60% |
| Draw3 ContactInput PublishMove+TryReadSnapshot+逐 Move 校验 | 88.825 ns | 88.68–116.175 ns | 32.54% |

Draw3 第一轮与后两轮差异明显，UI3 多项轮内噪声高，故这些数值只能冻结初始算法范围与观察噪声，不能用于宣称细微收益或整帧流畅度。后续 Work 2/3 若要比较这类项，须增加轮次或改善测量；真正 UI3 ULW、Draw3 成功 Present、GPU/资源趋势及 HC/H2 同机体验仍未验证。

## Work 2 当前真实离屏入口

`Inkeys.exe --bar-eraser-offscreen-test` 在参数分派后只初始化共享 UI WARP/D2D，并渲染生产 Bar/橡皮属性组件到离屏 bitmap；不建 HWND。首次正式等待进程退出的 ARM64 Debug/Release 基线均出现 0xC0000005，经临时阶段记录定位到未启动 Draw3 Host 的 Clear 命令被默认 running 的 Bridge 错误接受。F-018 最小 guard 后，ARM64 Release 完整 Solution 和该离屏场景退出 0、`failures=0`，覆盖 21 个主题/DPI 与窄窗口/动画画面；诊断打印已删除，原始日志留在忽略的 TestResults/release-hardening。该结果是正确性修复，不计作性能提升，也不代表主窗口 ULW、动态光影长帧或 HC/H2 对照。

UI3 专项以整数像素平移下 exact A8 整图遮罩为候选，预先冻结“先三轮生产离屏 D2D 像素/分片/耗时 baseline，后单条件优化，像素不等价或收益不越噪声则撤销”的口径。另核 idle 唤醒首帧时钟与失败退避领取 dt 的正确性，不能调快 duration 掩盖。研究入口：ui3-performance/research/current-cost-triage.md。尚无本次 UI3 整帧或 HC/H2 同机可比数据。

## Work 2 UI3 独立离屏调查结论

2026-09-28 在同一 Release|ARM64、WARP/D2D、512×256 BGRA 目标下，使用生产 `BarUIRendering::Shape` 与 `RenderPipeline::Scheduler` 诊断，分别对 Identity、整数 `(32,24)`、分数 `(32.5,24)` 平移做三轮，每轮 16 帧预热、11 块×64 帧测量。原始数据与像素在忽略的 `TestResults/release-hardening/ui3-exact-mask-baseline/`、`ui3-exact-mask-candidate/`；完整方法和 hash 见 ui3-performance/research/exact-mask-optimization.md。候选使整数平移每 64 帧遮罩提交 576→64，33 块合并中位数 0.0608078→0.0447094 ms，但 451/131072 像素最多改变 1/255；按冻结的逐像素质量门槛**撤销生产优化**，复验三个场景 BGRA hash 回到基线。因此 HF 不含此性能收益，不能把 26.5% 候选局部数字计入用户体验结果。

SVG/path 当前资源链与缓存 miss 条件已有独立只读生产审查，见 ui3-performance/research/svg-path-review.md。颜色逐帧改变时可能重新解析/栅格化 SVG，主按钮尺寸/n 动画可能重建局部 path，但没有 parse/raster/upload/draw 分段时间和真实发生频率，未增加缓存或宣称改善。F-019 Scene 缓存设备失效是正确性修补，不能当作速度收益。F-004 idle 首帧零时间修补预计消除静置后 50 ms 位置跳变，只有确定性时钟/无窗算法验证，尚无真实呈现轨迹；不能由此宣称主观流畅度达标。UI3 全窗口/GetDC/ULW、GPU/显存/句柄长期趋势及 HC/H2 同机比较仍需有 GUI 授权的真机采样。

## Work 3 Draw3 生产几何子段初始分布

`Draw3.PerformanceProbe.cpp` 通过显式无 HWND CLI 调用主产品编译的 `PlanLaserIncrementalRanges`；独立 reviewer 已核 production 模块、计时边界与四场景。2026-09-28 相同 Release|ARM64、Windows 11 ARM64 构建串行三轮，每轮每场景先 3 个 warm-up 块、后 11 个 measured 块。每块对预分配短/长增长轨迹调用 8192 或 12288 次，时间包括真实函数和调用方 stable cursor 更新，不含输入、modeler、几何顶点构建、GPU Map/Draw、Present、history。每帧五字段的非计时 `trajectory_checksum` 三轮完全一致；原始日志为忽略的 `TestResults/release-hardening/draw3-geometry-baseline-release-arm64-{1,2,3}.stdout.log`。下表 P95 是 **33 个块均摊每次调用耗时**的分位数，不是单帧或单次调用 P95；样本不足以报告可靠 P99。

| 场景 | 点数/块调用 | 三轮块 median（ns/调用） | 合并块 P95（ns/调用） | 三轮轨迹校验和 |
| --- | ---: | ---: | ---: | --- |
| short steady | 96 / 12288 | 40.397 / 30.518 / 30.827 | 40.690 | `7072970627664936059` |
| short changing duration | 96 / 12288 | 29.085 / 28.825 / 29.321 | 34.294 | `3923401809491347895` |
| long steady | 4096 / 8192 | 1208.838 / 1248.083 / 1255.273 | 1646.936 | `8787745198285828124` |
| long changing duration | 4096 / 8192 | 1257.568 / 1263.062 / 1229.333 | 1544.336 | `7227937043183381065` |

F-022 的从头扫描成本随笔长上升在这一子段已测得，但 4096 点增长轨迹的块均摊调用约 1.25 微秒，当前证据不足以证明它造成 8.33/16.67 ms 帧预算的主要退化。首轮 short steady 比后两轮偏高，亦说明轮间噪声。暂不加入带前缀版本/时间阈值的复杂缓存；若长笔真机诊断显示该段成为热点，再用同轨迹 hash 和生产视觉结果建立前后对照。F-020 的动态 SRV 能力修补是 Win7 兼容正确性，不作为性能收益；生产 renderer 的无 HWND WARP 回读只验证 Map/offset，真正 HW/WARP FL11 Win7、成功 Present、长文档资源趋势仍未测。Draw3 输入→成功 Present 的完整成本分布仍是 Work 3 未完成项。

## Work 3 ULW CPU dirty copy + alpha 检查初轮样本（已被取代）

独立 reviewer 发现下述初轮夹具在一个计时块中重复传入相同 source/DIB，只在块末读取最后一次返回值和 DIB。Release 优化器可能消去前面迭代的 alpha 扫描或合并 copy，故下表**不能作为可靠单次调用成本、瓶颈确认或 A/B 性能比较依据**。三轮原始日志保留为方法学失败证据；正在改为每块只计时一次生产 helper、块后消费完整结果与 DIB，再重新采 baseline 和候选。F-005 不因本表升级为已证实性能瓶颈。

`Draw3.TransparentPresentation.cpp::CopyAndInspectUlwDirtyRows` 从实际 `UlwPresenter::Present` 的 staging→top-down DIB 逐行 copy 和随后 dirty 像素 alpha/预乘扫描提取，仍由真实 Present 调用；显式无 HWND benchmark 用固定 RowPitch/DIB stride、七种全量/局部/透明/半透明/无效 alpha 内容调用同一函数，比较完整 DIB 与观察标志的稳定 hash。当前只测 CPU 子段，`CopySubresourceRegion/Map` 等待、`UpdateLayeredWindowIndirect` 和成功 Present 均不在样本内。2026-09-28 同一 Release|ARM64，三轮串行，每场景每轮预热 3 块+测 11 块；QPC=10MHz。原始文件：忽略的 `TestResults/release-hardening/draw3-ulw-copy-baseline-release-arm64-{1,2,3}.stdout.log`。下表 median/P95 都是**块内 8/128/512 次调用的均摊每次耗时**，不是单帧 P95；33 块不足以报可靠 P99。

| 场景 | 三轮块 median（ms/调用） | 33 块均摊 P95（ms/调用） | 完整 DIB+标志 hash |
| --- | ---: | ---: | --- |
| full mixed 1920×1080 | 3.0252 / 3.0661 / 3.1277 | 3.7180 | `ee91ae952ee74786` |
| full transparent 1920×1080 | 3.1281 / 3.1980 / 3.2509 | 3.6135 | `dc74737d38bdd282` |
| partial 256×256 | 0.1008 / 0.0980 / 0.0980 | 0.1205 | `e8b42779253ce538` |
| narrow 8×1080 | 0.0185 / 0.0185 / 0.0188 | 0.0327 | `c9b7c904ae711e67` |
| partial transparent | 0.0959 / 0.1033 / 0.0980 | 0.1309 | `d026720829703c35` |
| partial half alpha | 0.1019 / 0.0968 / 0.1007 | 0.1802 | `67c4b45ac01fff62` |
| partial invalid alpha | 0.0959 / 0.1080 / 0.1029 | 0.2122 | `9296b37ff5d81d30` |

初轮块均摊数字不能用于得出候选收益或是否值得保留的结论。新夹具还须保持七个 hash/标志及透明边界完全一致，三轮前后收益超出噪声才保留。Microsoft 的 [UPDATELAYEREDWINDOWINFO 结构说明](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-updatelayeredwindowinfo) 与 [旧函数页](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms633557%28v%3Dvs.85%29) 对 `prcDirty` 的系统更新范围口径不一致；本次 CPU 测量也不推断 Win7 系统合成节省。

## Work 3 ULW CPU 单次调用新基线

按独立审查更换夹具后，仍调用同一个生产 `CopyAndInspectUlwDirtyRows`，每场景每轮先 16 次 warm-up，再对 128 次**逐次**调用分别计时；每次在计时前重置 DIB，计时后检查结果并消费完整 DIB hash。Release|ARM64 串行三轮均 exit0，七场景的 DIB/三标志 hash 三轮完全一致。原始单次样本在忽略的 `TestResults/release-hardening/draw3-ulw-copy-rebaseline-release-arm64-{1,2,3}.stdout.log`。下表来自每场景 384 个单次 CPU 样本；P95 是该受限函数的分位数，P99 只落在最慢四个样本附近，受调度/缓存噪声影响大，不把它当稳定尾延迟或整帧指标。

| 场景 | 三轮单次 median（ms） | 合并单次 P95（ms） | 完整 DIB+标志 hash |
| --- | ---: | ---: | --- |
| full mixed 1920×1080 | 2.9001 / 2.8918 / 2.9517 | 3.9912 | `ee91ae952ee74786` |
| full transparent 1920×1080 | 4.3535 / 4.1864 / 4.2859 | 5.8826 | `dc74737d38bdd282` |
| partial 256×256 | 0.1647 / 0.1610 / 0.1555 | 0.2504 | `e8b42779253ce538` |
| narrow 8×1080 | 0.0368 / 0.0364 / 0.0348 | 0.0630 | `c9b7c904ae711e67` |
| partial transparent | 0.1626 / 0.1612 / 0.1599 | 0.2591 | `d026720829703c35` |
| partial half alpha | 0.1692 / 0.1639 / 0.1636 | 0.3202 | `67c4b45ac01fff62` |
| partial invalid alpha | 0.1659 / 0.1633 / 0.1598 | 0.2925 | `9296b37ff5d81d30` |

这个合成内存输入基线支持尝试融合 CPU copy/scan，但不覆盖 GPU staging `Map`、真实 ULW 系统提交或生产 full/dirty 频率；不能从 2.9/4.3 ms 推算主画布帧率。候选必须在同一单次调用夹具上保留上述全部 hash/标志、前后各三轮的 median/P95，并在收益小于轮间噪声时撤销。

### 同夹具融合候选复测与决定

只把 alpha/预乘检查移到每行 `memcpy` 之后、GPU staging `Unmap` 之前，并从刚复制的源行读取；DIB 内容和布尔公式不变。相同 Release|ARM64 单次调用夹具候选三轮均 exit0，七场景完整 DIB/三标志 hash 与新 baseline **逐字一致**。原始候选日志在忽略的 `TestResults/release-hardening/draw3-ulw-copy-singlecall-candidate-release-arm64-{1,2,3}.stdout.log`。以下百分比取三轮 median 中位数之比，正值是变慢；P95 为合并 384 个单次 CPU 样本，仅描述本夹具。

| 场景 | baseline median（ms） | candidate median（ms） | 变化 | P95 baseline→candidate（ms） |
| --- | ---: | ---: | ---: | ---: |
| full mixed | 2.9001 | 2.8697 | -1.05% | 3.9912→4.1347 |
| full transparent | 4.2859 | 4.2704 | -0.36% | 5.8826→6.1980 |
| partial 256×256 | 0.1610 | 0.1805 | +12.08% | 0.2504→0.2758 |
| narrow 8×1080 | 0.0364 | 0.0515 | +41.82% | 0.0630→0.1354 |
| partial transparent | 0.1612 | 0.1812 | +12.38% | 0.2591→0.2803 |
| partial half alpha | 0.1639 | 0.1812 | +10.59% | 0.3202→0.2517 |
| partial invalid alpha | 0.1633 | 0.1702 | +4.26% | 0.2925→0.2767 |

全量收益低于轮间噪声，局部 dirty 明确退化；因此**否决并撤销生产融合候选**，保留原两遍检查与离线基准。正确性测试可保留但不算性能提升。这个结果也不证明完整 Draw3 或 ULW 系统提交已达到 HC/H2 体验门槛；后续若另有具体瓶颈证据须重新冻结独立候选。

## Work 3 文档、Tile footprint 与 history CPU 基线

新增显式无 HWND `--draw3-history-benchmark` 链接主产品 `InkDocument`/`InkHistory` 模块，覆盖 10/100/1000 支 64 点笔迹、普通笔与每四笔一支橡皮、Undo/Redo、Undo 后新分支和独立第二页。源输入复制、页面准备、digest/资源采样均在分段计时外；production `AppendStroke`、`BuildStrokeTileFootprint`、`CanvasRuntimeHistory::AppendStroke` 等各自计时。2026-09-28 同一 Release|ARM64 串行三轮，六场景各 3 warm-up+11 measured 块，全部进程 exit0，文档/history digest 三轮恒定。原始块和 process private bytes/working set/handles 在忽略的 `TestResults/release-hardening/draw3-history-baseline-release-arm64-{1,2,3}.stdout.log`。下表是每次**构建整份文档**的块耗时，P95 仅有33块、受分配器与系统噪声影响，不是单笔 P95，也不含 GPU/Present/磁盘。

| 场景 | Append 三轮 median（ms） | footprint 三轮 median（ms） | history append 三轮 median（ms） | footprint 块 P95（ms） | 末态 retained/visible |
| --- | ---: | ---: | ---: | ---: | ---: |
| pen 10 | 0.0114 / 0.0086 / 0.0087 | 0.1836 / 0.1388 / 0.1397 | 0.0052 / 0.0042 / 0.0046 | 0.1920 | 11 / 9 |
| mixed eraser 10 | 0.0114 / 0.0087 / 0.0084 | 0.1793 / 0.1343 / 0.1337 | 0.0053 / 0.0039 / 0.0037 | 0.3581 | 11 / 9 |
| pen 100 | 0.0850 / 0.0792 / 0.0798 | 1.4449 / 1.3819 / 1.3952 | 0.0334 / 0.0310 / 0.0357 | 2.0590 | 101 / 76 |
| mixed eraser 100 | 0.0812 / 0.0786 / 0.0787 | 1.3020 / 1.3001 / 1.3071 | 0.0276 / 0.0265 / 0.0259 | 1.6249 | 101 / 76 |
| pen 1000 | 0.8491 / 0.8290 / 0.8094 | 14.7942 / 14.4319 / 14.1987 | 0.5196 / 0.5036 / 0.4779 | 18.3979 | 1001 / 751 |
| mixed eraser 1000 | 0.8200 / 0.7989 / 0.8130 | 14.4782 / 14.0837 / 14.6904 | 0.3988 / 0.3929 / 0.3931 | 15.3449 | 1001 / 751 |

1000 笔的 footprint 总成本约14ms，分摊到每笔约14µs；不能把“构建一千笔”当一帧卡顿。Undo→新分支后1000笔场景仍保留1001项、751项可见，确认当前 append-only sidecar 的资源趋势，但一轮分支不能外推长期泄漏速度。`private_before` 在 source 深拷贝前取得，process private delta 包含点数组、页面/history 和分配器行为，不是精确文档字节；`private_branch-private_append` 不含事先准备的分支点。原始资源字段可追踪，但本机进程的分配器复用、同一进程串行场景和无 GPU 限制了结论。独立 review 已核计时和 digest（含 bounds/undo 与 composition tile 坐标），tile-specific DecomposeRange 在无 barrier 时可能直接一块 CachedNode，不能冒充冷 GPU replay。PPT 全页快照/merge 的 O(P²) 与保存 worker 的慢盘背压仍只来自静态调查，需要独立生产基准/故障验证。

## F-025 Laser 事务的资源与性能边界

为防第二层栅格失败污染已提交 Laser 颜色，生产 renderer 仅在烘干时按需创建一份同视口 RGBA8 scratch，额外显存为 `4 × width × height` 字节：1920×1080 约 7.9 MiB、3840×2160 约 31.6 MiB，外加 RTV/SRV 对象；生命周期结束、resize 或设备释放时销毁。每次烘干增加一次已提交层到 scratch 的 GPU CopyResource；当前无 HWND WARP 64×64 红→绿测试验证像素事务，不构成真 GPU 帧成本、ULW/DComp Present 或 HC/H2 性能结论。真实长 Hold/Fade 显存趋势、烘干 P95、硬件 GPU copy 等待必须人工/真机测量；若实质退化需保留正确性并另设计等价低耗实现，不得丢样本或降低视觉效果。
