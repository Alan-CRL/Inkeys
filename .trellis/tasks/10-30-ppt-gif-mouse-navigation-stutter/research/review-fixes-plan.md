# 2026-10-31 审阅问题修复（用户已批准实施）

## 范围与证据

本轮延续现有 in_progress 任务；用户已阅读三项原因和修复方向并要求修复。不创建新任务，不修改其他并行改动，不 commit/push，不启动 GUI/Office。白板问题来源于已归档 page-status-transition-interaction 的 192ff6d5，本轮只修复该失败路径，不重开或改写其历史证据。

- P1：97e61925 的 fast clone 永久保留 FullName/Name；同 binding 结束→另存→重放仍发布旧路径。源码构建 DLL 和随附 DLL 的现有 ServerHarness 均已复现；native 以 fullName 生成持久化 key。
- P2：入队解锁后 PublishNavigationWaiting，消费线程可先 Complete；Complete 对空状态不记账，迟到 Publish 留下永久等待标记。
- P2：冷页加载失败追加的 SetPage 未绑定白板场景，队列 [PersistenceCompleted, SetWorkspace(Desktop)] 使恢复命令在 Desktop 执行，可能切页/追加空页。

## 实施合同

1. 文稿身份：只在原 PptComService owner 中以实际 dynamic 读取新鲜身份标量，身份失效/busy/过期 generation 不得发布旧路径为新的可信身份。拓扑缓存必须区分放映场次；新场次首次完整验证。同场普通翻页和周期维护仍保持 Slides.Item=0。身份变化可完整验证一次；无需更换 COM 公共 ABI、JSON schema、线程或注册方式。不要只增 binding revision 而令已绑定事件永久失效。所有 acquisition 一次释放/明确转移；标量意外 COM 返回也必须配平，禁止 GC/FinalRelease 补漏。
2. 等待状态：保留业务锁外 UI 发布，在状态机记住同场已完成 epoch，拒绝迟到/旧 Publish；旧完成不能清新请求，旧场不能污染新场，换场与 shutdown 收束。状态仅影响视觉，不产生 ready/输入权限。
3. 白板：失败恢复必须绑定原 workspace/generation/原页身份，消费时拒绝换场或换页结果。失败后 whiteboardLoading 已 reset，不能直接照抄成功加载的检查导致恢复永远被拒。复用现有场景与呈现机制，只补原页真实呈现；原翻页命令恰好一次结算，不创建其他工作区页面，不伪造 ready。

## 文件所有权

- managed 实施：PptCOM/PptCOM.cs、PresentationDescriptor.cs、PptCOM.Tests/ManagedCompatibilityTests.cs（必要时 Program.cs）。
- waiting 实施：PageControl.cppm、Ppt.cpp，以及 InkeysHeadlessTests/page_control_tests.cpp；仅必要时触及 IdtPlug-in.cpp，先通知协调者。
- whiteboard 实施：Draw3.DrawingController.cpp、Draw3.HiddenWindowTest.cpp、必要的 Draw3.WindowControl.cppm/相关现有命令声明；不得占用 waiting 代理文件。
- 主会话：本任务文档/规范、产物交接、构建/测试和独立审查。

## 验收

- 新增行为回归覆盖：同 binding 另存和首存/重放、身份 busy/异常/代次迟到与释放账本；Complete→Publish、正常顺序、新旧 epoch/场次与 shutdown；冷页失败同场恢复、退出到 Desktop/PPT、退出再入白板及页变化。
- 先构建 managed Release AnyCPU，运行 PptCOM.Tests；交接同次 DLL/TLB，TLB 不应有 ABI 差异。
- 使用 vswhere 定位 ARM64 原生 MSBuild，同 invocation 规范化 PATH，构建 InkeysRepo.sln Debug|ARM64；允许至少五分钟，若环境错误先定位，不能污染代码绕过。
- 执行适用 --page-control-only / --no-window；不执行 hidden-window、主程序 GUI 或 Office 测试。验证无 GUI 是本轮限制，真实退出/重开与等待像素旧缺口保持未验。
- 检查 diff、BOM/EOL、COM acquisition 及最小改动；更新 spec 和验证记录。独立 check 通过后交付未提交修改，原 GUI 未验项不标已完成。

## 独立审查补充

审查发现专用白板补呈现早于全局最终退出门处理；现由生产消费 helper 显式接收并拒绝 currentLoadRetry.ExitPrepared()，使用实际 ProcessPresentationExitBarrier + FIFO 回归确认最终 ACK 后不补呈现/不重复结算。
