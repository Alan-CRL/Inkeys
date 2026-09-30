# 执行账本

状态只用：已验证通过、已确认失败、已修复待验证、未验证、需要人工、假设被排除、基线已有问题、不适用（附理由）。Work 阶段与 Trellis 生命周期分开。

## 2026-09-29 17:58 总结

- Work 0–4 的代码实施、H0 历史 611/611 可枚举 SHA 逐 diff、22 个高风险专项、自动构建/回归与崩溃/重启修补已完成到当前源码；Work 5 自动部分已完成，正式宏/Win7/真设备/真实用户链仍是门禁。
- F-057/F-058/F-059/F-060 和最终 HiddenWindowTest 交接改动均经过红→绿或重复自然退出证据及独立只读 review；所有构建/测试日志在忽略的 `TestResults/release-hardening/`，失败日志不删除。
- 当前不把 `ArmResult::Failed` 的双重监督建立失败写成有界强退；不把 Win32 Touch area 旧 0/1/0 通过新观测门后的 0/0/0 写成旧根因已解；不把宏隔离探针等同正式 CI release gate。
- 启动失败退场顺序已补：六个启动失败分支在提示/Stop/Join 前先 `SetOffSignal(1)`，最新 Debug ARM64、Release 三架构 Build/Headless/PptCOM/supervisor 全部 exit0；剩余 P1 限定为正常 DComp fallback 的 pre-arm StopProduct、Host Stop 无界 drain 和双重监督 `Failed`。
- 当前总体实施/自动验证约 93%，发布门禁约 72–75%；HF 非 ignored 2366 文件 SHA-256=`c3e686ae75fa347fd3b2958f37f3b2cbd817e7062d5ccea6fdd3a5c5df39661d`，含四个未跟踪 `.cso` 生成物清理缺口。

| 日期 | Work | 单元 | owner | 状态 | 证据/结论 | 下一步 |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-09-27 | 0 | H0 与任务树 | 主 agent | 已验证通过 | H0 8b156fca，任务创建前干净；父任务及 A–G 已创建 | 来源与环境调查 |
| 2026-09-27 | 0 | 规划与 context | 主 agent | 已验证通过 | 八任务 validate 退出 0；大 spec 注入截断，须按路径补读 | Work 0 基线 |
| 2026-09-27 | 0 | HC/H2/环境 | baseline_research | 已验证通过（来源调查） | research/baseline-sources.md：HC 强候选 82f7b7c0/Actions run，H2 正式 Release 20260713a；运行对照未验证 | 核二进制/同机配置 |
| 2026-09-27 | 0 | 架构与测试 | architecture_research | 已验证通过（静态调查） | research/architecture-and-validation.md：当前生产链/验证边界与 Win7 约束 | 规划 review 后测生产路径 |
| 2026-09-27 | 0 | 审计集合 | 主 agent | 未验证 | H0 相对主线断点有 587 个候选 | 建逐 SHA coverage |
| 2026-09-27 | 0 | 独立规划审查 | planning_review | 已验证通过（规划） | research/planning-review.md 复审四项缺口已修，准许 task.py start；不代表产品验收 | 启动 baseline 子任务 |
| 2026-09-27 | 0 | H0 构建与无窗测试 | 主 agent | 已验证通过（构建/测试） | Debug|ARM64 Solution、InkeysHeadlessTests --no-window、PptCOM.Tests 均退出 0；真实 UI/笔/Office 未测 | 冻结性能采样入口与审计批次 |
| 2026-09-27 | 0 | UI3/Draw3 算法与输入子链 | 主 agent | 已验证通过（限于样本范围） | Debug/Release 各有 UI3 19 项算法与 Draw3 生产 ContactInput 三轮；原始日志在忽略 TestResults；benchmark 独立 diff review 已修正通过 | Work 2/3 补整帧与长期资源 |
| 2026-09-27 | 0 | baseline 子任务阶段出口 | 主 agent | 已验证通过（自动部分） | H0/HC 强候选/H2、环境、回归表、性能协议、611 候选冻结；Debug/Release 主 Solution 与适用测试通过 | Work 1；HC/H2 同机、GUI、Win7 保留未验证 |
| 2026-09-27 | 1 | Pen 子型单事务与 FineDial 时序 | 主 agent | 已验证通过（静态/构建/已有测试）；真 UI 未验证 | Bar 四入口统一至 ChangeStateModeToPenTool；条件 PPT 保留 revision；RenderLoop 候选标记同帧复用；独立 review 修正两次交错，Debug Solution/no-window 退出 0 | 处理剩余普通 stateMode 读写竞态、形状/宽色 |
| 2026-09-27 | 0/4 | H0 冻结历史逐 diff 首轮 | audit_review_1/2/3，主 agent 汇总 | 已验证通过（静态逐 diff 范围） | 611/611 SHA 差异已读并合并 central TSV；22 个专项深审、全部动态/目标系统与 HF diff 仍未验证；键盘 hook 误报已纠正 | Work 2/3 深审导入，Work 4 修 findings |
| 2026-09-27 | 1 | stateMode 写入锁与 UI3 值快照 | 主 agent + bar_render_snapshot | 已修复待验证（动态） | IdtState 同锁快照和条件 setter；Bar RenderLoop 同帧值、Layout 纯值重载、交互每消息更新；FineDial/预设/形状长手势带版本；独立 review R1/R2 源码关闭，完整 Debug/no-window 退出 0 | Release 构建与性能限界、真实 UI/PPT 人工交错 |
| 2026-09-27 | 1 | 阶段自动出口 | 主 agent | 已验证通过（构建/已有测试）；需要人工（真 UI） | 迁移表、职责图、状态所有权与 code-spec 已更新；Debug/Release ARM64 Solution 与 --no-window 均退出 0，独立 diff review 记录限制 | Work 2 UI3 专项；真机/交错/锁等待不升级 PASS |
| 2026-09-27 | 4（风险前置） | F-006 禁用 DWM presenter | disable_dwm_presenters | 已修复待验证 | 当前 diff 自动候选 DComp→ULW，强制/恢复 DWM 在 ConfigureWindow 前拒绝；保留 FLIP；独立 review/构建/隐藏 HWND/Win7 尚待 | 完成独立 review、主 Solution 构建及真机门禁 |
| 2026-09-27 | 4（风险前置） | F-012 删除发布故障注入 | 主 agent + release_hook_review | 已修复待验证 | 仅删除 IdtMain 的环境变量故障注入与调用；review 确认正式 -WarnTry/-CrashTry 保留；startup-preview 规范旧条款已修 | 主 Solution 构建和真实启动/崩溃专项 |
| 2026-09-27 | 4（风险前置） | F-008 索引边界、F-010 停止竞态 | autosave_index_bounds / startup_preview_race | 未验证 | 两个不重叠单元已分配，前者 red→green、后者先独立调查 | 收集证据后决定最小修复 |
| 2026-09-27 | 4（风险前置） | F-008 索引边界与 F-010 owner stop | autosave_index_bounds / preview_owner_stop_fix | 已修复待验证 | F-008 独立 red→green、standalone Debug build 通过；F-010 sticky stop 静态实施；主 Solution Debug/no-window 与 PptCOM.Tests 均退出 0；完整 Desktop suite 因 ReplaceFileW ACCESS_DENIED 仍失败 | 独立最终 diff review、Win7/真实 HWND/完整 Desktop 环境复验 |
| 2026-09-27 | 4（风险前置） | Desktop AutoSave 环境区分 | 主 agent | 已验证通过（沙箱外测试范围） | 同一无窗 Desktop 全子集在默认沙箱失败 27 项，在沙箱外仓库隔离数据中退出 0；F-013 标环境，不删除失败日志 | Win7/故障注入/真实恢复仍需独立验收 |
| 2026-09-27 | 4（风险前置） | F-014 更新链网络/ZIP 边界 | release_hook_review + 主 agent | 已确认失败 | 独立 threat model 确认 HTTP 降级、自报 hash、校验前解压、representation 越界 remove；当前 Release 的设置修复入口可达（启动自动线程受未定义 IDT_RELEASE gate 关闭），仍为发布阻塞 | 设计并实施 TLS、受信 metadata、路径与 ZIP/替换防护，隔离回归 |
| 2026-09-27 | 4（风险前置） | F-015 坏配置下标 | 主 agent | 已修复待验证 | IdtConfiguration 仅接收 0..3；Setting 局部快照再 clamp；不变更有效值 | 完整 Solution 构建、隔离坏配置 GUI 回归 |
| 2026-09-27 | 4（风险前置） | F-016 崩溃过滤器与循环 | 主 agent + crash_restart_chain | 已修复待验证 | Shutdown 恢复空旧 filter、进程内一次性门闩、CrashTry 五分钟抑制；详细生产链与恢复边界见 crash-restart-chain.md | 完整构建、独立 diff review、隔离真实 UEF/正常关闭/重启 |
| 2026-09-27 | 4（风险前置） | F-014 传输/ZIP/本地替换补丁 | disable_dwm_presenters + 主 agent | 已修复待验证；来源认证已确认失败 | HTTPS 手动逐跳、路径/hash/JSON/ZIP 上限、固定提取目标、两阶段原子指令、旧 EXE stage+backup；Debug Solution/no-window 已通过第一轮，独立 security review 列签名、真 CDN/Win7 和自定义旧名例外 | 完成最后修补复建与隔离夹具，发布签名/来源合同待决 |
| 2026-09-28 | 2（专项前置正确性） | F-018 离屏 Clear 命令接受缺陷 | 主 agent | 已修复待验证 | Release ARM64 现有 `--bar-eraser-offscreen-test` 红灯 0xC0000005，阶段记录定位 Clear Up；Product Host Running 门后同一无 HWND WARP/D2D 测试 exit0、`failures=0`，无临时打印 | 独立 diff review、真实启动/关闭交错；再建立 UI3 光影性能基准 |
| 2026-09-28 | 2 | F-004 Idle 首帧时钟和零 dt 语义 | 主 agent | 已修复待验证（真 UI） | 首轮 Debug ARM64 Solution exit0，但无窗 3 项失败；修正禁用动画即时完成和测试默认 Pct 曲线预期后，二轮 Debug Solution 与无窗 exit0、EraserAttribute failures=0；spec 时钟合同已更新 | 独立源码 review、Release/最终集成复验；真机 Idle→首帧轨迹 |
| 2026-09-28 | 2 | SVG/path 缓存失效审查 | ui3_svg_path_review、主 agent | F-019 已确认待修，其余性能假设未量化 | research/svg-path-review.md 跟踪现有逐实例 SVG 缓存与路径；Scene widgets 旧位图未随设备失效，独立 agent 正实施双 epoch 回归；未把颜色/路径候选冒充收益 | 收红绿与 diff review，阶段出口前清理 |
| 2026-09-28 | 2 | F-019 Scene SVG/PNG 设备域失效 | ui3_scene_epoch_fix、主 agent、独立 review | 已修复待验证（真窗口） | 生产无 HWND 双 WARP/D2D epoch 旧位图红灯报告 failures=1；成功隐式重建/显式释放清缓存后 Debug Solution 和离屏入口 exit0/failures=0；scene-device-cache-review.md 未见实现阻断，红原始日志覆盖与创建中途故障注入保留缺口 | Release/最终集成与 PageControl 真机回归 |
| 2026-09-28 | 2（并发风险前置） | F-021 底栏两个发布快照内存序 | 主 agent、bottom_dock_seqlock_review/check | 已修复待验证（GUI） | Release `--no-window` 原一次失败一次通过；诊断版同二进制后续 25 轮通过但不抹首败。两组 seqlock 奇数领取/载荷/校验补配对 fence，测试保证采样实际发布；独立最终 diff review 无新阻断，Debug/Release Solution、Debug 无窗、Release 5 轮无窗与生产离屏均 exit0 | 真 GUI 并发/尾延迟、最终跨任务复验；保留首轮失败 |
| 2026-09-28 | 2 | UI3 自动阶段出口 | 主 agent | 自动范围已验证通过；性能/人工门禁未通过 | exact mask 候选像素不等价已撤销；F-004/F-019/F-021 各自独立 review 与 Debug/Release ARM64 Solution/no-window/离屏通过；光影三轮原始数据和 SVG/path 静态成本报告归档于子任务 research | Work 3 Draw3 独立全链性能；UI3 真 ULW/HC/H2、长帧与用户手感保留人工验收 |
| 2026-09-28 | 3 | F-020 Win7 动态 SRV NO_OVERWRITE 可选能力 | draw3_nooverwrite_compat、主 agent、独立 review | 已修复待验证（Win7 真机） | 生产普通笔/Shape 的同一 WARP FL11.0 无 HWND 测试红灯进程 exit1、四项上传/offset 错；按 D3D11_OPTIONS 查询和 false 时 DISCARD 后 Debug/Release Solution 与测试均 exit0，独立 reviewer 核路径无阻断。FLIP 独立保留 | 最新 null guard 与基准新入口后复建；Win7 HW/WARP shader/Present 人工矩阵 |
| 2026-09-28 | 3 | 生产 Draw3 链路和 F-022 几何子段 | draw3_chain_research、draw3_geometry_benchmark、主 agent、独立 review | 已验证通过（窄口径基线），整链未验证 | production-chain.md 跟踪 RTS→contact→modeler→L0/L1/L2→GPU→Present→history/save；新无 HWND CLI 直接调用生产 Laser range planner，Release ARM64 四场景三轮/轨迹 hash 完成，4096 点约 1.25µs 块均摊调用；未证成关键帧瓶颈，未加复杂 hint | 继续 ULW CPU、GPU/资源、输入到呈现可测子段；真 Host/Win7 人工 |
| 2026-09-28 | 3 | F-005 ULW CPU copy/alpha 候选 | draw3_ulw_copy_benchmark、主 agent、独立 review | 已验证通过（受限新基线）；候选否决并撤销 | 初轮重复调用样本因编译器消除风险作废；改单次调用后 baseline/candidate 各三轮、每场景384次完整 DIB/hash 通过。全量收益约1%低于噪声且 P95变差，partial_256 +12%、narrow_long +42%；撤销 helper 融合后源与新 baseline 字节一致，Debug/Release Solution 与无 HWND CLI exit0 | 真 staging Map/ULW 成功 Present、Win7 dirty、长期资源仍需人工；继续 F-023 失败传播 |
| 2026-09-28 | 3 | F-023 raster 失败传播 | draw3_commit_failure_research、draw3_raster_failure_fix、主 agent | 已确认失败待修 | 研究证实 L1/橡皮游标、L0/Shape/Apply 与 landing 有漏判；正式 Debug ARM64 Rebuild exit0 后隔离 WARP 合法不可写 buffer 的无 HWND 红测进程 exit1、8项 FAIL；首次增量 link.exe LNK1000 作为工具链失败另记 | 等实施者冻结 L1+Controller 单一失败门禁，串行绿测和独立 review；F-024 另成单元 |
| 2026-09-28 | 3 | 长文档/history/PPT 保存成本预检 | draw3_history_cost_research、主 agent | 已验证通过（静态研究），动态未验证 | research/history-resource-cost.md 映射生产 InkDocument/History/GPU/AutoSave：PPT 全页深拷贝与按页线性 canonical 查找、Undo 分支旧项保留、缓存预算与队列积压；F-024 文档/footprint 事务缺口单列 | 新生产 CPU 无 HWND history 基准 10/100/1000 笔，之后按数据决定优化，不拿旧 demo 冒充 |
| 2026-09-27 | 1–5 | 产品实施与验收 | 未分配 | 未验证 | Work 0 尚未完成 | 依序执行 |
| 2026-09-28 | 3 | F-025 Laser 第二层提交失败事务 | draw3_laser_transaction_fix、主 agent、独立审查者 | 已修复待验证（真机） | 生产 WARP/FLIP 无 HWND CLI 红3项 exit1→Debug/Release 绿 exit0；按需 RGBA8 scratch 全成功后才换代，失败保留权威点列/旧已提交图层，独立 diff review 无新阻断；两配置完整 Solution 和周边无窗入口 exit0。成功烘干后 device-lost Hold/Fade 与真实呈现仍缺证据 | F-026/F-027 运行期失败保存/双窗处置；真机性能、Win7/FL11 矩阵 |
| 2026-09-28 | 4 | H0 高风险 commit 二次深审与 F-028 | audit_ui3_deep_batch、主 agent | 已验证通过（22项静态审查）；F-028 已修复待验证 | 611/611 已枚举 SHA 实际 diff 已读，22/22 指定高风险 commit 当前调用链复审；`deep-import/ui3-laser/state-draw3-review.md`。发现非整数 Slider 宽度误亮整数预设，Bar Layout/Interaction 最小修补，Debug/Release ARM64 Solution 编译；真实 GUI/Win7/HF diff 不在该静态结论内 | F-026/F-027 与安全/崩溃专项、最终 HF 全差异独立复审 |
| 2026-09-28 | 4 | F-029 parked Desktop 正常退出保存 | draw3_laser_transaction_fix、主 agent | 已修复待验证（Host/GUI） | 同一生产 Desktop 槽选择/可见 history 快照无 HWND 红 exit1→绿 exit0，Debug ARM64 主 Solution 构建0；现有 Desktop service 沙箱外隔离文件/index 全套 exit0。PPT/Whiteboard 活动时现只选 parked Desktop，非 Desktop Clear/关闭开关/空页无请求 | 独立 review、Release 构建、真实 Host Exit/Win7 与 F-026/F-027 fatal 保存链 |
| 2026-09-28 | 4 | F-042 控制唤醒失败 | draw3_laser_transaction_fix、主 agent、audit_ui3_deep_batch | 已修复待验证（完整 Host） | 生产 ContactInput 失败入队路径 Headless 红三断言→最终 exit0，完整 Debug ARM64 Solution exit0；独立 review 无新 P1/P2，确认只覆盖单消费者失唤醒子链 | F-045 顺序门、真实 Clear/Exit/owner 卡死与 Win7/GUI |
| 2026-09-28 | 4 | F-044 PPT 最后已提交版本保护 | ppt_atomic_versions、主 agent | 已修复待验证（进程强杀/最终矩阵） | 旧 worker failpoint 四红；index v2 与每次新物理文件 Stage2 初版 Debug ARM64 standalone Build exit0、沙箱外完整 Tests exit0 | 补 v1→v2/路径拒绝/独立 child-kill、主 Solution/Release、独立审查后才集成 F-043 |
| 2026-09-28 | 4 | F-043 统一 15 秒强制结束/重启 | shutdown_supervisor_impl、主 agent | 未验证 | 用户选 deadline 时无条件强制；新同 EXE helper 接口已冻结，Close/Restart 已先发 offSignal/异步 HideAll，helper 尚未接线 | 等 F-044 数据门完成，再接 IdtMain/CrashHandler/工程和无 GUI child 测试 |
| 2026-09-28 | 4 | F-045 跨 producer 命令顺序 | draw3_control_ordering | 未验证 | 独立 F-042 review 指既有 ContactInput Down/control 非全局 FIFO，已派对抗红测和共享 token + 失败水位设计 | 红→绿、独立 review、Host/GUI 边界 |
| 2026-09-28 | 4 | F-044 旧 durable PPT 与旧版回收安全 | ppt_atomic_versions、主 agent、独立 reviewer | 已修复待验证（最终配置/容量） | 生产物理版本/index v2、child T3 kill/fresh strict Load、v1→v2、路径拒绝；GC/alias 六红→绿，已删除危险按路径回收。standalone Debug ARM64 完整 tests0，主 Debug Solution0 | F-048 retained known IDs、F-041 双轨、Release/Win7/Office/容量与最终独立 review |
| 2026-09-28 | 4 | F-045 Draw3 命令/接触屏障 | draw3_control_ordering、主 agent | 已修复待验证（真 Host/RTS） | 入口两红→绿、多命令红→绿、Controller 生产 CLI 显式 PID exit1→0、Debug ARM64 Solution/Headless exit0；真实 Run 使用被探针测试的 DrainIngressBatch | 独立 F045 diff review，GUI/RTS/Present/保存与 Release 复验 |
| 2026-09-28 | 4 | F-043/046/047 退出与崩溃后置拉起 | shutdown_supervisor_impl、主 agent、独立 reviewer | 已修复待验证（真 UEF/GUI/Win7） | 精确父 HANDLE、15秒/自身低层兜底、UEF CrashRestart 等旧死后唯一拉起；Debug ARM64 Solution0，无GUI故障注入新红→首绿 race 失败→最终两轮 suite0，15秒 Close ~14.95秒 | 独立最终 diff review、真实未处理异常/窗口 owner/新实例 ready、Release/Win7；双保护皆失效及 fallback-only 无重启边界 |
| 2026-09-28 | 4 | F-048 PPT retained ID strict Load 缺口 | draw3_control_ordering（独立发现）、主 agent | 未验证 | 当前静态链：UInk retained SlideID 可能不在 index slideIds，fresh import TopologyMismatch；F-044 最终 reviewer 正写报告 | 分离 F-044 事务证据，生产 worker 两条红→绿，再审后实施 F-041 用户选择 |

## 文件所有权

主 agent 唯一写入父任务账本、跨域接口、IdtState、公共 RenderPipeline、WindowService、工程文件与公共测试入口。研究 agent 各只写指定 research 文件；后续 implement agent 每次列出不重叠的具体文件。构建输出目录只允许一个 MSBuild/测试进程；性能采样不与编译/扫描并行。
