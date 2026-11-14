# 第四次实机反馈：旧清理阻塞新绑定

## 结论与证据等级

- 【实机日志直接确认】旧绑定的同步 Unadvise 持续 55.5777165 秒；同一服务结束后才启动下一服务。新服务约 0.27 秒进入 Active，主要等待发生在旧清理。
- 【用户对照】旧 POWERPNT.exe 尚未退出时，重启 Inkeys 再打开第二份文稿即可立即联动。新 PowerPoint 可被绑定，不必等待旧进程退出。
- 【源码直接确认】GetPptState 同步等待 PptComService；其 finally 同步 FullCleanup/CP 重试。旧解绑、释放和新发现串行，产品进度受旧进程 RPC 返回约束。
- 【未确认】Office 后台驻留的内部原因、Unadvise/Release 内部具体等待栈、两次结束放映后未响应的根因。不能据当前证据将其全归于 Office 或全归于漏释放。

本次完整读取本聊天中的用户原文，3681 条 PptComLife 记录 seq=1..3681 无缺号；已匹配 getter 成功 acquire、call.end/fail 和 release.end/fail，没有未结束的已记录调用。唯一已计时且超过 1 秒的操作是 seq1789 Unadvise。该结论不覆盖没有插桩的操作。机器可读结果见 validation/fourth-user-log-analysis.json。

## 时间线

模块 230cb63b-1639-4077-bf63-257114a882e5，Inkeys PID36492，owner TID22560。

| seq | 现场 | 结果 |
| --- | --- | --- |
| 1808（延迟输出） | 最终关闭通知原始 event_qpc=114349199016 | 行头 qpc 是稍后 owner 输出时刻，不能作为通知到达时刻 |
| 1783 / 1787 | close.final / cleanup.begin | 最终关闭保护进入了清理 |
| 1788→1789 | cp.unadvise.begin/end，连接40，cookie1 | HRESULT=0x800706BE，555777165/10000000 = 55.5777165 秒纯调用耗时 |
| 1797→1803 | 首次失败保留 CP39，重试返回0x800706BA后释放 | 重试仅0.0003294秒，CP释放0.0000557秒，remaining=0 |
| 1809 / 1810 | 旧 service.end / summary | acquired=419，released=419，outstanding=0 |
| 1811 / 1812 | 下一轮 enabled / service.begin | enabled 距旧 service.end 约2.112秒，吻合 native 21×100ms退避 |
| 2423 | 新 owner346 Active，app355，presentation367，window371 | enabled 后约0.269秒进入Active；最终关闭通知到此约58.044秒 |

之前另一份实机日志卡在 CP ReleaseComObject 33.458 秒。本次阻塞转到 Unadvise，说明不能只隔离其中一个操作，也不能靠补充错误码或减少重试次数解决第一次调用尚未返回的等待。55.58秒也不是 Inkeys 固定30秒重绑计时器。

## 为什么前一轮保护未达到产品目标

2031 保护避免了关闭后继续读取对象模型，但仍走原有同步清理路径。FullCleanup 的 UnbindEvents 调用 TryDisconnect；只有 Unadvise 返回后，失败租约才进入 ComEventRetirementRegistry。注册表的重试又在当前/下一 owner 上同步执行。因此当前“退休”是保存失败引用，尚未隔离外部调用耗时。

WPS 正常退出是提供方对照；它本身不足以证明 PowerPoint 一定无引用遗漏。本日志的419/419只证明本模块已跟踪的取得最终配平。更关键的产品证据是：旧清理尚未返回时，新发现完全没有机会执行。

## 下一阶段具体改动边界（设计，未实施）

目标改为：旧 PowerPoint 仍在后台或其清理 RPC 挂起时，新的有效放映仍能被发现并建立联动；旧引用继续确定性清理。

1. 将“停止旧绑定对外服务”和“旧COM清理完成”拆成两个阶段。先失效旧session/descriptor/命令、停用旧sink、完成旧native页码失效，再允许新发现；后续旧清理不能再写共享页码或新server状态。
2. 隔离完整旧绑定的 Unadvise、CP/root Release、待清理重试和GC收尾。只后台执行CP的某一步或把旧CP重试带到新owner，仍会将等待带回新联动。
3. 首选评估让旧COM所有者保留自身对象并完成清理，由纯值协调层开始下一所有者，避免直接将共享RCW交给任意线程Release。必须核实实际apartment和跨线程封送；不能用Task超时后继续并发访问同一个旧RCW，不能强杀线程或Office。
4. 待清理任务数量、连续关闭/重开、同Application多文稿、失败CP/cookie唯一所有权及Inkeys退出必须有明确上限/交接策略。不能无限创建后台线程，也不能将关闭无关文稿扩大为新的全局中断。
5. 优先在 PptCOM.cs 的服务/清理边界及 ComEventConnection.cs 的失败租约调度内评估，测试覆盖在既有 PptCOM.Tests 中；若确认需要native协调，再单独说明 IdtPlug-in.cpp 的必要改动。当前不更改ABI、DLL嵌入方式、依赖或UI。

无GUI验收先用可控制的闸门让旧 Unadvise 和旧 Release 分别停住：在闸门释放前，新绑定必须已进入Active；旧请求/事件不能影响新绑定；释放闸门后每笔取得只释放一次、失败lease不丢失。多次重开、取消关闭、结束放映未关文稿、WPS和轮询路径一并回归。计数配平测试不能代替这项“清理未完成时仍可新绑定”的进度测试。

微软文档支持的边界：[RCW与apartment代理](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/runtime-callable-wrapper)、[Release与正在使用的RCW竞态](https://learn.microsoft.com/en-us/dotnet/framework/debug-trace-profile/raceonrcwcleanup-mda)。这些是设计约束，不是本次挂起已发生该竞态的证据。

## 结束放映后未响应

该份日志的 seq1523→1524 View.Exit 真实耗时0.1259559秒，随后收到一组2014通知。没有据此定位用户报告的两次未响应，也不能宣称解绑隔离会一并解决它们。已向用户请求对应seq/qpc或说明是否在本日志中，避免混淆“结束放映”和“关闭文稿”。

## 本轮验证与产物状态

仅重新解析用户日志、核对源码调用链、复算时间并更新任务记录。没有修改生产源码/DLL/EXE，没有新增构建或GUI测试；现有候选仍未通过本项实机验收。
