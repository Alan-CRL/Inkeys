# P3＋P4 历史、持久化与命令桥独立审查

审查日期：2026-10-17。审查角色：`trellis-check`（p34_history_check）。基线：当前未提交工作区；本审查未参与原历史／桥接实现。审查范围为 InkCanvas 同层替换、统一操作 journal、Undo／Redo／Clear、白板冷历史 v3、桌面／PPT 保存，以及 Product／Bridge／Host 的图形命令与快照生命周期。

已读取完整 hook 保存文件，并补读任务 PRD／design／implement／check.jsonl、shapes、whiteboard 和 rendering-and-ui 末段。Controller／ShapeRuntime 由 core owner 独占，本审查没有并行写业务文件；确证问题交给 owner 修复，随后只读复核。

## Findings（已交 owner 修复并静态复核）

### 1. 同场景前导命令提前关闭当前编辑／构造（P0）

- 文件：`Draw3.Host.cpp` 的 `PumpOneBridgeCommand`／`EnqueueCommandScene`／`PumpBridgeState`；`Draw3.DrawingController.cpp` 的 `processCanvasCommands`；`Draw3.ShapeRuntimeActions.inl` 的 `drainActiveShapeControlBarrier`。
- 证据：Host 在每个 scene-stamped 命令前投递 SetWorkspace 或 SetPresentationTarget，并在 `restoreLatestScene` 收敛时再次声明最新场景。原 Controller 对两种命令无条件关闭会话、推进 documentGeneration，早于各自已有的等值 no-op 判断。
- 复现：对象已自动选中后发布 BeginEdit 或 SetStyle，实际动作前的同场景声明先清空 editor；构造中按 Undo，同场景前导项先取消 construction，因此无法进入步骤 Back。直接调用编辑工厂／action handler 的测试绕过 Host 前导队列，不能暴露此问题。
- owner 修复：增加共享 `ShapeCommandChangesScene`，同时供正常命令关闭门禁与活动接触取消屏障使用。同一场景重复声明保留会话／身份，真实场景变化仍取消；保留 SetPresentationTarget 原等值分支中的加载重试逻辑。
- 新增验证点：生产 WindowController 队列按 Host 的 scene→action 顺序投递，覆盖 Desktop／Whiteboard／Presentation 的 BeginEdit 与 Undo→Back；真实页／workspace／target 变化仍拒绝旧身份。
- 复核补充：同 workspace 的 `SetWorkspace(Presentation)` 在当前绑定文稿时会实际切换到 isolated slot；已另交 owner 将该边界纳入真实场景变化判断，避免活动构造在目标清空后才 Up 提交。

### 2. 待重做编辑被 Redo Clear 跳过（P1）

- 文件：`Draw3.DrawingController.cpp` 中 `redoClear` 判定；`Draw3.ShapeRuntime.inl` 中 `CanRedoClearAfterOperations`。
- 复现：创建图形→编辑→Clear→Undo Clear→Undo 编辑。此时只有 Replace 待重做，`history.LastRedoItem()` 为空，`clearRedoAvailable` 仍为真。原判定直接重做 Clear，跳过编辑重做。
- owner 修复：Redo Clear 只有在追加 redo 栈和统一 journal 前向操作均耗尽后才可执行；以 `operationCursor >= operations.size()` 补齐门禁。
- 新增验证点：真实 Replace Undo 后无 Append redo 时先重做编辑，journal 到尾端后才允许重做 Clear。保留原普通追加／Clear continuation 行为。

### 3. 冷历史接受重复对象身份或重复 RenderItem 映射（P1）

- 文件：`Draw3.DrawingController.cpp` 中 `RestoreWhiteboardHistoryUnchecked` 与 `RunShapeEditHistoryProductionTest`。
- 复现：两个合法图形的冷历史，将第二个实际 stored stroke 的 Shape ID 改为第一个，保留不同几何和相同 revision；原逐对象解码与 journal 正反演算均可通过，renderer 的 ID／revision 缓存可能别名。将两个 items 的 strokeIndex 改成相同值，也可通过原索引范围检查。
- owner 修复：实际 stored strokes 的 Shape ID 去重；items 数量必须等于 strokes 数量；strokeIndex 唯一且在范围内。operation before／after 是同对象的语义快照，允许重复对象 ID，不参与文档级去重。
- 新增验证点：重复对象 ID、重复 strokeIndex、缺少 RenderItem 三个损坏输入均拒绝，并保留原 page 与 runtime；合法前后快照、隐藏 redo 和 Clear continuation 仍恢复。

## Findings（未修／需收尾确认）

### 4. 有界队列拒绝终结动作的处理（已报告主会话，待决策／修复）

- 文件：`Draw3.Bridge.h/.cpp`、`Draw3.Host.cpp`、`Bar.ShapeEditor.cpp` 的 Emit，以及 SelectionOverlay 的 Esc／Bar 构造提示动作发布。
- 证据：StateBridge 默认容量 256；Host 为每个 Preview 独立预留 command wake。UI 发布侧忽略 `PublishShapeUiAction` 的 QueueFull 等结果。若 Begin 已接受而 Commit／Cancel 被拒，UI 已结束本地手势，绘制线程仍可能保留 gestureActive／preview。
- 建议：预览允许合并或丢弃，但终结动作应沿既有有界输入续接重试或采用明确的失败取消策略，不能静默丢失。用小容量生产 Bridge／input fixture 确定性验证 Begin 被接受、终结被拒时不会永久悬挂；不得以无界队列替代。
- 本审查未改 UI／公共接口，因为 UI owner 正在维护该文件，且失败策略属于跨层设计决定。主会话已收到精确证据。

## 已核实的生产一致性

- InkCanvas 的 ReplaceStroke／ReplaceShape 验证同一稳定 ID、合法候选和原索引；最终项不追加新层位。
- CanvasPageRuntimeState 的 journal 顺序与 RenderItem 层序分离；普通追加与编辑共用 cursor，编辑前后快照保留同一对象语义，新分支截断 redo 并取消 Clear 前向分支。
- 编辑 Undo／Redo 先在候选 canvas／runtime 应用语义，再重放旧新 tiles；成功后安装，失败请求恢复原画面。运行期新修订在 GPU 失败时仍消耗，避免缓存复用。
- 编辑及其 Undo／Redo 丢弃创建时 hot preimage 和 composition 派生缓存；权威页面安装通过统一 reset 清理 Shape renderer 缓存并关闭旧会话。
- Clear 前完整编辑历史保留一层，Clear continuation 从逆向撤编辑、隐藏追加恢复为空态，冷历史序列化 operation cursor／floor／隐藏 redo／边界；PPT 有编辑 journal 的 warm 页不被仅可见内容的保存快照淘汰。
- 冷恢复在候选 page／runtime 内完整演算，失败不覆盖当前页；Shape 参数元数据复用共享 codec。桌面／PPT 可见快照保存最终对象参数，不扩大为跨进程恢复完整编辑栈。
- Product 通过 atomic shared_ptr 发布不可变 UI 快照，刷新回调只唤醒既有 UI 帧；清除回调在 mutex 内等待在途回调结束。当前 callback 使用空 context，无悬空 owner 指针。StopProduct 先阻止新调用、drain 调用闸门，再停止 Host／清空快照；未发现当前生命周期阻断。
- 编辑手势以 Down identity 验证 Preview／Commit／Cancel，普通属性／构造步骤动作要求当前发布身份；场景身份包含 document／display／page／workspace，UI 不直接写文档。

## Verification

- 静态审查：已完成实际生产路径及新增负例源码复核；后续 owner 修复仍须以主会话统一构建／测试结果闭环。
- Lint：限定 InkDocument／InkHistory／Bridge／Host／Product 的 git diff --check 退出码 0；各文件 BOM 保持，全部 BareLF=0（CRLF）。未新增格式化工具或构建系统。
- TypeCheck／构建：本代理未执行，遵守主会话统一 ARM64 MSBuild、避免与 owner 写文件竞态的分工。
- Tests：本代理未启动产品 EXE／Headless 或任何 HWND；新增生产测试交主会话统一执行，不把源码存在记为测试通过。
- 规范同步：`shapes.md` 的 P1／P2 历史版本、工具数及未接编辑表述仍需本轮收尾追加 P3＋限定 P4 合同，并同步 cold v3／scene no-op／Redo Clear 门禁。
- 无 GUI、Computer Use、commit、push。Win7／Win10 真机、实体笔和触屏仍属人工待验。


## 主会话闭环（2026-10-18）

上述审查时的待项现已由各owner与主会话修复。界面终结意图采用最多5项、同场景按种类合并的surface tick续接；绑定PPT→isolated门禁、Clear前Replace redo、冷页重复身份/映射负例均纳入生产测试。shapes.md已补当前合同和防回归。最终完整ARM64构建退出0；生产core/Interaction/EditHistory/376 GPU cases/100页storage/380 Editor场景/3024 Bar配置以及Headless、UInk、i18n均通过。实际命令与退出码见 `../validation.md`，本段由执行测试的主会话补记；不把原审查代理的只读工作冒称为其运行测试。
