# Research: 画布卡死调查的证据等级与条件化方案复核

- Query: 复核 design.md、implement.md、视频时间线及各研究结论，检查候选是否被误当根因、末段清除旧墨迹是否产生矛盾、现有日志能排除哪些链；区分旧 Debug 运行、HEAD 验证及本机构建依赖问题。
- Scope: internal；只读文档、既有图片/日志/窗口快照与本地文件身份，不执行 build、GUI、Git，不修改共享文档或产品代码。
- Date: 2026-10-15（Asia/Shanghai；任务于 2026-10-14 发起）

## Findings

### 1. 复核结论

当前设计的主要证据口径成立：`design.md:11` 明确尚不能确定最后一级根因，修复表以“被证实的条件”为入口；`canvas-input-path.md` 与 `rts-lifecycle-followup.md` 均已把“RTS交付/先前callback占用、decoder持续失败、admission”说明为取证顺序，而非已证实因果链。没有足够证据宣布“根因已定位”或选择一条正式行为修复。

截至本复核采样，尚未完成当前 HEAD 的动态验证。首轮运行是已存在的 Debug 产物，启动 edition=`3.0.0-20260811a`，与 HEAD 源码 edition=`3.0.0-20261014a` 不同；首轮 full solution build 已因 Abseil 链接错误失败，第二轮使用已核对共享 provider 的构建日志仍在编译。最终报告必须等待主会话取得实际退出码后更新，不能预先写 HEAD 构建/复现通过。

需要收紧的主要表述是：74–75秒的“旧墨迹从画面消失”与“Clear命令确实处理并成功呈现”仍有一层证据差；COM只读查询也不能仅以“有界”二字宣称不会阻塞。诊断与正式修复的授权/执行边界还应区分，见第7节。

### 2. 已检查的材料和证据等级

| 材料 | 本次实际核对 | 证据等级 / 限制 |
| --- | --- | --- |
| `design.md`、`implement.md`、`prd.md`、`task.json` | 完整阅读；任务仍 planning，本轮不修复 | 需求和待执行方案，不是运行结果 |
| `research/video-timeline.md` | 完整阅读操作时间线、字段解释与版本限制 | 主会话的完整视频审读；秒级采样有量化误差 |
| `Build/CanvasFreezeInvestigation/video/detail-07.jpg` | 直接查看：保留红墨迹，软笔高亮；Win32 thread=13548，Draw3 frame/present thread=2064；同 tick 周围新 seq | 直接图片证据，只覆盖该采样时刻，不是全程原始日志 |
| `Build/CanvasFreezeInvestigation/video/end-01.jpg`、`end-02.jpg` | 直接查看：红墨迹消失、软笔仍高亮，Win32采样日志仍前进，没有新笔迹 | 直接末段画面；单张图没有 Clear处理回执或 HWND显隐事实 |
| `research/canvas-input-path.md` | 完整主要结论，补读命令边界与排除表 | 当前源码调用链；视频动态事实标为主会话提供 |
| `research/rts-lifecycle-followup.md` | 完整阅读 writer gate、lifecycle状态矩阵、实际COM与本地镜像差异 | 当前源码事实与条件候选分开，未发现确定应用侧反向等待环 |
| `research/settings-window-path.md` | 完整主要结论，重点复核旧产物白屏、capture、owner、WCT及DXGI风险下调 | 当前源码边界；旧运行观察不能映射为HEAD复现 |
| `research/draw3-render-and-diagnostics.md` | 与后续输入/RTS研究交叉核对 | 早期渲染研究；RuntimeSnapshot输入计数须附“是否启用”的后续限制 |
| `Build/CanvasFreezeInvestigation/stdout-native.log` | 实际检出三组 Down/Up arrival 与 published=1 | 旧Debug在本机成功运行证据，不是用户故障现场 |
| `run/log/idt1791993530158.log`、`idt1791993585203.log` | 两份首行 edition均20260811a | 直接产物身份标签；不等于2026-08-11编译时间或精确commit |
| `settings-wct.json`、`settings-open-windows.json` | WCT 112线程，全部单ThreadObject、cycle=false；指定Setting/overlay/Draw3/RTS也如此；GUI快照capture=0 | 一次旧运行的有限采样，不是“排除所有死锁” |
| `solution-head.log`、`.exitcode`、`dependency-check.json`、`abseil-environment.json` | 首轮退出1；Abseil unresolved；local头缺失与shared头/库存在；包版本清单 | 构建环境证据，不是画布故障根因 |
| `solution-head-shared-provider.log` | 复核时仍在编译，CL include已指向完整共享provider | 只能证明构建已使用替代provider，不能提前记成功 |

未重新访问网络。本复核不把研究文档中引用的微软说明再扩张成新的系统因果保证。

### 3. 74–75秒旧墨迹消失对假说的约束

`video-timeline.md` 写约75秒，主会话与其它研究写约74秒；目前都指同一末段，建议统一成“约74–75秒”，避免把提帧采样近似写成精确动作时间。Captura计时有偏移，不能拿画面里的 `00:01:22` 反驳 MP4 约74秒。

本次直接查看 end-01/end-02 确认旧红墨迹已经不在，软笔仍高亮，之后尝试未产生可见新墨迹。以下区分必须保留：

1. **直接事实**：旧墨迹从画面消失。
2. **结合主会话连续视频动作的有力推断**：点清空后画布命令/呈现至少推进了一次。
3. **尚待计数验证**：真实 Clear command 已在 controller安全点完成，CPU history/content revision与当前成功输出同时更新。

如果第3项成立，则“50秒以后同一个旧active使全部canvas command一直无法处理到视频结束”在逻辑上不成立。它仍不能排除50–74秒之间出现过短暂命令等待、另一路局部gate，或Clear之后新的输入故障。

仅凭末段画面，仍须保留“显示表面被隐藏/切换、场景改变”等有限替代解释。需要 Clear处理计数、内容revision、输出target/revision、实际Present回执与两窗visible配套验证。`canvas-input-path.md` 与 `rts-lifecycle-followup.md` 已保留这一限制，`design.md:43` 也有“除非另一路显隐”条件；当前没有互相矛盾的全程死锁定论。

建议最终用语：“末段点清空后旧墨迹从画面消失，因此不支持‘整条画布命令链自失效起一直停止’；实际落笔链仍未恢复。”不要把它写成“全部画布功能都已经正常”或“已证明GPU绝无问题”。

### 4. 现有日志足以排除什么，仍不能排除什么

| 已有事实 | 在对应时间/事件范围内可排除或明显削弱 | 仍不能排除 |
| --- | --- | --- |
| `tool=0` 与软笔高亮 | “0是未选工具/Selection”的字段误读 | selectionMode独立值、完整bridge revision或工具样式是否全部收敛 |
| 新seq/tick的Drawpad `mouse accepted=1` | 对这些已收到的事件，Win32光标过滤没有拒绝；同一时刻Drawpad WndProc并未被productCallMutex永久挡住 | RTS没有交付Down、decoder/gate拒绝、只是MouseMove到达而真正Down缺失；不能从accepted推导墨迹输入正常 |
| 新seq/tick的Draw3 `frame` 与末端 `present` 行 | 在该循环运行区间，绘制owner并非一直停在同一mutex、Map、Present、Resize、WaitForCommitCompletion或console write中 | 输入到消费/几何的逻辑缺失、API后来才卡住、失败后返回并重试；不能推广为整个视频区间无等待 |
| 空dirty的 `present success=0` | 排除“这一行已经证明DXGI/ULW调用失败”的解释 | 真正未尝试Present，或其它帧有真实失败；应看attempt/HRESULT/recovery |
| 约49秒 selection-ulw真实成功及revision=5 | 辅助ULW至少有一次真实成功；不是所有presenter从启动就失败 | 之后Primary目标的ready/visibility、之后设备/窗口变化 |
| 橡皮光标能出现 | 对应cursor反馈/渲染可以产生视觉 | 真实橡皮RTS contact已到、已被admit或执行擦除 |
| 约74–75秒旧墨迹消失 | 全程所有命令/呈现完全不前进的笼统解释 | Clear未核对回执时的隐藏/换场景；输入链仍失败 |
| 旧native run三组Down/Up `published=1`且画出新线 | 该旧产物、本机配置和此次动作没有触发持续RTS丢输入；不能称其画布复现失败 | 用户视频、HEAD、真实触摸/笔、多屏CloneOrMixed或不同交错时序 |
| 旧native run GUI线程capture=0 | 采样时这些GUI输入队列未报告capture | 用户故障时capture、短暂capture残留、采样前后变化；worker GetCapture=0尤其不能外推 |
| WCT所有cycle=false且单节点 | 这一次查询没有识别到可关联的等待环 | 用户态mutex/SRW、原子自旋、GPU等待、未命中时段或没有提供的用户态栈 |
| 没在某截图看到rts-input | 没有足够依据作任何“零callback”结论 | 日志滚动、裁切、遮挡、被覆盖/丢行或未被render Flush |

当前更稳妥的调查顺序仍是 `RTS arrival → decoder/published → controller dequeued/admitted → geometry/history → actual Present → window visibility`。这个顺序来自可判别性，不是对某一个根因的概率证明。

### 5. 二进制身份和首轮测试的证据边界

| 基线 | 已知身份 | 可报告结果 |
| --- | --- | --- |
| 用户10月11日视频 | Inkeys64标题路径、录制日期与环境日志；没有EXE hash、精确commit或编译身份 | 真实用户运行的持久失效现象；当前源码可解释字段语义，但不能假定所有当前修复已在视频EXE中 |
| 首轮隔离run | 启动edition=`3.0.0-20260811a`；实际已有rts-input诊断；不是本轮HEAD新构建产物 | Setting曾白屏但能WM_CLOSE；后续绘制/组合步骤成功；应称“旧Debug副本未复现画布失效” |
| 当前HEAD | PRD记录源码HEAD=`5d319bbf`、源码edition=`3.0.0-20261014a` | 只能报告静态审计；待本轮实际构建完成并核验复制/运行的EXE身份后再报告动态结果 |

本次只读计算隔离 `Build/CanvasFreezeInvestigation/run/Inkeys.exe`：

- SHA256：`C1AE753DD96B5AC77B539854BD8FEF0990D48C8695271A2CDAE5C9E11DC2DDDE`。
- 大小：64,426,496 bytes。
- LastWriteTimeUtc：`2026-10-14T10:59:42.9113597Z`；CreationTimeUtc：`2026-10-14T15:58:49.2739277Z`。

这些值是本次读取时的快照；主会话若随后替换run目录EXE，需要给新产物另记hash，不能让旧日志和新二进制混成同一个run。edition中的20260811只是标记，不可据此声称文件在8月11日编译；该产物同时已有后来加入的rts-input更说明不能用标签反推出精确源码日期。

首轮白屏不能证明本案根因：它与持久落笔失效不是同一个观察，且旧运行的RTS published、后续笔迹、WM_CLOSE、Bar操作和WCT均削弱“这次白屏为永久全UI/DXGI死锁”。应保留白屏作为独立未定位现象，不从它展开当前HEAD的正式修复。

### 6. 构建环境复核

首轮 `solution-head.exitcode` 为1。第一个有意义的错误在 `solution-head.log:2262`：`absl::lts_20250512::status_internal::StatusRep::Unref` unresolved（LNK2001）；随后MapToLocalCode、FailedPreconditionError等LNK2019及最终LNK1120属于同组链接后果。日志总计32 errors；不能把最后汇总行当成32个独立源码问题。

`dependency-check.json` 的10个override版本核对成功只证明包元数据；`abseil-environment.json` 则直接记录工作树本地 `include/absl` 的关键头不存在，而共享provider有匹配头和absl库。主会话另外已核对两边的override/Abseil ABI/ARM64。当前证据支持“工作树provider安装内容不完整/元数据与实体不一致”的环境分类，不支持为了通过而修改Draw3源码、依赖版本或工程链接合同。

主会话正以命令行 `VcpkgInstalledDir` 指向 `D:\Project\Inkeys\Repo\Inkeys\VcpkgInstalled\Arm64\` 的完整provider，保持产品/工程不变。复核时第二轮CL命令的include确已指向共享根，但没有看到最终退出码，**不能写成已成功修复构建环境或HEAD编译已通过**。

`implement.md:26` 的记录应由主会话在构建结束后补充：初轮status检查不足、实体缺失、首轮结果、替代provider实际路径/身份和第二轮命令/退出码/日志。成功时应明确验证的是“使用该已核对本地provider的HEAD”，并保持环境问题与运行期画布调查分开。失败时继续定位首个新错误，不把它自动归到Abseil。

### 7. 需要主会话收紧的文档/最终口径

这些是表述与验证要求，不是已发现的产品缺陷；本代理未改共享文档。

1. **未完成项必须保留**：`implement.md:10` 的HEAD构建和下一项HEAD复测在本次复核仍未完成。旧Debug成功不应使它们变为已完成；对未复现的根因不能写“已定位/已修复”。
2. **“有界COM观测”要有实际合同**：`design.md:23` 的get_Enabled/get_HWND可能自身阻塞。rts-followup第8.3节已正确要求受控owner路径、记录begin/end，不在callback/writer/UI热路径调用，无法安全调度时先外部栈；不要通过另开线程后强制终止来伪造超时安全。
3. **诊断已获本轮授权，正式修复尚未授权**：用户允许本轮为调查修改代码。`implement.md:14` 的“后续批准”把诊断与修复列在一起，容易让最终回复错误归因于缺授权。若本轮选择只交设计，应说明尚缺现场或本轮取舍；不要说用户未允许诊断。正式恢复、改准入/锁协议等行为修复仍留待后续批准。
4. **恢复事务不是无arrival的自动结论**：`design.md:31` 中“RTS callback停止”若只由截图缺行判断还不够。必须先确认正确PID/EXE、Win32实际Down/Up、独立观测仍前进、最后writer阶段未卡住及计数已启用/日志未丢。未分清先前callback阻塞与collector不交付时，不应直接选择重绑作为根因修复。
5. **RuntimeSnapshot计数附启用状态**：当前普通Cursor开关不启用所有Coordinator metrics。早期渲染报告列出的input计数能力，最终必须加“计数已启用且有baseline”条件；全0不是无输入证明。即使字段全是原子，RuntimeSnapshot的业务锁仍使其不是无锁watchdog。
6. **clear表述保留观测层次**：建议统一约74–75秒，使用“点清空后旧墨迹从画面消失”，不用单张图声称document/history与成功Present均已严格验证。

### 8. 条件化方案的最小准入证据

| 待考虑的修复分支 | 必须先取得的事实 | 不足以触发该分支的现有证据 |
| --- | --- | --- |
| RTS恢复/同HWND重绑 | 运行身份正确；实际Down/Up命中；callback交付停止且不是writer未返回，或实际enabled/HWND绑定确实失效；受控恢复试验有清楚前后差异 | accepted-mouse、截图无rts-input、初始化成功、IsInitialized=true |
| decoder/shared-scale失败域 | Down arrival与no-decoder稳定对应；具体requested/first tcid、generation、lifecycle、scale和COM HRESULT说明失败点 | 某次mapping失败、source可调用、包版本/显示拓扑标签 |
| controller准入/边界收束 | Down持续published；controller dequeue/gate/drop原因和旧runtime终态明确；Clear处理序号与故障窗口可对齐 | 橡皮光标显示、空dirty、全0但未启用的计数；末段Clear已推进却仍声称全程卡在旧active |
| Setting capture收束 | 故障当下隐藏Setting持capture或真实hit-test不命中Drawpad；只释放其自身capture后能恢复 | 源码未显式ReleaseCapture、非失败时刻capture=0、独立worker GetCapture=0 |
| ImGui/DXGI锁范围修正 | 同一现场的render/Setting HWND栈与锁owner对应，或原子阶段明确识别DXGI/同步消息等WndProc锁 | 当前源码有锁、旧Debug白屏、WCT仅blocked/cycle=false |
| owner同步与绘制状态解耦 | 工具请求卡在owner提交前，明确enqueue/execute/return及desired/applied版本，实际等待影响发布 | tool=0、250ms重试文字、跨线程owner本身 |

条件化方案不应一次修改所有分支；确认一个故障路径后才决定最小diff。独立mock可验证日志或假定重入下的锁行为，不能把“mock能造死锁”提升为Windows真实RTS重入已经被证实。

### 9. 可用于最终报告的结论模板

“视频确认的是落笔/擦除失效，而非整个进程停止。在约50–53秒，Drawpad的Win32光标事件和Draw3循环仍推进，空dirty的success=0也没有证明GPU调用失败；末段点清空后旧墨迹消失，进一步削弱了整条画布命令链永久阻塞的解释。调查范围因此优先收敛到实际RTS contact交付、解码/发布与controller接纳，但现有视频没有完整RTS状态或故障栈，尚不能确认最后一级根因。首轮隔离测试使用版本标记20260811a的既有Debug产物，未复现持久输入失效，不代表HEAD结论。HEAD构建/运行结果应另列实际完成值。本轮没有实施正式修复，已保留按现场条件选择的最小修复方案。”

模板中的HEAD状态必须由主会话在最终出结果后替换，不能原样把“待完成”当作最终验证结果。

## Related Specs

- `.trellis/spec/native-desktop/errors-logging-and-resources.md:3`：直接确认、推断与待确认分层，静态风险不是已确认缺陷。
- `.trellis/spec/native-desktop/draw3-integration.md:95`、`:345`、`:412`：唯一Host/RTS、独立绘制owner、输出与内容握手。
- `.trellis/spec/native-desktop/input-and-ink.md:67`、`:423`：cursor/contact区别、诊断不能冒充修复证明、Closing/Quarantine回收边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1927`：历史Setting闭环及当前defer合同；不能把历史修复未经版本核对套到视频。
- 根AGENTS.md：最小修改、环境错误与源码错误分开、完整solution Debug|ARM64验证、不得为绕过本机问题改项目。

## External References / Versions

本次无新增外部检索。当前HEAD/版本信息来自任务PRD及主会话提供；日志edition、EXE SHA256、WCT与链接错误来自本次只读核对。原视频的完整二进制身份仍未知。

## Caveats / Not Found

- 未获得真实用户故障的全量原始console、RTS lifecycle镜像/实际COM Enabled、完整contact与controller处理计数或线程栈。
- 本复核没有执行任何build/GUI/Git，也没有修改design/implement/其它research或产品文件；唯一写入为本文件。
- 构建日志为进行中的共享产物，本文件只记录读取时状态；以主会话随后记录的最终退出码/二进制身份为准。
- 尚未发现支持立即实施某一正式修复分支的决定性证据；本轮交付应明确是故障边界与可验证方案，不能承诺问题已经修好。
