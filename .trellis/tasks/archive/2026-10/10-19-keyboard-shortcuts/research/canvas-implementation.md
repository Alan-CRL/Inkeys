# Draw3 快捷键条件命令实施记录（2026-10-19）

## 已实现
- 新 Draw3.Shortcut.h：固定64个受管requestId回执槽；接纳前预约，读取/退休释放，重复或旧回执不覆盖，Host重启不复用generation和requestId。共享生产State::Authorize、ResolveFence供无窗口测试。
- Bridge ProductState.shortcutIntentEpoch只随workspace/page/完整PPT target意图推进；普通工具、Clear内容修订不推进。条件发布锁内再核epoch/workspace/target；A→B→A即使绘制合并B也拒旧包。普通按钮Publish及captured-scene重放保留。
- Controller独立pageEpoch根据真实workspace、pageGuid/pageIndex和完整PPT target推进；新逻辑contentRevision在每次真实内容发布调用推进，覆盖同布尔值下第二笔/Undo，而不混用acceptedStrokeSceneGeneration或呈现bool revision。
- 快捷键命令一路携带共享Request至真实CanvasCommand；Host不为此重放旧场景，实际Controller在active.empty安全点用当前页和最新Bridge意图重新准入。所有提前退出、空页、阻止和Clear失败都有精确具名结果；Clear真实返回true才Applied，存在有效Canvas而空页才NoContent。
- 新ShortcutFence仅在快捷键条件入口可发布，普通Publish拒绝。第二Delete获得租约后等真实Controller栅栏，检查具名Clear后置contentRevision和空态；不再次Clear、不改变历史恢复点。租约前已入队/活动接触如果收尾产生新墨迹，会使Fence过期，保留批注状态。
- 根agent批准最小扩展ContactInput两文件：独立shortcutAdmissionBlocked，与原base门禁合成；低频setter用专用mutex串行发布，RTS/contact producer仍只读原子AdmissionRevision，不增加其锁/分配。释放租约不调用SetAdmissionBlocked(false)，保留PPT/load/fatal基础关门。
- Selection发布与绘制线程应用之间有短期handoff；lease退休后shortcut门位延续至WindowController已应用最新Selection，避免Clear与真实选择应用间插入新笔迹。

## 对业务接口
- CaptureProductShortcutContext；PublishProductShortcutCommand(type,scope,leaseId=0)；TryTakeProductShortcutCompletion(id,out)；RetireProductShortcutRequest(id)。
- AcquireProductShortcutLease(context,requireContentRevision=false)；ReleaseProductShortcutLease(id)。
- PublishProductShortcutFence(expectedPostClearContext,leaseId)：第二Delete必须等NoContent回执再Selection；不把Acquire返回直接当作已收尾证明。
- Context.available在Selection handoff期间短暂false；依赖前一次往返的后续X应留队等待，不按旧方向重复Clear。

## 验证
- 现有draw3_bridge_tests新增真实Bridge条件入队、同页Clear/Undo/Redo FIFO、独立内容修订、实际准入策略page ABA/Host重启/PPT重开、64槽饱和/退休/重复结果、Fence具名空态和新墨迹过期测试。
- 现有draw3_contact_tests新增真实ContactInput base/shortcut门禁组合、受阻Down保留route但ContactAdmitted=false、独立门的释放不重开其它门测试。
- 静态git diff --ignore-submodules=all --check通过；保持既有UTF8 BOM/CRLF。未在子agent启动任何构建、GUI、Hook、设备、commit或push。
- 完整Debug ARM64 solution与--no-window由根agent统一执行；上述共享策略/生产ContactInput测试不等同真实HWND/Office/Win7/UIPI验收。

## 文件
Draw3.Shortcut.h；Draw3.Bridge.h/.cpp；Draw3.Host.h/.cpp；Draw3.Product.cpp；Draw3.WindowControl.cppm；Draw3.DrawingController.cppm/.cpp；Draw3.ContactInput.cppm/.cpp；InkeysHeadlessTests/draw3_bridge_tests.cpp、draw3_contact_tests.cpp。

## 首次编译修正
- Draw3.DrawingController.cpp 的 global module fragment 显式 include Draw3.Shortcut.h。接口 cppm 的全局片段可通过 observer 签名使类型可达，但不会自动公开未被接口引用的 ResolveFence 自由函数；因此实现单元必须直接 include 声明。此次 C2039/C3861 属源码模块可达性问题，不做缓存清理或工程升级。
