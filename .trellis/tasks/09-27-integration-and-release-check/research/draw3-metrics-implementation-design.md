# E04 Draw3：生产采样实施设计

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。实施者：`draw3_metrics_implementation`。状态：**仅设计，等待主会话 GREEN_DESIGN；未改源码、未构建、未运行 CLI/基准**。

## 1. 差距、责任与约束

当前产品编译 `Draw3.RuntimeMetrics`，Controller 借用可选 Session，但 Host 尚未构造它。PM-01/02/03/04/05/06/09 是计量缺口，不能据此宣布新产品故障或优化收益：现有 Present 计数不区分成功；BeginFrame/失败提交会丢掉已回收 contact 的 landing；只有仍按住的帧进入分布；成功 QPC 晚读；混合 200 条 demo 门槛不能证明工具群体 P99；样本/dedup 在采样中扩容或静默截断。

补充静态已核 **PM-10**：Session `ToolName` 沿旧 demo 的整数0/1/2/3解释为Pen/Highlighter/Eraser/Laser；产品 `DrawingTool` 实为0=Pen、1=HardPen、2=Highlighter、3=Eraser、4=Laser、5..8形状，Controller直接传该值。启用旧Session会把工具误分群。U1在 `.cpp` 导入实际 `window_control`，用符号enum映射九种工具，未知值仍单列；该模块没有反向metrics依赖，不新增循环，不复制旧整数表。速度/固定橡皮须另带实际width-mode或scenario population，不能混后声称分别覆盖。默认metrics关闭，所以此项也是报告问题而非用户画布故障。

第一单元只修复 Session 的有界记录/报告合同，第二单元才接 Controller 的实际纯值身份与成功回执。输入采样、mailbox、modeler 参数、prediction、几何、GPU、画质、pacing、presenter 和保存合同保持现状。指标关闭时 Controller 仍用 null 指针短路，不创建会话或分配样本。

本 agent 先独占：`Draw3.RuntimeMetrics.cppm/.cpp`；第二单元获批后独占 `Draw3.DrawingController.cppm/.cpp` 的指标相关位置。已有 E03 初始化拒收 helper、probes、暂停 hook 不动。Host/HiddenWindowTest 必须等 root 明确交接；Main/Window/RTS/持久化/工程/spec/共享账本由 root 或其指定者修改。没有递归 agent、commit/push、branch/worktree、finish。

## 2. 单元 U1：Session correctness 与离线报告

### 2.1 拟冻结的 API

复用 `RuntimeMetricsSession`，不另起遥测框架。新增/调整 API 均只由唯一绘制 owner 调用；离线 getter/report 在 owner 已停后使用。

| API | 数值职责 |
| --- | --- |
| `BeginFrame()` | 累计实际 loop 次数、清本帧候选标记；**不清跨失败帧的纯值 pending**。保留旧调用的可编译入口。 |
| `SetCanvasIdentity(RuntimeMetricsCanvasIdentity)` | 锁存本帧真正 document/page/generation；身份变化将旧 pending 标 `Superseded`，不把旧页落笔记在新页成功帧。 |
| `RegisterContact(ContactHandle, device, tool, downQpc, admissionRevision)` | 一次登记实际已准入的 Controller contact，分母覆盖尚无可见几何的 Down；返回会话内匿名 contact ordinal，失败/容量满有明确结果。 |
| `StageLanding(..., RuntimeMetricsLandingProof)` | 旧 record+generation 仅在当前 run 内判等，立即转为已登记 ordinal；暂存 `Live` 或 `Stored` 证明，不在回收后解引用 record。旧窄签名可暂留作 U1 red fixture 与 U2 过渡，但无 canvas/retention proof 时不得跨失败帧假定可见。 |
| `CommitStagedLandings(success, immediateReturnQpc, frameProof)` | 仅在真实成功、时间有效、身份匹配且该候选内容确实进入当前成功画布时形成一次 landing；失败保留有 proof 的 completed 候选。|
| `InvalidateContact(handle, reason)` / `InvalidatePending(reason)` | Cancel/页槽切换/Clear/停机/不可恢复资源失效明确结束未呈现候选；失败不得静默减小分母。 |
| `RecordPresent(durationWallMs, success, returnQpc)` | 分开 attempt/success/failure；每次实际调用都有计数，不能把 `presentCount` 旧值继续叫成功帧。 |
| `RecordRenderFrame(RuntimeMetricsFrameSample)` | 保留 render attempt 的 wall、Present、reason、前后 contact/terminal 数和序列；Up/Cancel、页重放、粒子/恢复帧都可入样，不限定仍按住。 |
| `RecordActiveFrame(...)` / `EndActiveFrameSequence()` | U1 保留旧活动序列入口，U2 仅对真实连续 physical-writing 子集调用；idle 和已结束段不连接成长帧。 |
| `Snapshot()` / `WriteJson(...)` | 只读数值计数供生产 fixture 断言；离线分组/排序/格式化，热路径无文件或字符串 I/O。 |

`RuntimeMetricsCanvasIdentity` 按值保存 workspace GUID、page GUID、明确的 metric scene generation；JSON 只输出匿名 canvas ordinal 与 generation，不输出 GUID/地址/文稿路径。`RuntimeMetricsLandingProof` 分开 contact identity、候选输入 sequence/QPC、canvas identity、proof kind、Stored RenderItem index+generation、raster token、history revision、pipeline/output generation。导出 contact ordinal；record 只用于 Session 内键，**不会在未来帧解引用**。

U1 可先实现此合同而 Controller 仍沿旧接口；这些未启用路径的成功不升级为 Host 已可采样。U2 消除生产旧接口调用后删除不必要过渡重载。

### 2.2 失败后保留与明确的保守界限

- `BeginFrame` 只清“本帧重新 stage/confirm”的标记。失败 Present 不清纯值 completed pending。不能仅删 clear 语句就让其它页下一帧给旧 contact 回执。
- Session 自身不能证明 GPU/文档内容；调用者提供 candidate 的纯值 proof，Controller 的 shared helper 校验同 canvas、同 item generation、仍 visible、最终内容未被破坏以及完成合成/重放。只有通过的候选参与本帧 Commit。
- 普通 Stored 笔画的证据是实际 `CommitRuntimeStoredStrokeCpu` 返回的 `RenderItemId` 与 before/after raster token，及 `history.Find(id)`。完成 Up 前记录它，回收后 Session 持纯值。下帧只有同 canvas 的有效 item 和清晰权威重放/合成才能 restage；其它新笔的成功帧不能仅凭同 page index 给它回执。
- 一项最小保守合同可以先要求同 history revision+raster token；若 intervening append 使证明不再精确，即标 `SupersededProof` 而不是造成功 latency。若 U2 使用 `history.Find(id)` 可证 item 仍 visible，则允许 append 后重放，必须直接测正/反例。最终选择写入 U2 交付，不能隐藏漏样。
- `Live` 候选仅由当前 active runtime 对应的真实已栅格 sequence restage；旧 Move 不能被最新 diagnostic 快照倒填。Cancel/Clear/页切换不 restage。
- Laser 不属于 Stored history。其 layerId/当前 trail generation 能证明同代 t6/未烘干层才允许恢复；设备重建已清 Laser 时旧 pending 标 `TransientLost`，不借普通 Stored “final stable” 分布补齐。该工具的 Up/hold/fade 分类单列。
- 强退没有封口/报告时 run 是 incomplete，不能补写尾样本。正常 Session 封口将仍 pending 标 `SessionEndedUnpresented`，保留数量。

### 2.3 容量、成本与报告

- 保留默认 `maximumSamples=32768`。按固定预算（拟上限 32 MiB）约束 frames/contacts/presents 的共同容量；最大允许样本由实际 `sizeof` 推导并记录请求/实际容量，不盲目预分配旧上限 1,048,576 的多种大数组。
- 构造时一次分配全部固定表/数组；采样写入仅索引赋值、不扩容。contact dedup 使用预分配 open-address 表，负载率不超过 0.5，不以累计 32k contacts 的线性全查找处理每帧 stage。pending 有固定上限（拟64，**不是宣称物理设备只支持64**）；溢出按 contact 一次记 `PendingOverflow`。不降低产品 contact 容量。
- 每类 `seen/retained/dropped/invalid/pending/unpresented` 及 attempt/success/failure 保留，即使 raw 数组满。contact 注册一次保证容量/drop 不按每帧重复放大。饱和会使 coverage/truncated=false 的 gate 不通过，不能取头部小样本代表整段。
- 原始 frame/landing 数值保留；按 device、tool、reason 分群给 count、median、P95。P99 仅群体有效样本至少1000时给正式值；不足时为 null 并标 `insufficientPopulation`，必要的 exploratory 数字另名。空群体分位数 null。
- 非有限 wall、无效/逆向 QPC、负间隔记 invalid；不钳成0ms。QPC 差先转浮点后减，避免有符号差溢出；单位只称 `software Down-to-PresentReturn` 与 wall。CPU/GPU 另列 unavailable，不把总 wall 当线程 CPU。
- 旧 `MeetsStrictThresholds()` 保留并明确命名为 legacy demo 检查：200 混合 landing、旧8.33/9.5ms值不作首发 gate；不完整、丢样、失败/未呈现不能使它误报 complete。JSON schema version 2 分别输出 legacyThresholdMet 和 coverage，不能用单个 strictPass 含糊代表全工具首发性能。
- `WriteJson` 只用于 caller 已校验/预创建的隔离目录，create-new 不覆盖任意既有文件；正常构造/输出失败是诊断失败，不改变输入或渲染策略。locale 固定 classic，JSON 不输出 NaN/Inf。

## 3. 单元 U2：Controller 因果接入与实际 producer

| 纯值 | 真正 producer / 更新边界 |
| --- | --- |
| run identity | 未来 Host 每次成功采样 Start 的新 Session；Session 不能跨 Reset 复用旧 contact 键。 |
| contact identity | 已出队 Down 的 `ContactHandle(record,generation)`；Controller 注册后保存匿名 ordinal。reconnect 同新 handle 为新物理 contact，继续旧 Stroke 不伪称旧 Down。 |
| admission revision | 同 generation 的真实 Down snapshot + `ContactInputCoordinator::AdmissionRevision` 合同；不从当前全局值给旧 Down 倒填。 |
| source Down / consume | `down.qpc`、`lastConsumedSequence`、`lastInputSnapshot`；只保证 **已接受/真正消费的 snapshot**，当前 mailbox 未消费 Move 单列，不承诺所有 RTS 原 packet。 |
| modeled / visible version | actual `lastModelSnapshot` / model update 数与 `modelInputThisFrame`；候选在真实成功 raster/ApplyOperatorLayers 后锁存 sequence。不能用当前 snapshot 更新就说它已进几何。 |
| canvas | owner workspace/page GUID 与当前 `document_->WorkspaceGuid()` / `PageGuid()`；scene generation 在替换/Clear/页槽切换推进，currentContentRevision 不当逐包身份。 |
| Stored final | 真 `committed->renderItem/beforeState/afterState`、`pageRuntime.history.Revision`；不是 strokeCompleted observer 或 input recycled。 |
| raster/device generation | 真 `rasterPipelineGeneration` 与当前 ready replay；若不可得 device epoch，schema明确 absent，不能硬称pipeline generation等于device唯一epoch。 |
| output generation | `presentation_.RequestedOutputRevision()` 与 `LastPresentObservation().outputRevision/target`；失败 observation 不能复用上次成功值作当前成功。 |
| frame / Present serial | Controller 每次真实 loop/render attempt、Session 每次 Present 调用；成功 serial 只在调用返回true时递增。 |

`DrawingController::PresentFrame` 在 `presentation_.Present` 返回后**立即**锁存 QPC（仅采样开启），先于 observer/ready/其它帧末工作；Commit 使用该 QPC，不再在帧末读取。normal 的 lastPresentDuration/pacing 不重定义。

总 wall 记录每个 render attempt；原因位最少 physical-before/after、terminal、command/page、resize/recovery、hover、Laser fade/particle、noPresent、rasterFailed、presentFailed/success。新小 shared helper 只负责候选/帧数字装配和有效性校验，Run 与实际 CPU fixture 共用，不复制一套正确算法。当前 Run 的早 continue/idle 另统计 loop/idle/wake；完整 render 记录并不冒充所有 wake。既有等待/TargetFPS 不变。

消费/model/prediction、geometry+rasterSubmit、composite、Present、wait 可用阶段时钟包围已有批次；交错阶段诚实合并，不逐点 QPC，也不移动业务调用来凑分段。具体合并段与未观察字段在 U2 diff 前反馈 root；无法取得的 upload-only/GPU/cache hit/miss/显存时标 unavailable。

## 4. 单元 U3：Host / fixture 接入（等待文件交接）

显式 `HostStartOptions` 采样 opt-in，默认false；Host 在绘制 owner 生命周期内持有 Session，Controller只借用；Start失败或 Stop 后 join 才给 runner导出。传 actual driverType/featureLevel/presenter 数值元数据，input diagnostics启用不覆盖已有 hidden fixture 开关。只保留当前run，不把同址同generation串到重启。

复用 `Draw3.HiddenWindowTest`、Window Service 和真实 StartProduct。测试只定向自有HWND、不全局SendInput、不碰真实配置。root解析CLI/项目登记与正式输出路径，本agent不越界抢改。既有10ms WaitUntil只用于等待结束，不作为延迟终点。first render/冷路径与 warmup稳态分开。

## 5. 红→绿、独立检查与构建槽

1. root确认设计/API/容量后返回 GREEN_DESIGN。U1先加真实 Session probe（拟导出 `RunDraw3RuntimeMetricsProductionProbe`，root登记产品CLI或Headless）：测试实际 Session/WriteJson，不克隆百分位或计数实现。旧逻辑的 failed Present、completed pending、截断/逆时/小样本至少有行为red；新增 schema 本身不算根因red。
2. root独占BUILD_SLOT完整 `InkeysRepo.sln Debug|ARM64`（原生host、规范Path、至少5min）；保存红日志和私有PID/路径。U1最小修补后同slot绿、Headless，交独立checker。该绿只说明 Session 合同，不冒充 Controller/Host性能。
3. U2 fixture调用 Run共用实际候选装配/验证helper：Down+Up→回收→首次Present=false→同画布完整成功只有一次landing；换页/Clear/Cancel/Undo item不可见/同槽新generation/输出代次不匹配为反例；同时两笔/超过pendingcap/终态无按住/失败raster无Present等分母正确。actual Controller PresentReturn捕获用未来隐藏Host验证，不以CPU假返回替代实际API边界。
4. U3明确交接文件后，只启用显式模式并验证真实生产 Host/RTS/modeler/renderer/presenter，发送/发布/消费/呈现数各自记录；metricoff和on同轨迹功能/完成笔数/像素结果一致。root串行原生Debug与相关Release/三架构，无并行输出争用。
5. 最后采样前停止构建/扫描/其它worker基准；Release 每backend/固定工具轨迹至少3轮。soft/hard/highlighter/固定/速度橡皮/Laser/已开放形状分群，包含点/慢/快/折返/停动/Up/Cancel/换工具/多接触；保留冷/首次/16笔预热/稳态与原始数据、每轮count/median/P95。小群体P99不可靠；不宣称新增观测本身是优化。

Win7 SP1 **仅 KB2670838**、Hardware FL11.0/无FL11.0→WARP、FLIP_SEQUENTIAL、DComp/ULW且两个DWM禁用全部保持。隐藏窗口软件成功返回不是光学可见时间、真笔、Win7或HC/H2同机体验通过；HC身份仍未唯一定位。

## 6. 已读上下文与当前证据

已读 active implement.jsonl 的路径与PRD/design/implement，完整性能采样研究、父baseline/performance/handoff、native-desktop索引/cpp/build/errors/diagnostics/input/Draw3/render相关合同、native质量规范和CPU/GPU/reuse/cross-layer边界；`get_context.py --mode packages` 确认单仓库。hook完整输出已从保存路径读取，过长分段续读。

实际源码检查点：Session默认32768、staged64、presentedKeys初reserve512并线性查重；Controller在 PresentFrame 无条件RecordPresent，frame末晚QPC、runtime回收前Stage、active-after分支才RecordActiveFrame；Host未传Session。当前git还含root/E01/E03任务源码修改，未覆盖或回退。所有上述结论是静态核实，未记动态PASS；验收证据将写独立implementation交付，父账本由root更新。
