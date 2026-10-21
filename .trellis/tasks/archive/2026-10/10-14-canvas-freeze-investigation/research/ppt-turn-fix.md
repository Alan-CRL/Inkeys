# PPT 翻页目标修复计划（2026-10-17）

已证交错：raw A + descriptor B 被观察后，旧实现消费 revision 却未解析 target；后续空响应不再重建。修复使用生产与无窗口测试共享的 PptTargetCache，描述符更新设置待解析状态，只有在 raw/descriptor 和会话可信时成功解析才消耗。保持原目标用于暂停/呈现，但 pending 时不可发布。旧 DLL 每次有效 descriptor 视作更新，避免缺少 revision 通知的相同缺口。页码 UI 从实际已接受 target 生成；旧 UI ack 仍严格校验会话、revision、页种类和页码。

所有权：IdtPlug-in.cpp、Business/PptSession.h、InkeysHeadlessTests/ppt_session_tests.cpp；沿用现有编译项，不新增项目配置。无窗口测试覆盖两个更新次序、长期相同 revision、快速翻页、解析失败重试、EndScreen、拓扑变化、会话更替及旧回执。父会话负责完整编译和 --no-window 测试。本代理禁止创建 HWND、启动 GUI、操控桌面或提交。

## 追加：同页暂停恢复的第二条确定缺口

PageControl.cppm 的 PptPageCommitState 只在 session/target/current/total 变化时清除 acknowledged。PptInfo 的 Unknown/恢复帧可被 UI 合并，Host 清了 uiReady 后，原 target 已经 acknowledged 的 UI 不必再回执。因此仅补 pending descriptor 还不能覆盖同页恢复。

修复：Host 的 suspend(true) 在原 presentationTargetMutex 内调用真实 Bridge::RequirePresentationTargetRefresh(expected)。Bridge 在自己的锁内只接受当前 identity，重复暂停只保留标记、不推进 revision；下一次可信 target 发布绕过一次同目标去重，成功后消耗标记，失败保留。新 revision 触发原 Host command、Controller 文档恢复/成功 Present 与 PageControl 新回执；旧 ready/ack 因 identity 不符继续被拒绝。没有强制 uiReady 或绕过持久化门禁。桥接重启清标记且继续不复用 target revision。

产品调用通过 productCallMutex 串行 publish/suspend；新路径锁序仍为 presentationTargetMutex→bridge mutex，没有新增反向持锁回调。无 UI 文件修改、无新文件编译项。

## 实施与验证范围

已修改 IdtPlug-in.cpp、Business/PptSession.h、Draw3.Bridge.h/.cpp、Draw3.Host.cpp、InkeysHeadlessTests/ppt_session_tests.cpp。保留各文件原 BOM/换行。git diff --check 通过；父会话集中执行当前源码构建和 RunPptSessionTests 所在 --no-window 套件。

回归实际调用生产 PptTargetCache、descriptor resolver、StateBridge 和共享 UI ack 谓词：两种交错、同 revision 10,000 次稳定收敛、快速反向翻页、失败解析重试、EndScreen、拓扑及 session 变更、reset、1,000 次重复暂停、恢复新 revision 与迟到暂停/回执拒绝。这里没有执行 Host/GPU/Office/真实 UI 提交，不将这些纯组件回归表述为完整 GUI 验收；实际产品端到端仍待获得窗口运行授权。

独立检查补充回归：真实 Bridge 在 refresh 待处理时拒绝无效 target，之后合法同页发布仍须获得新 revision；Clear 后、Bridge.Reset 后以及新 session 发布后，旧 refresh identity 均被拒绝，后续相同目标 heartbeat 继续去重。这里 Bridge.Reset 是实际生产方法调用，与前面的 cache.target.reset 覆盖分别记录。仅补测试/研究，执行结果由父会话集中构建记录。
