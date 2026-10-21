# Research: Canvas admission and recovery audit

- Query: 全面核查画布拒绝输入的状态与解除路径；寻找 PPT 翻页/其他窗口交互之外可独立导致持续不可写的机制，并提出无窗口验证和最小修复。
- Scope: internal，静态生产源码审计；只写本研究报告，未运行程序、GUI、窗口测试或故障注入。
- Date: 2026-10-17

## Findings

### 结论与优先级

1. **P1 / 已有确定性交错证据**：PptInfo raw 页先读、descriptor revision 提前消费，descriptor 已缓存但尚未应用时失去重算机会；当前 target 与 UI 页码不一致，UI-ready 永远不能匹配。详见 `ppt-turn-controller-audit.md` / `ppt-turn-events-audit.md`。不是 F1–F4 重复发现。本报告没有重复运行该摘录实验。
2. **P1 / 明确无界等待，可立即做真实 worker 无窗口验证**：`Draw3.PresentationAutoSave.cpp:148` 命名 mutex 无限等待。阻塞 Load 或位于 Load 前的 Save 时，输入停在 loadPending，没有 completion，四次 IoError 重试机制根本未启动。锁 owner 必须仍存活或长期运行；已退出 owner 的 abandoned mutex 当前会成功取得，不能归因于上次崩溃留下死锁。
3. **P2 / 明确资源故障边界，尚无现场证据**：worker completion 分配异常可以吞掉终态；Host 将已取出的 completion 包装为 CanvasCommand 时分配异常还会使整个绘制 Run 退出。两者不能通过简单强制打开 admission 解决。最小本轮修复可聚焦前两项，并明确资源耗尽仍是限制。
4. **P2 / 条件性，不应误称普通翻页必现**：CPU slot/拓扑切换失败后未保留重试命令，旧/空 ready 被清除，目标稳定后不会自然补发。这类失败需资源分配失败或异常拓扑前提，应另设故障注入后再定修复，不宜改成无条件 ready。

### 生产拒绝与恢复矩阵

以下按不同状态 owner 拆开。此矩阵覆盖直接入场门、生命周期/设备拒绝及文档呈现依赖；不声称已经形式化证明所有 GPU/COM/OS 路径无死锁。

| 层 / 状态 owner | 拒绝入口及依据 | 正常解除或重新尝试 | 持续停写风险 / 评价 |
| --- | --- | --- | --- |
| Host presentationTargetMutex | `Host.cpp:551–565`：commandScenePending；PPT 无 target；ready identity 不符；suspended；requirePresentationUiReady 且 UI identity 不符；从 PPT 退出尚未应用其他 workspace | `Host.cpp:841–855` WorkspaceChanged 成功呈现回执更新 workspace/ready、清 commandScenePending；`:2177–2206` UI ack / suspend 改变后刷新门 | target/UI 缓存不收敛已确认；不能省略 identity 或强制开门，否则可在错误幻灯片写入 |
| Host bridge pump | `Host.cpp:1293–1304` 仅新 targetRevision / restoreLatestScene 入队；`:1322–1355` 命令跨场景先关闭 admission | FIFO 命令完成，document Present 后 WorkspaceChanged；新目标继续泵入 | 合法稳定目标去重本身正常；若下层失败丢掉“应用目标”责任，普通 heartbeat 不补发 |
| Controller current-load | `DrawingController.cpp:10380–10385` loadPending 或未初始化且 mutationRevision=0 拒绝新 Down | `:12594–12635` completion 清 pending，Loaded 严格物化安装或确认 NotFound，stage ready；IoError 进入既有有限退避 | worker 卡住或丢 completion → pending 不清，retry 无法推进；持续 IoError/Invalid/manualOnly 是安全停写，不应凭空创建空白数据 |
| Controller retry state | `:844–917` manualOnly、exit、目标/代次不符、已有 pending 会阻止提交 | 成功取消；目标/代次变化重置；允许的同 target 明确重发；250/500/1000/2000ms 四次预算 | 预算耗尽继续关门是已批准 F2 合同，不能把无限自动重试当作必要修复 |
| Controller document switch | `:12694–12720` 清 pendingWorkspaceReady 后 CPU switch、topology rebind、page_count 失败 continue | 新目标/成功加载/后续有效切换重新 stage | 稳定 revision 失败后不会自动重放；仅静态条件性机制，未证明合法普通页触发 |
| Controller replay/Present | `:8325–8342` stage 身份；`:14954–14958` Present 成功且无 viewport recovery/refresh 才回 ready | dirty/replay/recovery 重试，成功帧发布同 identity | 持续 GPU 故障必须保持停写；不能将 CPU switch 直接当作 ready。已见普通 raster 重试和 device removal 恢复分支 `:14930–14943`，未做完整 GPU fault 审计 |
| Controller FIFO 边界 | `:10352–10416` commandBoundaryPending 暂停新 contact 出队，保证命令隔离 | `:13699–13700`、`:14722–14723` 命令队列清空后解除；admission revision 变化先 seal active | 不无限等待真实 Up；`:14623` 合成终态后 quarantine 物理路由。未发现“持笔切页必然 active 永不为空” |
| Controller selection | `:10373–10378` Whiteboard selection 模式拒绝绘制 | 切回绘制工具；新 Down 进入新模式 | 有意产品语义，不是卡死；鼠标光标消息是否 accepted 不证明 RTS 绘制 Down 是否接受 |
| Contact coordinator admission | `ContactInput.cpp:893–923` 奇数 revision 封门；旧 Down revision 不可在开门后补画 | SetAdmissionBlocked(false) 产生新 revision；新 Down 正常接受 | 不允许旧 contact 跨页；门开后必须重新 Down |
| Contact slot / quarantine | `ContactInput.cpp:738–768` 非有限坐标、无 free slot、queue 入队失败拒绝；`:867–891` quarantine 不回收直到真实终态 | Up/Cancelled 收尾释放；`:930+` CloseAllProducerContacts；RTS 重复同 key Down 先取消旧 route | 丢失终态且不同 key 无限累积可耗尽池；正常同 key 重用及 Disabled/Error 已有恢复，不足以宣称新 bug。不能靠超时回收仍持有物理 contact 的槽，否则旧 Move 污染新代 |
| RTS decoder lifecycle | `RealtimeStylus.cpp:1138–1166` 无 source/packet、no-decoder、TouchPad 等拒绝；`:1723–1746` 未有 decoder 时按 Down/InRange 补建 | Enabled 开 lifecycle；缺 scale/decoder 可后续重查；Tablet/Mapping 更新重建 | Disabled 后无 Enabled 属 producer 生命周期停止；本轮没有证明正常窗口切换会永久断 lifecycle |
| RTS active binding / decode | `RealtimeStylus.cpp:1171–1232` duplicate Down 先 Cancel 原 binding；binding 满/解码失败/PublishDown 失败均清当前绑定并平衡 Touch | Up/Cancel、Error/Disabled/Tablet reset 关闭；后续合法 Down 重试 | `:1456–1471` Error 保留 decoder lifecycle（已存在），不能再当新“Error 永久禁用 RTS”问题 |
| Touch/Pen 导航抑制 | `DrawingController.cpp:9090–9175` touch pan 下 Pen 记录为 suppressed，触摸可能变成导航而不画；`:10428–10434` 等物理终态/新合法笔输入解除 | 真实终态清 suppressed；独立 Pen Down 未落入 pan 批次会重置 | 设备/手势级拒绝，不是所有画布输入门；`HasQuarantinedContacts` 在`:13568`用于 haptics，不是全局绘制拒绝 |

### 命名 mutex 的最小修复及真实无窗口测试

生产位置：`Draw3.PresentationAutoSave.cpp:136–159` NamedMutexGuard，Save `:1158`、Load `:1482`；唯一 worker `:1723–1789`，CloseAndDrain `:1917–1924`。

可审查最小方案：将单次 Acquire 从 INFINITE 改为固定有限等待（例如 1000ms，具体预算由实现选择并在常量/注释写明）。超时/失败保持 Acquire=false，让既有 Save/Load 错误路径产生 IoError；**WAIT_ABANDONED 仍表示已取得所有权，应正常释放**。未取得锁不读取/写入索引，不删除文件，不返回 NotFound，不安装空白数据。这样临时占用释放后现有四次退避可恢复原墨迹，而不是永久 loadPending。

限制：有限锁等待不等同整个 I/O 超时；锁内 filesystem、网络路径、杀毒软件过滤器等仍可能耗时。CloseAndDrain 仍逐个处理已接受队列，不能承诺固定1000ms退出；不要 detach 持有 service 指针的 worker，也不要 TerminateThread。

建议添加到既有 `--presentation-autosave-only` 或真实 current-load CLI，**仅 mutex/event/thread/file，不创建任何 HWND**：

1. 使用全新隔离 autoSaveRoot；通过真实 Save 写入非空已提交 UInk，保存文件哈希/墨迹身份。
2. 独立辅助线程（必须与 worker 不同线程）按同样 root normalization/hash 取得真实命名 mutex，并发 ready event；Win32 mutex 属于线程，使用单独线程已足以产生跨线程互斥，无需启动程序窗口。
3. 提交真实 Current Load，外部 owner 保持占用超过 Acquire 预算；断言真实 completion 在有界观察期返回 IoError，target/slotGeneration/loadKind 不丢，文件字节不变；未出现 Loaded/NotFound。
4. 释放 mutex，驱动真实 current-load retry/helper，再取 Loaded 并物化/核对原墨迹；应恢复 admission 所需 initialized 条件。只测 Acquire bool 不足以证明 worker→retry 链。
5. 再测 Save 在前 Load 在后：两者均在锁未释放时产生有界失败终态，后续释放后的新请求成功，排除队头阻塞。测试时少量队列，不把总队列时长误认为单请求等待。
6. 保留 abandoned-owner 用例（辅助线程获得后退出而不 Release，Windows 返回 WAIT_ABANDONED），确认正常取得并释放；正常无竞争对照也保留。
7. fixture必须RAII/finally保证释放、join，设置安全超时；测试父进程强杀只能判失败，不能判正常服务退出成功。不碰用户真正 AutoSave root。

### completion 丢失和绘制线程停止：更深的资源边界

- `PresentationAutoSave.cpp:1737–1756` completion 身份复制位于内层 try 外；字符串/target 拷贝异常落入 `:1791–1796`，只打印日志，工作项已出队，最终回执丢失。
- `:1699–1718` PushCompletion 中 deque 分配失败只记录日志返回；调用者以为提交了终态，controller loadPending 没有释放。诊断 failed/loaded/committed 计数在 push 前增长，计数不证明 completion 可取。
- `Host.cpp:1387–1396` 先 TryTakeCompletion 从 service 删除，再 make_shared/Enqueue；WindowControl.cpp:417 deque push 也可分配。异常穿过 ConsumeBridge→Run，Host.cpp:1684–1706 捕获，RequestExit、释放 controller/presenter/GPU，`:1710` running=false。UI是另一线程，符合“UI活、画布不再处理”的外观，但这只是明确的异常机制，**未证实用户现场发生低内存，也未执行故障注入**。
- 不建议只在 Host catch 后继续 Run：部分消费、stage、数据结构可能已不一致。更稳方向是 accepted request 在提交时预留终态节点/身份，worker 以 move 填充并采用不分配的完成链；Host 保留待交付 completion 直到命令入队成功。需要贯穿队列生命周期的设计及故障注入，较单一 bounded-mutex 修改范围大。若只修某个 catch，必须明确另外两处仍有风险。

### 同类 desktop mutex

`Draw3.AutoSave.cpp:254–290` 命名 mutex 以 root+date 生成，`:270` INFINITE；Save `:518–520` 和恢复/导入 `:880–889` 取得锁，CloseAndDrain `:1368–1376` join。它与 PPT service 使用独立 worker/不同 mutex 名，**不能把 desktop 锁占用直接说成 PPT 冷 Load 队头阻塞**。Desktop 的 Clear undo 恢复等待、保存/退出受影响；本轮只确认同类无界等待，未证明全局 Desktop 落笔 admission 因此关闭。

## Files found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：Host gate、工作区/文档/UI ready、持久化 completion 到命令的交接及 Run 异常收尾。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：Down 准入、手势抑制、FIFO 边界、slot 加载重试和成功 Present。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp`：slot/route generation、quarantine、admission revision 和 wake。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp`：decoder、active binding、重复 Down、Error/Disabled/Tablet 恢复。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`：CanvasCommand 队列与 control wake。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp`：PPT root mutex、串行 worker、completion 和 shutdown。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp`：Desktop 同类 root+date mutex。
- `inkStrokeModelerTestTests/contact_input_tests.cpp:3447`：既有 `--presentation-autosave-only` 命令分派，可承载无窗口存储回归。

## Related specs / External references

已读 `.trellis/workflow.md`、`.trellis/spec/native-desktop/index.md`、`input-and-ink.md`、`canvas-freeze-boundaries.md` 以及 `.trellis/spec/ppt-interop/native-session-ui3.md`。主要边界：旧 contact 不跨页；CPU/Present/UI identity 完整后才准入；F2 保留有限重试及耗尽安全语义；产品默认测试可能创建窗口，本轮禁止执行。本次仅分析本地源码，无外部文档、版本或平台保证引用。

## Caveats / Not Found

- 只写本报告；未读 implement.jsonl/check.jsonl，未修改产品/配置、未执行 git、构建、程序或窗口。
- 新 mutex 与异常机制为静态证据；上述测试是建议，不是已执行结果。主会话若实现，应补真实 worker 无窗口证据。
- 本报告未对 GPU 驱动内部等待、Office COM 内部调用或所有 OS 消息图做穷尽证明，不能宣称没有其他死锁。
- 旧 F1–F4 已修边界不算新发现。当前用户反馈不能确定 Office/WPS、设备、文件占用、多实例或资源耗尽条件。
- 行号对应审计时工作区，其他 agent 修复后可能变化；按函数名定位更稳。


## 2026-10-18 处置与保留边界

以上为10-17静态审计原始记录，行号和“未故障注入/仅建议/禁止GUI”不代表最终状态。raw/descriptor收敛、同页暂停UI身份、PPT与Desktop锁上限、PPT Accepted终态交接现已实现；真实operator new与worker故障注入、持锁/释放后恢复、旧非空墨迹保护、生产CLI及真实PowerPoint/设置GUI结果见final-validation-20261018.md。身份、代次、Present和UI-ready门未绕过；持笔换页在物理Up前完成新UI-ready，Up后新Down落笔通过。资源故障下CPU拓扑切换失去应用责任、Desktop completion全链异常、持续GPU/Office/RTS/OS内部故障仍非本轮PPT终态保证；没有将这些条件性风险宣称已全面修复，也未以超时强设空白/ready。
