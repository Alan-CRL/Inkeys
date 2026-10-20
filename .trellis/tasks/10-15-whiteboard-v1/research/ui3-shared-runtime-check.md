# UI3 共享按钮、输入与多目标渲染：最终独立检查

日期：2026-10-16。活动任务：`.trellis/tasks/10-15-whiteboard-v1`。

本报告复核最后一批公共脏区剔除、分页内容增量更新、呈现身份确认与真实基准夹具；此前全范围实现检查由主会话保留。本代理只读产品源码，未并行构建、运行测试或启动 GUI，以下修复由主会话落实。构建与测试结论来自主会话实际退出码及本代理读取的最终日志；不是静态推断。

当前结论：本批代码未发现尚未修复的局部正确性阻塞，最终编译与无窗口功能测试通过；性能预算未通过，不能标记总体验收通过。父任务保持 `in_progress`、`implementation_complete=false`，不结束、归档、commit 或 push。

## Findings (fixed)

- File: `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp`
  - Issue: 新增有限 SVG 证明旁路测试使用 `make_unique<Ui3SvgProbe>()`，实际类型只提供带 `ownerSerial` 的构造函数，build9 出现 C2512。
  - Fix: 复用已有探针构造合同，传入独立测试身份 `0xB380`；没有修改产品接口来迁就测试。最终完整 solution 编译通过。

- File: `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`
  - Issue: 新增动画连续性测试要求下一次 20ms 后的整数像素矩形必须变化。既有 0.4s EaseInOutCubic 在前 40ms 的位移可小于一个像素，floor/ceil 后矩形保持不变，不能据此认定动画停止。
  - Fix: 保留内容更新前后全部 17 个按钮及 presentation 包络的严格相等检查；随后以五次真实 20ms 共享 Advance 验证矩形继续变化且动画仍活跃。没有使用一次被 50ms clamp 的 100ms Tick，没有改生产曲线或降低像素要求。最终 PageControlUI3 为零失败。

- File: `Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp`
  - Issue: 浮动启动夹具在创建 coordinator 前设置 `fold=false`，仍会被生产首次布局的浮动启动策略改为 `true`。隐藏 Geometry 的按压动画可以触发无可见 dirty 的全脏兜底，因此“600 次成功提交”仍可能只覆盖折叠主按钮，不能证明展开主栏负载。
  - Fix: 首批让生产协调器完成启动放置，第二批通过原状态入口展开；采样逐帧检查 fold、主栏表面、实际列表中 Draw/Geometry 的正尺寸、字号、图标及文字 alpha。要求 `main_visible_frames=600`，同时保留主栏和各可见分页各 600 次成功提交、公共剔除计数大于零的严格条件。旧折叠数据分别归档，不作为有效展开基线或改善比例。

- File: `Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp`
  - Issue: 基准调用真实 UI 配置归一化，可能在源码目录生成配置，不能让验证夹具影响用户或应用配置。
  - Fix: 在首轮真实 UI Load 前将配置根目录暂时指向绝对的忽略目录 `TestResults/ui3-shared-runtime/runtime-config`，所有 worker 和管线停止后恢复原值。原夹具生成文件的身份经创建时间核实后由主会话保留到忽略证据目录；本代理未修改用户文件。

## Findings (not fixed)

1. **性能 gate 未通过，保留为当前任务未完成项。** 最终采用实现的完整测量来自 build12 的 `baseline-expanded/`：27 组中 23 组至少一个预算超限。目标仍为 batch P95≤16.67ms、P99≤33.4ms，没有提高阈值或筛除慢帧。三轮范围如下，单位均为毫秒。

| 场景 | 倍率 | batch P95 | batch P99 |
| --- | --- | --- | --- |
| 展开主栏 | 100% | 13.19–13.48 | 17.86–20.85 |
| 主栏及四个 PPT 分页 | 100% | 17.77–18.57 | 22.94–26.50 |
| 主栏及两个白板分页、菜单和主题 | 100% | 18.89–21.37 | 24.36–29.58 |
| 展开主栏 | 150% | 15.98–26.91 | 21.41–40.19 |
| 主栏及四个 PPT 分页 | 150% | 23.08–30.32 | 29.77–70.82 |
| 主栏及两个白板分页、菜单和主题 | 150% | 22.85–25.84 | 27.60–35.40 |
| 展开主栏 | 200% | 18.38–20.51 | 20.16–29.49 |
| 主栏及四个 PPT 分页 | 200% | 23.24–25.94 | 28.90–60.89 |
| 主栏及两个白板分页、菜单和主题 | 200% | 23.48–25.17 | 25.43–30.14 |

主栏成功提交间隔 P95 为 33.89–39.39ms；100% 主栏单独运行也约为 37.04–37.68ms。因此部分 batch 达到预算不等于 60fps 或主观流畅度验收通过。完整样本包含 45 个超过 50ms 的 batch，主会话逐段复核发现其中 43 个的最大排他阶段是 GetDC；另有 69 个超过 50ms 的主栏成功提交间隔。GetDC 会 flush 之前的 D2D 工作，当前证据不能将这些耗时进一步确定为某一缓存、绘制命令、系统调度或 API 内部缺陷。此项需要继续定位完整事务与提交节奏，不能当作已修复。

2. **未保留没有收益证据的候选优化。** 片段级 A8 柔光剔除在 build13 的九组真实 BGRA 对照中正确，但 21 组部分基准没有证明耗时收益，且计算、Draw、GetDC 同时出现整段波动。主会话撤回该两行候选及专门断言，保留原公共 Shape/Word/整按钮剔除；没有把波动归因为候选，也没有把部分基准冒充完整验收。候选源码、CSV、EXE SHA 和恢复记录留在 `candidate-mask-slices-partial/`。最终 renderer 源码 SHA 与恢复记录一致，build14 manifest 明确绑定 build12 完整生产渲染参考；仅在 Scheduler.Stop 后输出的 `mask_slices` CSV 字段不改变活动负载。

3. **全资源热态零分配尚无完整证明。** 固定几何、相同 epoch 的真实热态事务验证 context/bitmap/圆角遮罩复用及创建计数不增加。完整动画负载有 829 次父圆角遮罩创建，属于包含几何变化的活动期计数，既不能说成全部热态零创建，也没有证据说明每帧重建。未执行覆盖所有字体、SVG、CPU/GPU 对象的完整分配器跟踪，保留该验证边界。

4. **原生 GUI 验收未执行。** 未启动普通主窗口、Computer Use、真实 PPT/WPS 或任务栏窗口。原生 ULW/DWM、WndProc/触摸驱动端到端延迟、任务栏恢复、跨显示器/DPI、PPT 真实转场及动画手感仍待人工验收。无窗口标准按钮反馈和生产 Pump 回归不能替代这些结果。

没有以本次静态检查为由修改公共模块边界或引入额外缓存。针对跨轮变慢的专项所有权检查未发现 offscreen callback/shared Fixture 的自循环：scene 未持有指向自身 callback 的 shared_ptr，offscreen 未安装自捕获 hooks，lightingSubscribers 为非拥有观察列表且析构退订；Scheduler 停止并销毁回调时输入 worker 仍存活，Fixture deleter 可以注销并排空回调。全局 owner 的字体 cache、固定 map 和按钮列表使用替换语义，相关渲染 cache 有容量上限。该静态结论不能替代真实进程内存或驱动内部释放跟踪。

## Verification

- Lint: `git diff --check` 通过；仅有父 PRD 既存 LF→CRLF 提示。没有为本次检查新增格式化或另建 lint 工具链。
- TypeCheck: 通过。主会话使用 Visual Studio 动态定位的 ARM64 原生 MSBuild，在同一 PowerShell invocation 规范化 PATH，构建 `InkeysRepo.sln / Debug | ARM64`，build14 退出 0。命令保持非增量链接，未修改 SDK/工具链或工程配置来绕过环境错误。证据：`build-integration-14.log`、`final-build-manifest.json`。
- Tests: 功能通过，性能失败。最终 build14 的 `--ui3-input-worker-test`、`--bar-eraser-offscreen-test`、`InkeysHeadlessTests --no-window` 均实际退出 0。输入日志为 production mailbox/dispatcher/continuation/deadline/cancel PASS；PageControlUI3、EraserVisual 零失败；橡皮 216 布局、Recall 和 animation correctness 全部通过。证据为 `input/offscreen/headless-integration-14-*` 日志。

最终保留的公共剔除检查覆盖 3 倍率×3 DPI，共九组真实完整重绘与保留像素局部重绘逐 BGRA 相等，每组明确发生两个剔除。静态核对确认 dirty clip 在推入时转为设备坐标；当前对象包络按实际变换、DPI/unit mode 以及描边、blur 和抗锯齿余量保守判断。按钮先更新继承坐标再决定是否跳过绘制，未知 custom 内容不做整按钮猜测剔除，有限 SVG 证明作用域保留原观察顺序。Word 使用受 layout rectangle 限制的 DrawText CLIP。无活动 clip、无效/未知包络或不同 context 都保守走原绘制。

分页同布局内容更新只触及受影响的页码、标签、主题及反馈，不重开几何目标；SetOpacity 对已稳定同目标幂等，但显式零时长仍可结束未完成的同目标动画。相同文字的新业务身份也必须经过真实、可失败重试的提交，不能只发布新输入身份。像素、窗口位置和可见性成功后才消费 damage 和发布已呈现命中；资源/最终 sink/窗口交接失败保留旧命中及待提交状态。相关实际像素、连续性和失败回归在最终离屏日志中通过。

完整生产 Scheduler 基准使用实际 WARP/D2D、GetDC/ReleaseDC/EndDraw，只替换最终 HWND 提交端。默认光效开启，120 帧预热、600 活动帧、三轮、100%/150%/200% 倍率；主栏单独、PPT 四可见分页、白板两可见分页分组。有效数据合计 16200 次主栏提交、48600 次全部提交、16200 个实际展开可见主栏采样、公共剔除计数 185162；零掉样，复算 `dataIssues=[]`，三个指定 Idle 观测均零额外回调。该数据有效性通过不改变 23/27 性能预算失败的结论。

规范与任务记录以 `prd.md`/`design.md` 最新“统一 UI3”章节、`rendering-and-ui.md`、`whiteboard.md`、`ui3-render-diagnostics.md` 和 `validation.md` 为准，历史共享 Scene/旧轮性能通过记录不能覆盖本轮剩余项。本报告不标记父任务完成，不建立 commit，不 push，不归档。
