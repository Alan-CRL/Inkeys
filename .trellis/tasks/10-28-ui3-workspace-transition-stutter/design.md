# 技术设计：三态切换主栏动画卡顿

状态：2026-10-28用户审阅并批准实施。以下保留原始调查及候选选择门；实际结果另记verification/。

## 1. 调查结论与证据等级

本问题需要拆成两条时间线：**请求到主栏开始变化的延迟**，以及**动画活动期间相邻成功提交之间的停顿**。准备文档/背景慢不等于动画计算慢；某个 Bar 回调快也不等于下一次 Bar 能准时运行。

| 问题/机制 | 源码事实与影响 | 当前结论 |
| --- | --- | --- |
| 共享渲染线程被白板背景占用 | Bar、PageControl、Whiteboard Freeze 在同一 Scheduler 串行运行；进入白板时 Freeze 整屏绘制/GetDC/提交，背景窗口 SetBounds/Hide 还可能同步等待 owner | 高优先的转场特有阻塞候选；退出白板主要是 Hide，桌面↔PPT 不能统一归因到白板全屏绘制 |
| 居中根随宽度动画逐帧移动，扩大 dirty | 主栏根从当前宽度派生；映射变化要求 full replacement/full viewport damage | 明确的转场成本放大链，且是防残影/错误映射的现有正确性保护；不能直接删除 |
| Back 容量预留依赖已回弹的当前值 | `Bar.RenderLoop.cpp:7743-7805` 对 `max(val,startV,tar,middleV)` 再乘 1.06；尺寸越过历史峰值便扩容。`Bar.Rendering.cpp:306-372` 尺寸变化重建 context/target 并清设备缓存 | 可用确定性测试证明的预留缺陷候选；只在首次/新容量边界可能反复重建，历史居中 CSV 组内尺寸无变化，**不是所有切换的已证根因** |
| 背景 ACK 到协调提交可能等轮询 | StateMonitoring 的 Draw3 revision 等待有 250 ms 周期；背景完成没有同一路专用通知 | 候选主要解释开始晚；不得把 250 ms 当作每次固定延迟或动画中掉帧 |
| 提交时重复状态投影 | SetWhiteboardActive 后 RefreshToolState 会再次 StateUpdate | 可去重的候选；SetTar 本身有幂等保护，不能据此声称每帧重启动画 |
| 白板按钮颜色动画触发 SVG CPU 重建 | Whiteboard 选中状态改变→contentColor 连续过渡→icon.color1 每帧变化→CacheBitmap 字符串换色、解析、栅格化及 CreateBitmap | 白板进出直接可达的重复工作链；可复用已有 Ui3SvgStageTimer 测各阶段，尚不能断言其占主要耗时 |
| 公共绘制基础成本 | 历史展开主栏多目标基准 27 组中 23 组未满足旧预算，GetDC/绘制有压力；已有 mask 分片优化候选因无可靠收益撤回 | 需要当前转场分段采样；历史结果不作为当前 HEAD 性能证明 |

详细调用链、精确行号与反证见 `research/workspace-transition-path.md`、`research/render-animation-hotpath.md`、`research/evidence-and-validation.md`。三份研究是本表的证据主体，不从旧任务复制过时“已修”结论。

## 2. 保持的架构与合同

仍使用现有三态协调器、唯一 Draw3 Host、UI3 Bar、公共 BarButton、BarUIRendering、PageControl 及共享 RenderPipeline。新增平行状态机、渲染线程、另一套按钮绘制、最终动画帧缓存或切到硬件后端均不在方案中。

1. **业务身份与视觉提交分开**：pending/desired 不冒充 committed。白板首背景 ACK、窗口 owner 就绪、Draw3 首帧/输入 fence、PPT session/revision 继续按既有合同验证；不能为缩短等待提前放行命中。
2. **公共按钮单一来源**：StateUpdate 按稳定快照派生有效 hide/size/Selected/Disable；保留列表身份、隐藏成员锚点、Freeze 退场顺序、相同状态幂等。主栏和分页使用公共目标、推进、绘制封装，固定面板不引入通用节点树。
3. **动画时钟与批次**：同一 batch 使用同一 frameTime；idle 后首帧 dt=0；同帧一次推进；从当前值反向，CanJoin 的 0.5 中点边界不变。连续字体修正、父子面板目标和 current/target 分工不变。
4. **共享资源与呈现**：默认 WARP、唯一串行 render owner、每目标独立 context/target；不在 UI 锁内跨 owner 等待。完整 GetDC/ULW/ReleaseDC/EndDraw 成功后才消费 damage 和发布命中；失败重试保留旧成功快照。容量变化完整重画，不能向新 bitmap 只画旧局部 dirty。
5. **光效与质量**：保留默认边缘光、鼠标光、AA、premultiplied alpha 和源/目标映射。不通过禁光效、隐藏元素、缩短动画、降低帧率或逐帧 SetDirect 掩盖卡顿。
6. **诊断不开销外溢**：复用 RawCaptureReport 和现有七细阶段。新增细项仅在详细采样门开启时写固定数值；默认路径不新增计时、格式化、文件 I/O 或阻塞日志。

## 3. 修复顺序与选择门

S0 是前置门；S1–S4 是可独立验证的候选编号，不要求全部实施。完成采样后按实际主导成本排序，白板SVG或owner等待若占主导可优先处理；不因容量候选较容易修改就将它当作体验修复结论。

### S0：补齐同一请求的时间线，冻结当前基线

复用 centering 测试六向/快速反向的生产轨迹，但性能运行必须走真实共享 Scheduler，同时运行该状态实际可见的 PageControl/Freeze。直接递增 16 ms 的正确性 Step 保留原用途。用测试侧有界记录关联 request/revision、背景完成、committed、首次目标提交、Bar attempt/commit、末态/idle；不扩充产品配置或另造遥测系统。

冷场景使用 fresh fixture owner/资源，单独覆盖“从未超过过的最大宽度”；热场景先稳定再反复切换。记录实际 target 尺寸变化和 cache reset，避免预热历史峰值把容量缺陷藏掉。每个方向分别报告调度等待、Bar 各阶段、同批其他客户端和成功间隔。SVG 优先使用已有 Ui3SvgStageTimer 的分段计量，不重复增加同类埋点；只有样本证据支持时才升级对应修复。

### S1：修复稳定动画包络预留，保持视觉轨迹

S0 或 fresh 红测证实连续扩容后，优先作一个小而可独立验收的正确性/资源修改：复用 `BarUiGetCurveExtrema` 及既有动画范围能力，以当前动画段的 start/target、关键帧段及实际曲线极值求稳定包络。包络必须包含当前值，但不能把已回弹的 current 再作为新端点重复乘 Back 系数。若真实宽度轨迹没有触发额外容量增长，则降级并跳过该性能补丁，先处理样本中实际主导者。

`back=1.1` 时 EaseOutBack 从 0→1 的解析峰值约 1.044714；现有算法的动态预留可能从 1.06 上升到 1.107396。这个算式证明预留会随当前采样变化，不证明一定实际扩容。反向、负值、非零起点、中间关键帧、动画关闭、倍率变化和中途 retarget 必须使用真实动画类验证；不得仅用零起点公式替代所有情况。

保留容量只增不减、拖动/释放固定表面、设备 epoch 恢复、完整替换的原合同。目标是在同一未重定向段内最多进行一次必要增长；稳定历史容量足够时不重建。不默认顺带改写所有 renderer 缓存生命周期。若 S0 显示用户热切换完全没有扩容，该修改不能单独宣称解决体验问题，继续 S2。

### S2：只处理采样证实的白板共享线程阻塞

先区分 owner 等待和真正 WARP 全屏成本。若 owner 等待占主导，复用 Window Service 的异步/就绪回执机制，在进入可绘制阶段前完成几何/显隐准备；render 回调消费已确认的 readiness，未就绪时按现有 Retry/请求语义退出，避免在共享 render callback 内同步跨 owner 长等。仍由原 owner 操作 HWND，不从业务线程直接 SetWindowPos，也不每帧反复投递相同请求。

若成本主要在全屏绘制/读回，先消除同一 revision 的重复失效/失败重绘，只在屏幕尺寸、主题、页背景或真实 revision 改变时重画。任何预备资源只使用已知当前目标和已有 epoch，不创建额外 device/render worker。不能靠把 Freeze 简单排到后面解决已经发生的整批超时；若必须把一次不可分全屏工作拆分或跨帧准备，需保留首背景成功 ACK 的事务边界，并在实施前补充具体接缝设计，禁止直接提前提交白板。

若采样说明 Freeze 成本低，则不改该模块。共享线程对 Draw3 device 的隔离保持原样。

### S3：缩短无必要的协调/投影延迟，按证据实施

若 request→committed 的尾部确实卡在背景完成后的轮询，可为既有 coordinator 增加轻量 revision-completion 通知并合并到现有等待条件；保留超时兜底、失败预算、取消、过期 token 拒绝和源补偿。不忙等，不减少安全预算，不因任意 wake 当作 ACK。

若重复 StateUpdate 有可测锁竞争，仅合并同一提交内的同步投影，保留必要的业务状态刷新、Recall 保护、pending 关闭动作和锁外唤醒。相同状态不重定向，不用取消动画换取短耗时。

### S4：剩余热点的选择门

对白板按钮 SVG，先比较颜色插值期间的 parse/raster/upload 占比；若证实主要耗时，再拆开源内容/结构和颜色失效边界，复用已有可证明等价的资源。仍由公共 renderer 持有资源，保留 color1 占位色替换、多色 SVG、alpha、尺寸阈值、内容关键帧和 epoch 失效；不能只给白板按钮另写绘制器、固定颜色、禁用颜色动画或缓存所有最终颜色帧。若没有小范围且像素等价的方案，先补充具体设计，不直接实施通用 SVG 重构。

full replacement 先作为正确性基线保留。只有在 S0–S3 后仍被证明为最大成本，且可证明旧新 source/origin/映射一致时，才设计局部替换；不能把 `bottomDockRootLayoutChanged` 标志直接删掉。光影优化也须有切换样本、真实像素比较和收益对照；旧 mask-slices 候选不直接恢复。此阶段若需要明显扩大改动面，先更新设计再实施。

## 4. 验证与回退

- 每个补丁独立红测→最小实现→正确性回归→同机三轮前后性能对照；没有稳定收益或越过合同则撤回该补丁，不叠加猜测优化。
- S1 回退限容量包络；S2 限 Freeze readiness/提交调度；S3 限 completion 通知/单次投影。一个候选失败不回滚已验证的无关阶段，更不触碰当前分支的 workspace failure recovery。
- 三态六向、快速反向、idle 首帧、Freeze 隐藏/恢复、工具/属性保持、PPT 页码身份、失败重试、设备恢复、不同倍率、贴底与浮动、白板首次进入和再次进入均按 `implement.md` 验证。
- 既有热态目标：batch P95≤16.67 ms、P99≤33.4 ms；同时评估成功提交间隔及 >50 ms 清单。不能以平均 FPS、空 Continue、代理回调时间或 batch 达标单独宣称流畅。
- 本轮不执行编译/性能实验或 GUI。实施阶段按用户规则采用完整 `InkeysRepo.sln Debug|ARM64`、ARM64 原生 MSBuild、同 invocation 的 PATH workaround、至少 5 分钟超时；只跑已确认无窗口的入口。真实视觉与 Office/DWM 验收单列，需用户另行授权 GUI。

## 5. 实施选型与实际覆盖（2026-10-28）

S0已使用真实共享Scheduler/Bar/PageControl六向离屏负载证实两个可独立修复的机制：

- 粗粒度sleep_until在无Draw3绘制态timer period时，使足够快的batch仍常以31–35ms启动。共享Scheduler改用现有FramePacing内部高分辨率/兼容timer，通过小型导出TryWaitForRemainingFrameTime无自旋接缝调用；失败保持原deadline的sleep_until回退，不改变idle、请求合并、动画dt或全局timeBeginPeriod。
- 主栏无条件按隐藏属性/More预留约2006×946起步位图，GetDC开销占主导之一；P→D真实活动还出现2006→2012→2022→2026的三次宽度增长。最终保留原横向对称安全预约及207.5等既有余量，仅用真实ValueRange整段极值替代current采样后重复放大的启发式；不再为约8%的横向面积收益改写居中/显示布局模型。纵向去掉所有隐藏属性/More的无条件整屏预留，复用current、实际曲线预测viewport及displayCenterY完整物理段求required size，再与历史容量max。显示目标缩放只乘独立required size，固定held/released surface及完整source包含保护保留。

纯target/source映射变化在观察结束后只扩大完整清绘/ULW，不晚置tracker全量校准而丢失未变视觉的成功bounds；已知root居中重排则在BeginFrame后、任何选择性Observe前标记全量观察。实际Draw/Clear仍覆盖整个候选viewport，ULW仍完整替换，成功事务与失败重试不缩减。

S2白板owner等待、S3协调通知/重复投影、S4公共SVG/光影重写均未纳入当前补丁：当前无窗口负载没有覆盖真实Freeze/Window/Draw3协调全链，不能据静态怀疑直接修改交接；当前已测的timer/GetDC成本先处理。没有改默认光效、视觉曲线、字号、按钮或WARP后端。

验证矩阵的实际调整：先pilot两轮100%，随后每方向5次×三倍率×三轮（270转场/版），而不是默认30次；保留30次完整模式及1–30次有界环境参数。保留原all-cycles结果，并按首轮资源冷/后续热循环分别报告，不删坏样本。Initialize已预热Desktop，故metadata明确freshCapacityCovered=false；场景由WorkspaceSceneScope直接置Committed，nativeCoordinatorCovered=false/Freeze=false。全部最终A/B在同一正常用户环境、同默认动画/光效下串行运行并记录独立EXE哈希，沙箱pilot不作为最终对照。

几何固定时间fixture另外显式采集实际ULW源viewport BGRA，正常用户基线324帧（与沙箱基线全部相同）供容量候选逐像素比较；它是图像正确性证据，不冒充性能。新增真实timer与dirty tracker事务边界测试，最终完整ARM64 Solution/Headless及生产无窗口入口结果见verification/。

横向紧缩的两版候选已被真实数据否定并撤回：44FF2B…版D→P发生15次纵向追扩/P→D多次横向追扩，DEE0/DD93…版虽然补锚点range仍出现19–22次横向追扩。原因包括SetDirect驱动与retained viewport的耦合，继续扩展预测模型违反minimal diff取舍。保留失败raw/EXE哈希，不把它们的进程exit0或初版像素通过当作最终验收。

最终原生fixture新增可失败容量断言：除每run首个D→P在第二帧建立pending placement真实目标允许增长一次外，未重定向场景段不许继续改变target尺寸。A02209…最终pilot通过，首段1次、其它全部0；后续所有矩阵结果按此断言执行。像素与Headless在最终源固定后重新验证，旧版结果只保留研究用途。

最终保守方案的三倍率矩阵曾在150%首个P→D捕获一次纵向290→292的像素边界增长，原生进程确实exit1（不是将summary当通过）。修正仅在独立plannedHeight真正突破历史capacity时预约既有viewportPadding的两侧余量；不每帧对历史叠加，且加法有LONG上界保护。保留该轮失败数据，最终容量稳定性断言不放宽，修正后重新执行三倍率/像素/Headless。


## 最终实施收敛（以此段为准）

更紧的横向预测候选在真实Scheduler中出现持续追扩，已撤回，不扩大居中根布局模型。最终保留既有横向对称安全预约/207.5等余量，仅把1.06 Back余量作用于完整实际width曲线极值；主要容量收益来自取消隐藏属性/More纵向无条件预约，使用现有可见/在途包络和真实displayCenterY端点独立求所需reach后再max历史。requestedWidth突破历史时仍触发visible bounds扫描。150%290→292红测后，仅在独立plannedHeight真正突破历史时保留viewportPadding*2两侧像素裕量，非每帧历史叠加，保留overflow与完整source包含检查。固定held/release surface不变。

late资源或source映射改变仍完整bitmap绘制/full replacement提交，但不在Observe结束后误设全量观察状态以清除未观察稳定边界；已知root布局位移在BeginFrame之后Observe之前ForceFullDamage。新增tracker回归检查纯容量扩大/真实root移动后单独隐藏稳定子项的旧像素清理。最终真实fixture以稳定目标段首帧后不增长为原生失败门禁，仅fresh fixture第一D→P实际pending placement二帧重定向有一次有据许可，全部倍率三轮通过。

新增Scheduler修复复用既有FramePacing timer，通过小型TryWaitForRemainingFrameTime导出返回成功与否，零自旋、失败沿原deadline sleep_until回退，无新增owner/线程或全局timer period。未改SVG、Freeze/Window ACK、协调轮询、默认曲线/光效/字体。实际预算与覆盖缺口见verification/final-verification.md：同Debug A/B显著改善，最终Release绝对预算仍非全部达标，不将剩余阶段主导结论扩展成SVG细分或全产品60FPS证明。

## S4独立计量后的处置

为避免开启Ui3SvgProbe改变完整观察/缓存决策，新增独立固定数值TLS，仅复用现有四阶段timer钩子；默认不读新时钟、不启用proof。数值scope与原probe共存共享一对原计时读钟，真实owner callback退出并Stop/join后才写sidecar。签名与合同已同步ui3-render-diagnostics.md，红绿/透明性与采样结果见verification/svg-numeric-timing.md。

100%/200%各一轮、每向2cycle短样本中，301/272个成功提交帧的四阶段SVG CPU累计占相同成功Bar callback时间4.59%/3.03%（全部496/462个callback口径为3.81%/2.62%）；成功帧SVG总P95约0.841/0.817ms，GetDC P95约6.384/14.719ms。保留100%单个约17ms raster离群，不据平均占比否认偶发慢帧；Draw阶段仅CPU提交，仍不能排除SVG的异步WARP成本。该pilot不替代前一批完整矩阵，也不据不同运行/样本P95相减推算占比。

当前未选择公共SVG缓存重构、重复GetBounds微改或旧mask-slices方案：没有证据支持其足以解决剩余预算。继续保留完整绘制/提交和现有画质；若要进一步改变backing/source布局以缩小互操作面积，须单独设计固定坐标映射、布局/命中转换、负原点/多DPI、反向/失败/设备epoch及完整source包含证明，再作独立A/B。现有source/root/容量相互依赖，不能再次仅缩容量让其逐帧追扩。切换呈现后端、增线程和减光效不纳入本任务已验证修复。
