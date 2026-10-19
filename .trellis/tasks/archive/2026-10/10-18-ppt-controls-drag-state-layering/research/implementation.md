# PageControl 1–3 项生产实施记录

2026-10-18。用户已批准开始修复 1–3；唯一生产写入责任为 PageControl.cpp/.cppm，测试代理另写 page_control_tests.cpp。第 4 项不新增层级修复。以下为实现记录，构建和实际执行结果由主会话统一填写 verification；本实施代理未运行构建或产品测试。

## 生产修改

- 以 D→S 同一短事务采 RenderSnapshot 的两个 pair 拖动所有权，按 session/epoch/pair version/当前 pair 位置及倍率核对；包括 released pending。Configure 仅对同工作区、同尺寸/实际倍率、已可见的纯拖动位置立即落定，普通出现/消失、倍率、DPI 和工作区转场保留。RetargetBounds 在相同目标但要求不动画时也必须结束已有插值，不能早退继续余移。
- render 在 T 内保留实际 Scene logical RECT、实际 scale 及加当前 outset 的 presentation RECT 达到该帧已拟合目标的证明，成功 Present 和 Window Service 提交之后才允许 CommitDragSurface 推进该候选的 surface mask。目标未到不提前 handoff，继续有界动画帧。原直移的双窗成功、P.try_lock、两次 stale gate、D→S 和锁外业务回调均保留。
- Down 提取 BeginSurfacePointer 小事务：短 T 锁预检命中、工作区、session/epoch/generation；锁外 SetCapture，允许旧窗同步 Cancel 完成；再以当前 HWND 映射同一事件屏幕点并短锁复核，成功才 PointerDown、BeginTracking 和确定一次方向动作/长按快照。失败捕获或代次/模式/命中变化不能遗留新 tracking。没有改成递归锁或增加线程。
- Up 从事件到达时 HWND client 转 screen，FinishSurfacePointer 在清理之前复用 UpdateDragLocked 采最终点。Page/background pending 到 Up 才过阈值时转最终拖动并取消 preview；阈值内仍正常 preview。Arrow/Whiteboard 不转拖动。T 内结束同 session/epoch 的 tracker，再清 pointer/drag 状态，随后锁外 ReleaseCapture；同步旧 CaptureChanged 不回滚刚释放的候选。
- 中轴候选复用 BarBottomDockCenterThresholdDip，通过 cpp global module fragment include 既有 header，再显式传入 cppm 纯策略。只按布局屏幕 DPI 换算一次，不乘 middlePairScale。先按当前实际布局倍率 Clamp，再尝试 Y=0；中心碰撞保留合法自由候选，否则回退当前 pair 的最后 feasible。不改碰撞算法、位置 JSON 或屏幕选择政策。
- 复用现有 TraceDrag/Present：新增限频 constraint（raw/accepted、clamp、collision、中心阻塞、pending mask）和 down/move/up 门禁数值；保留已有单 client Retry 阶段报告。没有把局部停住编成已证实唯一根因。

## 测试接缝与实际入口

cppm 导出四个纯策略，page_control_tests.cpp 由另一代理直接消费：ResolvePptDragCandidate、IsPptDragFrameOwned、IsPptDragGeometryAtTarget、ShouldStartPptDragOnRelease。

cpp 保留 AnimatedBounds/Configure/Scene 私有，在既有 RunOffscreenTests 增加真实生产接缝断言。MovePairWindowsDirect 仅模板化 read/move 操作，UpdateDragLocked 仅模板化窗口移动操作；生产仍调用原 USER32 API。BeginSurfacePointer 模板化 capture/owns/to-client/release，生产仍由 WndProc 提供实际调用。

已有精确产品 CLI `Inkeys.exe --bar-eraser-offscreen-test` 在 IdtMain 初始化配置、单实例、HWND 前 return，调用 Bar.EraserAttribute.Test.cpp 的共享 WARP/D2D 离屏入口，最终调用 RunOffscreenTests。普通 InkeysHeadlessTests --no-window **不会**执行 cpp 私有离屏用例，两种结果必须分列。

新增离屏断言覆盖四控件 Up 无最后 Move 的实际候选/两面 bounds、多帧 logical/presentation 几何稳定；真实另一线程占用 P 导致最后 Move 和 Up 都 fallback，released pending 无后续输入；真实 AnimatedBounds/Scene 中间帧不能凭版本提交；第二窗失败顺序 0→1→0 回滚与单面成功未完成 pair；Up 才入 40 DIP 带；普通位置/倍率仍动画；注入同步旧 Cancel 在新同 pair tracking 前清退、capture 期间 T 可被进入、失败捕获/新 session/失效 generation 拒绝；箭头一次 Down 动作和 Page 短按 preview 保留。

以上均没有创建真实 HWND 或执行真实 capture/Office。窗口几何和 capture 操作在生产接缝处注入，不声称 USER32 设备实测。真实鼠标/触摸交错、实际窗口轨迹与 Office/WPP 仍 NOT VERIFIED。

## 本角色检查与边界

- 已读取 implement.jsonl 全部上下文文件、PRD/design/implement 和截断注入保存文件对应完整 canonical 内容。
- `git diff --check -- Inkeys/Inkeys/UI/PageControl` 通过；原两文件 UTF-8 无 BOM、CRLF 均保留。
- 未修改 PptCOM、Window Service、COM ABI/DLL/TLB、配置和项目文件；页码 -/- 使用已有 0eb1c7c5 回归，不放开未知页墨迹门禁。
- 未运行构建、--no-window、离屏 CLI、GUI、Office、git commit/push，等待主会话统一验证和独立审查。

## 独立审查补正：raw/fit 候选身份

审查发现 raw 用户倍率与 runtime fit 不同时，上一段 released pending 尚未收敛又收到新 Down，BeginPptDragTracking 会覆盖 layout 为 fit 身份但保留 pending/revision，导致 raw publication 无法继续认领 pair。第二段零 Move 或受限 Up 没有发布新候选时，会恢复位置缓动。

生产 BeginDragTracking 现于既有 D→S 短事务核 session/epoch 后，始终用 publishedPpt.layout 建立 canonical tracker 身份；state.dragStartLayout/feasibleLayout 仍保留 runtime fit 几何。没有重置 pending/revision、最后 committedLayout 或跨场次门禁。

增加真实内部离屏回归：raw middle scale=3、现有资源预算将其拟合为不同倍率，实际两面先处于 fit 旧几何 B；第一段窗口提交失败形成非零平移 C 的 released pending；第二段 Down 和零 Move Up 不发布新 candidate，断言 revision、committedLayout、raw ownership 保持；随后首个 Configure fallback 帧立即到达 fit C，两面完成并在后续多帧稳定。此用例实际调用生产 Begin/Update/Finish/Configure/commit，窗口操作仍通过既有接缝注入。

本角色只运行 diff/格式静态检查，未构建或执行用例；主会话统一执行并由独立检查者复核。

## 用户追加的吸附体验修订

初版 40 DIP/吸附直接跳转已按用户追加授权改为 20 屏幕 DIP 双门槛和约 140ms 的 UI3 Y 过渡。现行实施与新增真实离屏用例见 research/snap-revision-implementation.md；自由拖动、raw/fit canonical 和 COM/层级边界保持。主会话统一执行本轮构建与回归，本角色未运行新增用例。
