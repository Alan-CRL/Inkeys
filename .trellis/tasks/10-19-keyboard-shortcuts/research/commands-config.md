# 快捷键业务、状态转换与配置研究（仅规划）

调查日期：2026-10-19。只读调查现行源码；没有开始任务实现、修改产品文件、运行构建或 GUI。

## 最新需求优先级

上传的未来规划不是本期完整范围。本期仅固定 Ctrl+Alt+X、Ctrl+Z、Ctrl+Y、Ctrl+Shift+Z、Ctrl+Delete、Ctrl+Q，逐组合开关，不开放自定义或提示角标，不继续 PPT 键盘翻页接管。

根代理转达的最新用户覆盖：**分离式批注 → Selection 目前直接清空，合并式批注 → Selection 保留墨迹；两种样式 Selection/既有 ClickThrough → 最近批注工具均不清空。** 不新增独立穿透快捷键。早先“双样式全局往返保留墨迹”已撤销。

用户已确认 Ctrl 保持按住的两次独立 Delete：第一次清空当前页保持当前工具；第二次进入 Selection；两种样式相同，原本空白也适用。根代理当前设计采用连续 Ctrl 会话，不附加任意 500ms 时限；需有 Delete Up 才能再次触发，自动重复不算第二次。

## 1. 已有状态业务可复用点与陷阱

| 证据 | 当前行为 | 本期含义 |
| --- | --- | --- |
| `Inkeys/IdtState.h:21`、`:82` | IsComputerInteractionMode 包含 Selection 与 ClickThrough；lastAnnotationMode 初始 Pen | 功能键不能用墨迹是否为空或 Bar 展开状态判断归属；两种电脑操作态均交还普通功能键 |
| `Inkeys/IdtState.cpp:204`、`:1307`、`:1354` | Pen/Shape/Eraser 切换更新 lastAnnotationMode，保留笔型、laserActive、颜色/粗细等子状态 | 恢复只切顶层模式，不重设 Pen/Shape 子类型，不无条件调用 ChangeStateModeToPen |
| `Inkeys/IdtState.cpp:1067`、`:1211` | ApplySelectionTransitionLocked 与 ChangeStateModeToSelection 不发 Clear，设置 Target/Select/Echo、背景并推进 revision | 可提炼为合并式退出/双 Delete 第二步的无清空 Selection 内核 |
| `Inkeys/IdtState.cpp:1103`、`:1138` | 按钮 Selection 在分离式、有内容、非白板时先 Publish Clear，Accepted 后核模式 revision 再 Selection | 分离式全局退出语义类似；但目前仅验证 Accepted，没有真实成功与精确页完成结果，不能原样满足新事务 |
| `Inkeys/IdtState.cpp:1148` | ClickThrough 按钮只能分离式；恢复 lastAnnotationMode；退出也保持 Freeze 关闭 | 可复用恢复内核，不可用此公开函数实现全局往返，因为它会进入 ClickThrough 且合并式拒绝 |
| `Inkeys/IdtState.cpp:1187` | ToggleFreezeFromButton 在可用场景切 Freeze，ClickThrough 时会恢复工具 | 本期 Q 仅批注场景准入；Button 函数业务可复用，但应有 expected-mode/route 条件边界，避免旧 Q 将新 ClickThrough 拉回 |
| `Inkeys/IdtState.cpp:1226` | ChangeStateModeToSelectionIfRevision 写 draw3ExitSelectionModeRevision，专用于真实 PPT 退出交接 | **不要直接把这个入口当普通快捷键条件 Selection**，需另建普通用户命令入口，避免假造退出版本 |
| `Inkeys/Inkeys/UI/Freeze/Freeze.cppm:61`、`:109`、`:148` | Freeze 原子字含版本、active、presentation/whiteboard；PPT/白板不可用；DeactivateIfRevision 是版本条件停用 | 全局往返退出应关闭 Freeze，恢复保持关闭；不能只切 Selection 让旧定格盖住下层；不得恢复进入前 Freeze |

建议在 `IdtState.h/.cpp` 增加窄入口（名称为设计候选）：`ToggleAnnotationFromShortcut(expectedIntent)`、`EnterSelectionFromShortcut(expectedModeRevision, expectedRequest)`，在 stateModeTransitionMutex 下决定方向、检查意图、写状态；锁外 SyncDraw3State 和刷新 Bar。分离式需要清空的退出以有身份的 Clear 请求开始，在精确成功回执后进入普通 Selection；没有内容走有身份的 NoContent 成功，不等待磁盘。合并式直接无清空 Selection。白板未发布能力不因快捷键开放。

Freeze 的异步/并发收尾必须持本次版本条件，不能迟到退出关闭用户新开启的定格。若引入 conditional toggle，增加最小 `ToggleIfRevision` 可选扩展；现有 Toggle 自身已再次验证 availability，不能只靠 Hook 捕获时可用。

## 2. Draw3 已有命令链保证了什么

- `Draw3.Product.cpp:181`：PublishProductCommand 取得 productCallMutex，验证存活，转 Host；只传 CommandType。
- `Draw3.Host.cpp:2246`：PublishCommand 取得 commandPublishMutex，先保留 Command wake，再 bridge.Publish，失败撤销保留；不能绕过这层直接写 bridge 导致命令无对应唤醒。
- `Draw3.Bridge.cpp:129`：Publish 在 mutex 下将当时 workspace 与共享不可变 presentationTarget 复制进命令；CommandResult::Accepted 仅代表进入有限 FIFO，不代表渲染执行成功。
- `Draw3.Host.cpp:1340`、`:1376`：每条命令先 EnqueueCommandScene(captured scene)，再 Clear/Undo/Redo CanvasCommand；完成后再收敛 latest state。既有鼠标按钮等无条件命令有意保留这一 captured-scene FIFO 合同。
- `Draw3.DrawingController.cpp:11852`：实际命令 while(active.empty()) 才排队执行，活动笔迹会使业务晚于 Hook/Bar 消费；仅在 Bar 核一次快照不能保护执行时的场景。
- `Draw3.DrawingController.cpp:12286`：PPT load pending/unresolved 会丢弃画布命令，并调用 reportCommand。
- `Draw3.DrawingController.cpp:12315`：Clear 先捕获 CPU tail，有内容而捕获失败直接终止；沿唯一 Clear 事务建立恢复点、区间边界与异步保存。
- `Draw3.DrawingController.cpp:11755`：无 document、无内容或不可执行均返回 false；没有内容应在新回执分类中单独识别，不与真正失败混为一谈。空 Clear 不追加边界、不破坏 Undo、不保存磁盘。
- `Draw3.DrawingController.cpp:12378`、`:12457`：Undo/Redo 真实分支应继续复用；当前最近 Clear 恢复与 Redo Clear 已接通，不增加第二套历史。

关键风险：`old event -> Snapshot validation -> PublishProductCommand(type)` 存在 TOCTOU；普通 Publish 会把旧事件认领为**发布那一刻**的新页面，而不是事件捕获时的页面。单纯给 Bar/Hook 增加 epoch、后面仍走该无条件 API 不够。

另一个风险：既有 captured-scene 重放会主动切回旧场景。新快捷键“已失效的延迟事件”不能借这一步复活；不能只在 Host 检查一次后，就在 Controller 无条件执行。

## 3. 最小的有身份命令扩展建议

保留原普通按钮 API 与 FIFO 行为；新增仅快捷键使用的受条件限制的发布重载，并把条件一路传到实际 Controller 消费点。

候选值类型 `ShortcutCommandContext` 包含：Host generation、快捷键路由/意图 generation、稳定逻辑页 scope generation、workspace、pageGuid/pageIndex、PPT 完整 ReadyIdentity（key/binding/page/SlideID/target/session revision）。命令额外带 requestId；不得只匹配页码或 hasContent，也不得把 contentRevision 当页面身份。

必须区分三类版本：

1. **页面/工作区作用域**：页/文稿/工作区切换与 ABA、Host 重启退休旧命令；普通 Clear/Undo/Redo 不应退休同页后续已排队命令。
2. **自动收起起笔 scene**：现有 acceptedStrokeSceneGeneration 在 `Draw3.DrawingController.cpp:10952` 的 resetGpuForPageSwitch 中递增，**Clear 与跨 Clear 恢复也推进它**（`:11815`）。它可辅助回执验证，但不能直接作为所有编辑键的唯一固定条件，否则 Ctrl+Delete 后快速 Ctrl+Z 会因 Clear 自己推进 scene 而被错误丢弃。
3. **本次清空后的后置修订**：回执带 source context、result context、content/history revision，双 Delete 或分离式全局退出只接受具名自身 Clear 的后继状态；新的笔画/Undo/第二次独立清空等不得被当成该 Clear 后置空态。

建议复用现有 Host logical-scene 发布路径（acceptedStrokeSceneChanged 提供触发位置），添加一个只随逻辑页归属变化的窄 command-scope stamp；不扩大 runtime metrics 身份，不强迫所有 UI 命令改用新合同。PageGuid/工作区必须从实际 Controller 的同一临界点获得；新增 Host 快照应一次性发布一致的完整载荷。

条件发布须在与 command enqueue 同一同步边界验证 expected context/当前 desired scene，不允许“先核后再无条件 Publish”。Controller 在实际命令分支前再核有效授权与真实页面身份；失效返回 Stale，不切回旧页面、不执行 Clear/Undo/Redo。若保留 captured scene 准备动作，也必须先证明该 scene 仍是当前允许的目标，实际 CPU load/switch 失败不能落到别的画布执行。

新上下文不应因终键 Up 或 Ctrl Up 立刻退休普通已接纳命令：键很快松开而 Draw3 仍在执行是正常情形。**Ctrl 会话结束只取消“双 Delete 第二步候选”，不撤销第一次已接纳的单 Clear**。动作授权与两击识别候选应分开保存。

## 4. 真实回执与复合操作

现有 `DrawingControllerRuntimeObserver::commandProcessed` 只含 type/page/pageCount，`Draw3.DrawingController.cpp:11852` 的 reportCommand 在成功、失败、空内容、load 阻止都被调用；Host 的 clearCommandCount 增加不能证明该 Clear 真正清空，更不能证明是这个请求。

建议增加单独窄回执 `ShortcutCommandCompletion { requestId, sourceContext, resultingContext, result }`，结果至少：Applied、NoContent、Stale、Blocked、Failed（需要时 NotRunning）。空页第一击在 Controller 核真实页且存在有效 document/canvas 后给 NoContent；snapshot/GPU/区间异常不能伪装 NoContent。回执必须有限队列或固定受管 ticket slot，并在接纳时确保有回执落点；禁止 Hook 等待和 detached waiter。关闭时退休 ticket，不调用已销毁 Bar。磁盘 durable completion 不是此成功条件，沿现有 Clear CPU 事务结果即可。

### Ctrl+Delete 连按

- 第一独立 Down 立即请求 Clear 并保留工具，不因画布本来空就跳 Selection；自动重复只吞同一终键重复，不生成第二请求。
- Delete Up 后、同一连续 Ctrl 会话第二次 Down 标记 pendingSelection；若首 Clear 仍未完成，等待其精确回执，不能先改模式。
- Applied/NoContent 且上下文/模式/配置/焦点意图仍有效，才执行普通 Selection 条件入口；两种样式相同，不调用分离式会再清空的按钮入口。
- Ctrl 全部松开发生在第二击之前只取消未形成的候选；两个按压已捕获后松 Ctrl 不撤销等待具名回执的第二步，第一次已接纳单 Clear 同样不受松键影响。其它终键/命令、外部模式/页/工作区/配置/焦点改变按总设计退休相应意图。识别上下文失效后新一轮第一击重新开始。
- 若第一 Clear 未 Accepted 或完成 Failed/Blocked/Stale，不授权第二步 Selection；可以把第二次 Delete 当新单 Clear 重试，设置说明只描述正常两步，日志/测试明确失败行为。
- 自己的 Applied Clear 导致起笔 scene 改变只能以同 requestId 回执明确迁移候选；不能全局放宽任意 scene 不匹配。第一次单 Clear 不长期持新输入租约；其后新笔迹/Undo 等内容变化退休候选，第二步形成后才加短期复合屏障并核具名后置内容修订。

### 分离式全局退出

- 一次 X 产生明确“清空后进入 Selection”意图，而不是复用 Delete 的两击识别状态。
- 接纳/执行首 Clear 失败时保持原工具；成功或真实空页 NoContent 后进入 Selection；关闭定格按同一次意图条件完成。
- 总设计统一：连续两个全局 X 在有界受管序列中先完成退出再返回，前一个自身的模式变化由具名结果衔接，第二个 X 不取消第一个或再按旧快照清空；首个失败则退休依赖切换。外部新工具、更晚的其它用户命令、配置/页切换退休旧 pendingSelection；迟到回执不得覆盖新工具意图。
- 不能靠全局 shared pending bool：旧回执不能清除新请求 slot。使用 requestId + generation 比较退休。
- 这两种复合操作共用 Clear 业务和有身份回执机制，复用状态内核；不需要新增“清空再选择”历史操作或持久化格式。

## 5. 设置与 Inkeys3 独立配置

当前快捷键设置是只读说明文本：`Setting.cpp:7660` 开始 tab5；`:7686` 读取 HotKey.E 并手工换行。`Inkeys/src/i18n/zh-CN.jsonc:617` 的旧说明仍写 Ctrl+Win+Alt、Ctrl+E、Ctrl+Z/超级恢复，必须更新，不能保留不存在的旧能力宣称。

配置建议独立 `Input.Shortcuts`（最终命名由设计统一）或等价新 Inkeys3 schema group：六个 `IdtAtomic<bool>`、`ConfigUploadMode::NoUpload`，ToggleAnnotation、Undo、RedoY、RedoShiftZ、Clear、Freeze。六项默认 true 为方案建议，待整份规划批准；用户已明确 Ctrl+Alt+X 启用。RedoY 与 RedoShiftZ 分别独立禁用，一个禁用不影响另一组；没有自定义键字符串或旧 Inkeys2 迁移。

总设计已明确只有六个开关：Ctrl+Delete 同时管理单次清空和两击进入选择，不加第七个开关。

现有 `Other.Config.cpp:165` bool codec 只接受 JSON bool；IdtAtomic codec 复用底层 bool；默认由 schema Reset/ReadAll 路径建立。通常无需专门更改 JSON codec；测试缺失/非法类型退默认、ReadAll/ReadMini/Write/冻结副本即可。不得把新 bool 绑定 setlist 历史字段。

设置使用现有 toggle 卡片样式，每帧读原子值，仅变化时更新一次值并 QueueConfigWrite（`Setting.cpp:443` 已提供冻结副本 FIFO）；发布新启用快照/generation，不在持 ImGui/render 锁时同步安装/卸载 Hook 或等 HWND。逐项禁用立即退休该键旧候选；重新启用不复活旧请求。

说明需三语同步，明确：全局键两样式不同清空规则；普通五组合仅批注有效，Selection/ClickThrough 交还下层；空画布仍属批注；Ctrl+Delete 两步与长按不重复；PPT 原生翻页键不被接管；PPT/白板定格不可用；暂不自定义。无需承诺未做的 Ctrl 数字提示/工具快捷键/应用切换自动暂停。按既有 `Scripts/i18n.ps1 sync/check` 生成 IdtI18nKeys.g.h 与 snapshot，禁止手改生成 header。

## 6. 实施文件最小范围与验证要求

业务必要：IdtState.h/.cpp；Draw3.Bridge.h/.cpp；Draw3.Product.h/.cpp 或当前公开产品头；Draw3.Host.h/.cpp；Draw3.WindowControl.cppm；Draw3.DrawingController.cpp（及 observer 声明所在 cppm）；Shortcut 新业务/输入文件与项目注册；Other.Config.cppm；Setting.cpp；三语与生成资源；headless/初始化前真实状态生产探针。

依赖文件在实现前按实际编译归属确认，不能误把不存在的 Draw3.Bridge.cppm 或 DrawingController.h 当修改目标。

验证重点：

- 两样式×有墨迹/空页×Pen/Shape/Eraser/Laser×Selection/既有ClickThrough，X 往返和最近工具宽色保存；分离式退出 Clear 拒绝、Blocked、NoContent。
- Delete Down 自动重复不算两击；两次真实 Down/Up、Ctrl aggregate 全松、另一终键、切页/模式/配置/焦点失效；首Clear pending时第二击，成功/失败/空页精确关联。
- 普通 Ctrl+Delete→Ctrl+Z→Redo FIFO 仍有效；不能因首Clear scene递增误拒后续同页编辑。
- 强制队列延迟、页面 A→B→A、Host 重启、PPT 同页号不同key/binding、load阻止、旧回执不能进入/覆盖新工具、连续 X 序列与外部新意图的退休边界。
- 测试必须打到生产条件发布与 Controller 命令准入/回执路径，不能只有 Hook matcher/mock callback。受仓库限制默认不启动任何 HWND/GUI；可用共享生产策略头及初始化前 probe 验证条件状态，真实呈现留人工。
- Config roundtrip 与6组合独立开关、三语检查；实现批准后完整 Solution Debug ARM64 + headless --no-window 与真实 IdtState 生产探针。Win7 不新增高版本静态导入；本次规划未运行平台或 GUI 验收。

参考已读取：native-desktop/index.md、cpp-conventions.md、build-and-compatibility.md、configuration-i18n-and-assets.md 的配置/生成链/StateMode 合同、draw3-integration.md 的唯一owner/FIFO/保存/Clear/Selection 合同、mouse-hook-and-autocollapse.md 的旧键盘路径与回调性能边界。
