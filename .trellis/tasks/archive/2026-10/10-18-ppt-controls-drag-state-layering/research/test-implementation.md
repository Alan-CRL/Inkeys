# PageControl headless 测试实施记录

- 日期：2026-10-18；任务：10-18-ppt-controls-drag-state-layering。
- 授权：用户批准实施1–3，随后追加20DIP双门槛及短促吸附动画要求；本角色仅写入 InkeysHeadlessTests/page_control_tests.cpp 与自身研究记录。
- 不独占工作区；生产 PageControl.cpp/.cppm 由 page_control_implementation 负责，构建与运行由主会话统一执行。本角色未构建、运行测试二进制、启动窗口/Office、操纵桌面或执行 commit/push。

## 新增覆盖

本记录已随用户追加要求更新；原40DIP纯中轴方案属于首轮历史实现，新测试以design第4节及research/snap-revision-design.md的20DIP双条件为准。首轮通过结果不作为本次修订的执行结果。

所有新函数均已登记到既有 RunPageControlTests，直接调用生产 cppm 中的候选、终点判定、ownership 与几何 guard，不在测试复制另一份吸附/布局算法。

| 测试 | 行为证据 |
| --- | --- |
| TestMiddlePairCenterSnap | 1152个二维输入组合：两屏幕（偶数尺寸与负原点/奇数尺寸）×96/144/192 DPI×0.5/1/2.5/3控件倍率×左右控件×正负Y的19.9/20/20.1 DIP×额外侧距X的19.9/20/20.1/120 DIP；两个20门槛都含等号、任一越界不吸附、倍率不扩大吸附带、两侧对称同高、主体中心最多半像素整数舍入误差；直接核logical RECT距边缘减去按实际scale舍入的既有gap后才是额外距离 |
| TestMiddlePairCenterSnap 补充 | 200px高屏幕先Clamp到12.5px再Snap；底部pair的19px偏移不被中轴吸附改动；旧drag-start倍率3与本帧倍率0.5不同时按当前尺寸裁剪（1080高屏幕Y上限496.25px） |
| TestMiddleSnapCollisionFallback | 左/右×正/负四个障碍夹具，X=19px且Y=±19px满足双门槛，先用生产PageControlGroupsOverlap验证“自由候选可行、中心碰撞”；中心不可行时仍保留自由候选且不推动底组 |
| TestMiddleSnapCollisionFallback 补充 | 对角碰撞回退整个当前pair，后续可行候选仍能继续；底组新发布使raw碰撞而中心可行时采用中心；候选中的旧session/epoch/pairVersions/另一组位置不能覆盖当前发布 |
| TestPptReleaseDragDecision | Page/background阈值内保留click/preview；Up正/负X/Y才达到系统半阈值时允许最终drag；已拖动即使零位移也消费Up，清退手势不重启；DragHandle、Arrow、Hidden、Whiteboard边界 |
| TestDragFrameOwnershipAndGeometry | 当前pair两面共享ownership，released pending仍保持；无关pair新发布不打断当前pair；过期session/epoch/revision/位置/倍率拒绝；像素四边任何一像素未到位或非有限scale拒绝；沿用0.0001的scale容差；两面提交及运行期handoff完成后才释放 |

删除了旧 TestDragCommitHandoff 中 const bool presentationLockAvailable=false 的伪锁注入。原tracker测试仍保留，但注释明确其不能作为真实锁竞争或真实几何提交证据。

## 与生产测试协调

- 当前纯候选测试使用同一个 Bar.BottomDock.h 的 BarBottomDockThresholdDip=20，未新增阈值常量/配置。测试中的19.9/20/20.1是验收边界数据，120DIP为远离边缘反例；主栏自身40DIP常量不改。
- 静态检查发现 ResolvePptDragCandidate 初版先按candidate旧scale进行Clamp、再合入ppt当前scale，和当前布局合同不一致。生产角色已把顺序改为先合入当前pair位置再按本帧ppt.layout裁剪，本角色补固定反例；未扩大其他布局算法。
- headless工程只编PageControl.cppm，--no-window不会执行私有AnimatedBounds或WndProc。因此此文件中的geometry/ownership测试是生产策略证据，不能单独证明真实ConfigureSurface动画已停止。
- 追加吸附动画由生产cpp私有MiddlePairSnapState和既有Ease/Render路径负责；本角色不另建纯动画模型，不用bool字段冒充真实中间帧或稳定性。主会话已确认既有 --bar-eraser-offscreen-test 在产品初始化前直接进入WARP/D2D，无HWND；它实际调用PageControl::RunOffscreenTests。生产角色负责在该入口覆盖真实bounds、锁忙兜底、两面部分失败与同步旧capture Cancel。本记录不将尚未执行的内部用例计为已通过。
- 已有touch主点替换、long-press、deadline续帧、页码/ack门禁用例保持；未改COM、Window、工程配置或第4项层级策略。

## 本角色检查与限制

- 仅完成源码静态核对、git diff --check、编码/换行检查；最终构建与测试退出码以主会话验证记录为准。
- page_control_tests.cpp保持原UTF-8无BOM、CRLF；未整文件格式化，diff只含任务相关include、移除伪注入和4个测试及调用点。
- 真实USER32鼠标/触摸capture、显示窗口轨迹、故障设备局部停住、Office/WPP页码与实际z序均NOT VERIFIED，不能从pure/headless或无HWND注入推断实机已通过。
