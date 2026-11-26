在 `refactoring/draw` 工作区按 PRD 第三轮决定完成只读审查。未修改文件，未运行构建或测试。下文 `DC` 指 `Draw3.DrawingController.cpp`，`spec` 指任务内的 `research/uink-v11-spec.md`，其余 Draw3 源文件省略目录。

## P1 · 实施前修正

1. **S1–S3 无法按现有顺序逐步通过门禁。**【码】S1 删除 `eraserShape`，S3 才迁移仍读写它的适配层。S2 删除 `uink_page_file`、`UInkSourceRevision` 和保存 API，S4–S6 才迁移直接调用方。S4 提前切根后，旧版索引和目录会进入正式新根，计划已明确承认这一点。建议 S1/S2 先新增接口并保留过渡调用，随各场景迁移删除旧接口，最后一次性切换产品根目录。中间测试统一注入临时根。同步修订回滚说明。【证据】`implement.md:34–63,99–100`、`uink_draw3_export.cpp:180`、`Draw3.WhiteboardStore.cpp:22,104,115`、`Draw3.PresentationAutoSave.cpp:623`、`Draw3.AutoSave.cpp:774`。

2. **白板冷读需要定义可执行的撤回状态。**【码】现有冷读恢复普通 redo、Clear redo continuation、图形 `Replace` 历史和 `undoFloor/operationFloor`。白板 Clear 目前保留内存恢复点，PPT 才有磁盘 `PreviousInterval` 分支。【推】只把最后可见笔画导入，或沿用会重编 `undoId` 的导入器，都会改变撤回范围。建议 §5.2 明写白板冷却后丢弃已有 redo 和图形修改前版本，热页保留现有历史。按连续 `undoId` 装回撤回组，区分普通导入根与可逐组撤空的 Clear 恢复，并在本次 Undo Clear 后重新建立内存 redo。验收覆盖 `A → Clear → B → 冷读 → 撤空 B → 恢复 A → 撤空 A`，以及二次 Clear、撤回后离页保存。【证据】`prd.md:50–54`、`design.md:136–143`、`DC:1004–1041,14662–14681,22576,23275–23317,24224–24276`、`uink_draw3_import.cpp:238–259`。

3. **PPT 单文件缺少绑定切换和冷恢复规则。**【码】运行时槽以 `(PresentationKey, bindingMode)` 隔离。普通翻页复用驻留页，停放槽满足持久化条件且无 redo、Clear continuation 或图形编辑历史时可驱逐。SlideID 重排会投影活动页、保留页和独立结束页。【推】§5.2 同时写“仍常驻”“保留 lane”，无法指导同文稿从页码回退切到 SlideID 时如何读写同一个文件。建议定义文稿级 writer、模式隔离和完成路由。按 GUID 更新受重排影响的页，缺失 SlideID 保留为未绑定，回退页按明确页序配对，结束页单列且无 SlideID。写明保留页再出现、页数变化和停放脏页退出重试的处理与测试。【证据】`design.md:127–143`、`DC:1192–1217,2208–2289,22419–22433,23844–23915`、`spec:1182–1188`。

4. **追加失败和驱逐条件未形成闭环。**【码】现有白板只在已保存 revision 与当前 revision 一致、页面既非当前也非加载目标时驱逐，桌面只在收到匹配的成功回执后释放 Clear 前快照。【推】短写后继续追加可能使后续版本无法解析，加载优先也可能越过尚未持久化的依赖。建议 §4–5 区分已排队、已 Flush、已发布目录三种状态，按页 GUID、revision、代次和 Clear 区间匹配结果。依赖未落盘时用内存数据或等待对应写入，禁止合并掉 Clear。短写或 Flush 失败须回滚至旧末尾，回滚失败就封闭 writer 并保留脏页。注明整页写入如何取得已释放的上一区间，以及 128 MiB 读限额如何与持续追加相容。【证据】`design.md:93–105,129–140,199,228`、`DC:18618–18625,23519–23543,23577–23626`、`spec:592–595`。

5. **退出屏障和读取异常需要覆盖 worker 全链路。**【码】Host 的退出等待没有超时。白板在绘制线程逐页 `Drain`，桌面和 PPT 在回执后由 Host `CloseAndDrain`。桌面退出还会保存停放的 Desktop，配置开关只门控桌面。白板完成队列的 `push_back` 和唤醒在任务 `try/catch` 外。【推】L1–L3 捕异常仍可能因完成通知失败而挂起退出，或因异常跨 `noexcept` 终止。建议 §3.3、§5.4 写明准入、执行、完成投递和关闭各自的失败出口。失败也要清 pending、唤醒等待者并保留数据。覆盖活动及停放页、正常和图形故障退出。按真实阶段描述等待，并明确阻塞 I/O 的处理。【证据】`Draw3.Host.cpp:1918,2850–2894`、`DC:1937–1966,22360–22438`、`Draw3.WhiteboardStore.cpp:98–142`、`Draw3.AutoSave.cpp:1130–1145`。

6. **“分配前检查”需要落实到可测试的预算。**【码】现有扫描会在 `unpack` 前限制容器与深度，瓦片足迹可遍历整个候选范围，Ink 位移目前用 `float` 累加。【推】删预扫后仅限制点数组 `reserve`，仍会遗漏 MsgPack 临时区、未知块、注册表循环及有限大坐标的派生工作量。建议 S1 先保留有界预检，替换时证明等价。明确范围与文件长度检查、字节和分配预算、每页累计几何/瓦片访问上限、受限的注册表遍历，以及 `float64` 累加后再检查缩窄。分别测可定位坏块后继续、边界不可定位时保留前缀、无有效页时失败。【证据】`design.md:63–84`、`uink_codec.cpp:197–216,1447–1468,2577–2624`、`Draw3.InkHistory.cpp:234–245,320–348`、`spec:1348`。

## P2 · 建议补齐

- **写出契约落实到字节夹具。**【码】§3.1 已写 `version=11`、编号和圆帽。显式白板 Workspace 仍需明确每个 Canvas 引用同一 `workspaceGuid`。隐式 Device 省略 GUID，Header 对应计数写 `1`。§4 的 `Create` 直接写最终文件，未落实 SAVE-1 的完整保存发布顺序。建议用同目录临时文件一次写 Header、HEXT 和首 Canvas，检查编码与写入结果、Flush 后无覆盖发布。字节测试断言 Header `str8`、`type:uint16`、编号 `uint32`、枚举 `int32`、几何 `float32`，覆盖多部件共组、点线 Round 和 Clear 前后独占组。【证据】`design.md:36–59,92`、`spec:669–672,712,862,937–951,1668,2033,2209,2270`。
- **视角和外部回退。**【码】Draw3 支持 `scale=1` 的非零平移，白板冷读从会话侧表恢复。§3.1 写“原点 0”，§7 固定取首 Device，均遗漏现有行为或规范回退。建议保留合法平移和白板侧表优先级，明确外部缩放回退。首 Device 没有该页时选第一个有页的 Device，再按层叠加。区分可恢复的字段错误、重复已知键和坏 Canvas 的作用域。【证据】`design.md:46,67–75,186–190`、`DC:18267–18274,18589–18592,24337`、`spec:559,1218–1219,2279`。
- **图形显示与编辑分开定义。**【码】规范要求显示文件中的最终部件。未知预设、无效 `construction` 仍可保留合法对象组，缺部件或坏引用才解组。旧矩形按部件显示，§6 按当前 DPI 重建可能改变圆角。建议明确保留已写几何，首次编辑才重建。列出 `strokeColor/fillColor`、局部 `width`、`transform[6]`、`aux` 和按预设必填项的精确映射。对 21 个官方名与 4 个 `legacy-*` 逐一断言名称、参数、部件顺序和编辑结果。【证据】`design.md:159–172`、`uink_draw3_export.cpp:214–223`、`spec:1646–1672,2307–2358,2441–2450`。
- **补齐工程和测试入口。**【码】`uink_file` 还编入模型器应用工程，产品 solution 不含模型器测试。建议明确两个产品/测试项目的 `.vcxproj` 与 `.filters`，以及模型器应用对共用模块的迁移，单列 `inkStrokeModelerTest.sln` 门禁。逐步列出 `Draw3.Host` 启停与完成投递、`HiddenWindowTest` 的旧保存 API 与索引断言、`FailedCleanupFixture` 的 revision/SHA 收据、`contact_input_tests` 的页文件入口、`IdtMain` 的白板及停放/绑定自检，明确各步随哪次 API 变更迁移。【证据】`implement.md:9–11,46,62–87`、`Inkeys.vcxproj:1124–1146`、`inkStrokeModelerTest.vcxproj:274–284`、`inkStrokeModelerTestTests.vcxproj:112–167`、`Draw3.Host.cpp:2288–2294,2455–2467`、`Draw3.HiddenWindowTest.cpp:525,956–985`、`Draw3.FailedCleanupFixture.cpp:403,986`、`contact_input_tests.cpp:3610–3615`、`IdtMain.cpp:2107–2134`。

## P3 · 备注

- 【码】`ScanPageDirectory` 只扫边界，无法判断 SAVE-22 的尾部无效块，也无法区分 SAVE-24 的中间语义损坏。建议本期只定义安全位置所需信息，扫描与恢复留到 C1.6。`Compact` 的 50%／8 MiB 也缺实测依据，保留空间预算和整理触发条件，按实测定值。`ReadPage` 应明确接收已解析的注册表上下文，整读按完整 Canvas 键去重。【证据】`design.md:94–107`、`prd.md:54–58`、`spec:695–700,730–732,2652`。

## 仅需产品负责人确认

- 【码】荧光笔可设置小于 1 px 的原始宽度，渲染有 1 px 高度下限。`max(1,w)` 写出后，官方字段只有一个 `width`，无法反推出原值。这个边界的验收以规范读取器画出同样的有效高度为准，还是要求原始宽度原值往返？【证据】`prd.md:43,89`、`design.md:53`、`IdtState.cpp:1989–2000`、`PenWidthPolicy.hpp:35–38,75–88`、`Draw3.HighlighterNib.h:12–15`、`spec:2163–2168`。