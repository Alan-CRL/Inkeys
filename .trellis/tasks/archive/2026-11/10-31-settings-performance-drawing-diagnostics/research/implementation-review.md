# 实施代码复核

日期：2026-10-31；2026-11-01 复核接续修复。角色：trellis-check；本轮按根代理要求只读复核源码，不与集中构建并发修改源码，不执行构建、GUI、提交或任务状态操作。

## 范围与依据

- 已读最新 `prd.md`、`design.md`、`implement.md`、`check.jsonl` 及其适用的共享调度、配置/国际化、输入、尾修正规范和研究合同。
- 覆盖 Draw3 五个 runtime 文件、Host 偏好/诊断、TransparentPresentation 与 RTS 摘要、Config、RenderPipeline、IdtMain 启动和边界自检、Setting 路由/条目和三语言文案、既有 headless 测试 diff。
- 编译与运行验证由根代理集中完成，本记录的“通过”仅指静态源码复核，不替代构建/测试退出码或真机触控/视觉验证。

## 发现与修复状态

2026-11-01 最终只读复核结论：下述两项 P2 均已在生产调用路径确认消除，已补共享生产 helper 的确定性交错回归。最后一轮设置测试调整仍保留真实布局、顶点、滚动和命中断言，不是隐藏所有失败条件。当前没有遗留质量阻塞项；根代理集中执行的完整 Solution 与五组测试均报告退出码 0，详情见末节。

### P2（原问题已静态修复）：通用单指门禁会抢先拒绝本应接入旧笔的断触 contact

证据位置（复核时行号）：

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:10722` 起，在 `initializeStroke` 开头读取当前通用偏好。
- `:10741` 的 `IgnoreAdditionalTouch` 在存在物理活动 Touch 且当前多指关闭时，立即 `DiscardUntilTerminal` 并返回。
- 该门禁发生在触摸手势登记和后面的 `awaitingReconnect` 候选筛选/`AdoptReconnectedContact` 之前，因此候选不能先证明自己是旧笔续接。
- `prd.md` R3/A13 要求设置变化不能影响已经在进行、等待断触续接或尚未收尾的旧笔，同一笔续接沿旧值。

具体触发：A/B 两指均在多指开启时已被准入；A 因短暂 Up 进入 awaitingReconnect，B 仍物理活动；设置改成关闭多指；A 在合法身份/时间/运动窗口内重新 Down。本次 Down 本应续接 A 的旧笔，但新门禁看到 B 活跃，直接丢弃 A，旧 A 最终按候选超时收尾。

建议：保持“新独立笔”的单指门禁先于手势登记；真正接续的候选必须先经过既有严格身份/运动条件，再沿旧笔接受，不能因为有任意 awaitingReconnect 就一概绕过门禁，也不能重新建模或改旧笔预测/修正设置。具体实现应由拥有 runtime 的代理完成并加生产回归。

测试覆盖缺口：新增 `RunGeneralDrawingOptionsProductionTest` 在约 `:6626` 只调用 `ClearPenTerminalState(oldStroke)` 模拟接续，未经过真实“门禁 → 候选匹配”顺序，因而不能发现上述场景。需要增加 A/B 接触、关多指、A 真实接续、B 正常 Up/Cancel、下一独立额外指仍拒收的回归。

此问题已即时发给根代理。本复核没有修改源码，以免与集中编译冲突。

2026-11-01 修复复核：

- 生产 `initializeStroke` 的通用新笔 gate 已移至真实候选匹配、modeler.Update 成功和 AdoptReconnectedContact 返回之后，当前约 `DrawingController.cpp:11382`。Laser 不参加续接，激光专用门禁继续在手势登记之前。
- 窄提取 `EvaluateRuntimeReconnectCandidate`（`:3954`）保留原身份比较、按候选宽度覆盖、motion 解析及 EvaluateInterruptedStrokeReconnect 参数；没有放宽时间、运动或身份条件。未匹配结果仍通过 IsBetterInterruptedStrokeReconnectMatch 的原过滤。
- `RunSingleTouchReconnectAdmissionProductionProbe`（约 `:4954`）已接入真实 control-fence probe链：A/B route、A Up候选、关闭多指、C合法匹配、真实模型继续 Update 后 Adopt；另含工具身份、空间范围、过期和模型 Update 失败负例。它验证新笔拒收与成功接续的先后关系，覆盖了原 ClearPenTerminalState 辅助测试遗漏。
- 源码次序已消除原误拒收问题；最终运行结果仍以根代理集中构建/自检为准。

### P2（已静态修复）：拒收交出 route 后再读取其身份，存在槽复用竞态

初次复核时 `RejectIndependentTouch`（当时 `DrawingController.cpp:3590`—`:3595`）先通过 IgnoreAdditionalTouch 调用 DiscardUntilTerminal，之后才 `gestures.OnTouchUp(CanvasTouchKey(handle))`。

DiscardUntilTerminal 会交出 consumer 所持 route：Producing 变 Quarantined 后真实 producer 的 Up/Cancel 可以直接回收；ClosingDiscarded 或已终态 ConsumerOwned 分支也可完成回收。`CanvasTouchKey`（`:2389`）读取的是 record 当前 TabletContextId/ContactId，不核旧 generation。交出后，producer 可能已为下一次 Down 复用这个槽；此时得到的 key 属于新接触，旧手势身份没有被退休，可能残留 FSM 状态。

依据：`Draw3.ContactInput.cpp:212` 新 generation 初始化时改写 tablet/contact ID；`:571`—`:625` 的 quarantined/closing producer 回收合同；`Draw3.CanvasNavigation.cpp:240` 的 OnTouchUp 按给定 key 精确查找，key不对即不清旧项。

建议仅做窄修复：在 Discard 之前保存本次已登记的原始 key（或将其作为参数传入），之后用保存值 OnTouchUp。禁止交出 route 后再解引用其身份。新 probe 当前顺序为 helper 返回后才 PublishUp，没有覆盖这段竞态；应至少验证 helper 用既存 key 退休，保留原 Up/Cancel 与旧槽不再占用断言。本问题已发给根代理，本代理未修改源码。

2026-11-01 最终修复复核：

- `RejectIndependentTouch` 当前 `DrawingController.cpp:3601` 在 IgnoreAdditionalTouch/Discard 之前捕获 `registeredKey`，`:3604` 只用这个保存值退休手势。交出 route 后不再读取 record 身份；调用点后续 `MetricKey(handle)` 也只复制 pointer/generation（`:2532`），不解引用旧槽。
- `RunIndependentTouchDiscardIdentityProductionProbe`（`:4965`）利用生产 helper 的默认空操作 `afterDiscard` 模板边界，分别在 Up 和 Cancel 后立刻发布同槽新 Down，确定性验证 record 相同、generation 不同、身份变更，且只退休旧 key、保留另一活动指并最终零槽/零手势残留。
- probe 的 `:5001` 正确消费并确认终态 ControlWake，直到取得真实非空新 Down；没有把 coordinator 的 null marker 当成新 contact。
- 真实合法接续仍在生产 `:11438` 成功返回；独立新笔拒收发生于 `:11449`，保留上一项 P2 的修复。`EvaluateRuntimeReconnectCandidate` 前补有 QpcDeltaSeconds 声明，定义/声明签名一致，未改变原数值逻辑。
- 以上是对实际生产代码和确定性 probe 的静态确认；最终 probe 退出码以根代理构建/测试记录为准。

## 静态复核通过的实现

### 每笔模型与抬笔修正

- 新独立笔的 `ResetNominalStroke` 后，`LatchStrokeDrawingOptions` 把 predictionMode 与 penTailCorrectionEnabled 写入 ActiveStroke，并用同一份模式构造该笔模型参数。
- 已有成功接续分支在新笔初始化之前返回，保留旧 ActiveStroke/modeler；`ClearPenTerminalState` 未抹除新增保存值。上节准入次序问题之外，旧笔不会因全局期望值改变而重置 predictor 或尾修正。
- 四处生产预测判断（断触候选、Shape、Laser、普通轨迹）均读对应 `ActiveStroke::predictionMode`；Disabled 模型不再由这些分支误调用 Predict。
- 橡皮在 latch 时继续强制 Disabled，保留原有行为；全局预测设置没有给橡皮开启预测。
- FinalizeStoredStroke 只按本笔值跳过 FitCompletedPenGeometry；正常 taper、公切线、真实 Up 接纳和 Stored 提交保持。ActiveStroke 池 Reset 恢复默认，生产随后重新赋本次偏好。
- 原模型/尾拟合算法与 Shader、预算、形状和粒子视觉均未改变；帧率由现有完整 profile 创建入口接入，非法 profile 回到 120。

### 多指、激光与发布

- Laser 拒收仍在手势登记之前；通用拒收在真实接续失败后对称退休登记项，并调用真实 `DiscardUntilTerminal`。未恢复 producing route 直接 Recycle 的历史泄漏模式，也未关闭 RTS 的多点采集；原身份竞态已通过提前保存 key 消除。
- 全局 gate 仅数物理仍活跃的 Touch，排除 ended/awaitingReconnect；Pen/Mouse 不参与计数。
- Host `SetGeneralDrawingOptions` 与 Controller 同名 setter 均通过原子 exchange 比较值，仅变化时发布 control wake，符合幂等要求。
- Host 保留激光原始子偏好，owner 计算总开关与激光多指的合取；全局隐藏不覆盖磁盘子偏好。Particle setter 只在有效激光选项变化时调用，未造成每帧重复 wake。
- 新 Controller 创建后清空 applied 缓存并在首帧前 PumpBridgeState，启动前/重建后的偏好能到达运行时；粒子保持旧 Inactive 生效边界。

### 配置、共享节拍和设置

- `Other.Config.cpp` 对两个帧率和预测模式先检查 JSON type 为 int/uint，再检查 isInt 宽度和各自白名单；bool、real、string、负值、超宽整数都不会窄化成合法档位。bool 字段沿既有严格 `isBool` codec。
- 七个新字段 defaults 为 60/120、true/true/2、false/false；迁移三项保持原配置键。IdtMain 在 config.ReadAll 后、UI/Host 启动前发布界面和通用/激光偏好，画布档位取 configOnce 的会话启动值。
- Scheduler 仅有实例内原子 30/60 偏好，统一 deadline 从上一批时间重算；未添加逐客户端等待、额外线程或 idle Continue。非法帧率恢复 60，setter 不制造 idle 请求。
- UI 使用显式 `predictionModes{2,1,0}` 映射精准/快速/关闭，不把 UI 索引强转内部枚举；通用分类顺序、默认值、正式效果说明和新笔提示符合需求。
- PageId 追加 16，未复用退休空洞；导航置于绘制后，middle bounds 与数量同步为 7。三项性能开关只在新性能页保留，实验室没有重复入口。
- BETA 出现在两个激光说明中；激光多指显隐使用当前全局偏好，粒子独立显示。配置保存仍走既有 QueueConfigWrite FIFO。

### 画布诊断

- 设备/adapter 与 GetDesc1 仅在图形 owner 的初始化、恢复或 Resize 边界读取；Setting 仅调用 Host::CanvasInformation 复制缓存，不访问 live Draw3 COM 对象。
- 显示取实际 driverType、featureLevel、适配器和交换链描述；Hardware 请求后实际 WARP 会标回退。DComp 主表面和辅助 selection-only ULW 分开说明。
- waitable 标记在实际 waitable swapchain + SetMaximumFrameLatency 成功后设置，ReleaseAttempt 清零；尺寸/格式/缓冲/SwapEffect/AlphaMode/采样/flags 来自 GetDesc1。
- RTS 摘要最多两次实际 attempt，不依赖控制台或调试宏；Host 输出真实 Extended/BasicXY/Win32Mouse 枚举及尝试阶段/结果。
- Host 新初始化代次会清空旧图形与输入文本，停止后明确标“本代最后记录”；恢复/失败更新图形状态，不把固定模板当实际结果。
- 元数据格式化和诊断分配异常被限制在可选诊断边界，不改变输入后端选择或绘图结果。

## 测试与证据的实际限度

- 新配置自检覆盖 defaults、四档画布/三档预测 roundtrip、保留激光子偏好、错误类型和超宽整数。
- Scheduler 新测试用实际实例和 Bar/PPT/Settings 假客户端，覆盖共享 batch clock、30→60→30、非法值回 60 与 idle 不增回调；2026-11-01 已补齐原先缺失的活动 60→30。
- 尾修正 probe 已加 false 与未拟合基线逐点比较，以及保留正常 taper/公切线的对照；default true 的原拟合回归仍保留。
- General probe 使用真实 modeler 的 Reset/Down/Move/Predict/Up，验证三预测配置的基础生命周期；修复后另加实际候选 helper、modeler continuation 与 route/FSM handoff 的组合回归，替代了原仅辅助层清终态的接续证据。
- 对称退休另外有 afterDiscard 的确定性 ABA 回归，覆盖 producer Up/Cancel、同槽新 generation 和 unrelated gesture 保留；不需要用概率竞争或真实 HWND 才能触发该旧错误。
- Host 信息测试只把代表性字符串经真实发布回调合并，能验证代次清空/停止标记，不能证明 DXGI 元数据采样、真实 Hardware→WARP / DComp→ULW / RTS→Mouse 设备路径已动态运行。产品采集来源由本轮静态链确认；真实硬件/GUI 未授权，不能宣称已通过。
- 本代理只执行 `git diff --check --ignore-submodules=all`，结果通过；未运行测试和构建，以根代理最终记录为准。
- 所有修改源文件 BOM 状态相对 HEAD 保持；工作树为 CRLF。仓库 `core.autocrlf=true`、text=auto，index LF / worktree CRLF 是既有 checkout 机制，不能把它误报为全文件换行改写。实际 Git diff 未见全文件格式化。

## 后续复核

1. 两项源码复核 P2 均已闭合，集中 control-fence 自检也已报告成功，不再为此次检查扩大源码范围。
2. 最终验证由根代理集中执行，本代理未重跑测试或构建。
3. 新增 `.trellis/spec/native-desktop/settings-performance-and-drawing.md` 已复核：记录真实新笔与接续边界、禁预测不改正常Up、尾修正只跳过Fit、strict配置、共享帧率和owner诊断采集，且索引已登记。不需要改动历史阶段2求解器算法。

## 最终设置测试差异复核

- `setting_design_tests.cpp:193` 的中段内容高度由 236 改为 276，对应 7 个 36 DIP 根项及 6 个 4 DIP 行距；仍断言 top/middle/footer 总高度、最小窗口真实溢出，没有改产品布局来迎合旧测试。
- `:3474` 的 collapsed/expanded scrollbar 断言由旧“600 DIP必然容纳六项”的假设，改成根据真实首末行范围、padding 与 InnerRect 高度计算 overflow。期望值不读取 ScrollbarY 自身，因此不是恒真比较；520 DIP 最小高度仍无条件要求 ScrollbarY 与 maximumScroll>0。
- `:2768` 的 thumb DrawData 检查仅排除 ImGui 标为 Hidden/SkipItems、轨道不足1像素或宿主裁剪外的帧。稳定和可见过渡帧仍需实际匹配主题 grab 顶点并满足 alpha>0/轨道宽度上限；相邻的命中范围避开 scrollbar、正文可用宽度和真实滚轮/首末项可达性断言没有被这个 guard 关闭。
- 新性能入口实际点击后必须切换到 PageId::Performance；旧 Plugins 的索引由2移3，Version 通过 back()继续核命中/可见性，避免把“第六项”误当末项。图标测试仍检查包含新字形的完整集合。
- 因而最后修改属于七项导航后的测试前提修正，保留独立、可失败的生产行为断言；未发现用条件绕过全部验证或删除相关断言。

## 最终范围、格式与验证结果

- 最终静态范围为预期的设置/配置/共享调度/Draw3接线与诊断、国际化、既有测试和相关spec。没有工程文件、依赖、Shader、签名配置或无关业务重构 diff。
- 对最终28个已修改跟踪文件做BOM/换行检查：BOM无变化，非文档工作树文件全部CRLF；`git diff --check --ignore-submodules=all` 通过。新任务研究文档与spec属于本任务记录。
- 根代理提供的最终退出结果（本代理未重复执行）：`InkeysRepo.sln Debug|ARM64` 完整构建 exit 0；`--config-boundary-test`、`--draw3-pen-taper-test`、`--draw3-parked-desktop-exit-test`、`--draw3-control-fence-test`、`InkeysHeadlessTests.exe --no-window` 五组全部 exit 0。
- LINK 的 IncrBuildImage 内部崩溃通过只移除生成的 `Inkeys.ilk` 后以相同 Solution/配置重建恢复；未更改工程、依赖或源码绕过工具链错误。原沙箱 MSBuild IPC问题与此链接生成物问题均归构建环境，不归为产品功能错误。
- 最后 headless 的25项旧overflow/thumb前提失败经上节测试修正清零，Scheduler30↔60及其余既有断言仍通过。未执行GUI、真实触控/粒子视觉或真机驱动回退，不能把这些无窗口成功扩大为硬件验收。
