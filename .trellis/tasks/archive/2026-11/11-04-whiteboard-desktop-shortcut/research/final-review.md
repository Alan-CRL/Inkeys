# 独立集成检查记录

日期：2026-11-04。检查代理 `integration_review`，全程只读产品源码；仅写本报告，未编译、启动 GUI、Computer Use、commit 或 push。动态验证由主会话统一执行并记录，本文不是运行通过证明。

## 检查范围与结论

对照本任务 prd/design/implement、check.jsonl、native-desktop 规范，检查完整工作区改动：Config/名称解析、设置真实控件与 FIFO、ShellLink 分类与保障、受限管道、wWinMain 准入/更新/SuperTop/关停、白板状态入口、资源登记、专项测试及三份规范更新。

静态检查结束后，主会话动态专项发现多监听者在首创建者退出后的转发失败，重新开启检查并完成定位与修正；修正后的生产头文件已由独立真实原生探针验证通过，见末节。随后纯关闭释放presence后的白板再入缺口也已复核修正，normal6真实原生探针通过，见末节。当前没有未关闭的明确源码缺陷；最终完整产品 Solution 与专项仍须以主会话真实退出码为准，不能用静态检查结论或探针替代完整产物验证。

## 已关闭的检查项

| 问题 | 已核对的修正 |
| --- | --- |
| ShellLink 参数缓冲 1024 字符可能截断未知后缀，误分类为白板并参与删除 | 读取32768字符并拒绝容量饱和；空白参数不算无参数；真实 ShellLink 加入1100个空格后的未知后缀回归 |
| 已拒绝的另一类草稿可能导致已保存的两类重名：保存 A/B，草稿改 B/A 后只保存一方 | UI 同时保护另一类原草稿及最后有效名；提交独立复查真实已保存对方；排队前验证冻结自身名等于当前请求且不占已保存对方名；双向生产提交和实际控件回归 |
| 路径权限探针早于转发，已有实例快捷入口仍创建/删除 IdtRootCheck 文件 | 将原探针窄范围后移至准入与 Forwarded 早退之后；更新和 SuperTop 仍更晚 |
| 崩溃重启绕过 SetOffSignal，管道仍可能接纳已无人处理的请求 | main 侧非阻塞 RetireLaunchCommands，共用于普通退场、监督器退场和 CrashHandler 已赢得重启意图后的 BeginShutdown 之前；TakeReadyPending 与 Exiting 原子仲裁 |
| 更新失败保留一次性白板意图；监听恢复改写原失败错误码 | 失败分支 DiscardIntent；交接前 Stop/冻结后只传白名单参数；失败重开监听不重放旧意图；保留 StartReplacement 的 GetLastError |

## 数据流与边界复核

- 白板四项 main.json 字段独立 NoUpload；0/1/2严格整数，非法文本/类型回退；字符串名称对沿 Config 私有 rwMutex 复制和提交。预设显示索引不当作持久化 ID。
- 设置类别复用同一 renderCategory 控件与动画，创建、名称、两开关和稳定 child ID 独立。文件 I/O 只由既有 FIFO worker 使用冻结名称、exe、目录、种类、开关执行；改名和语言变化仅保存。
- ShellLink 以完整目标路径和受限参数分类；未知参数保留；修正仅同类旧名，先保存再删除，Windows ordinal 大小写同名不自删。指定名的类别/图标错误按合同修正。
- 管道16字节固定包、1字节回执；消息模式拒绝超长包。每个监听进程使用会话/安装目录fileID前缀加PID的独立端点，客户端只读枚举候选，要求系统查询服务端PID等于候选PID，再检查用户SID/会话/安装根；DACL限当前用户及SYSTEM，拒绝远程客户端。一个绝对截止覆盖枚举、重试、连接及读写；只有候选端点全部不存在才准入，连接/身份/回执失败不退化为第二份；允许多开仍先转发。
- 工作区消费点等待真实启动进度全部完成，包括 Window/Draw3/Whiteboard/Bar/Freeze/PPT 客户端，失败与退出不消费。已有白板幂等；最小化经既有窗口组恢复路径；新进入复用 RequestWhiteboardActive。
- update/SuperTop 合法交接只携带未消费的一次性意图；Adopt 成功不向原进程反向转发；Restart/Crash/Warn 监督器参数不增加白板意图。监听线程 overlapped I/O 支持 stop 取消，无阻塞 Flush；关停屏障 Stop/join。
- `IDI_ICON1=191` 保持最小图标组，原 icon.ico 没有源码改动；白板 `IDI_ICON2=332`，RC、vcxproj、filters 和 SVG/ICO 同步，ShellLink 使用负资源ID。ICO九尺寸PNG帧及最终EXE提取须以主会话脚本结果为准。
- `--desktop-shortcut-test` 保持严格两参数早退及原 presentation harness 开关；新增管道专项沿原独立单实例安全测试入口，不启动产品窗口。`--desktop-shortcut-only --no-window` 只执行配置和通用设置控件，避免无关预览输出。

## 测试质量与覆盖边界

新增测试实际使用 Config 编解码、ImGui控件、冻结 FIFO、临时目录真实 ShellLink、私有复制 EXE 与真实命名管道/Ready/父退出交接；坏包/错误目录/伪装服务端/多监听与首份退出/延迟监听/有限超时不是仅复写生产公式。主会话新增的图标脚本读取最终EXE资源，逐帧与ICO比对后按负ID提取，验证对象正确。

更新哨兵测试证明生产 Admit 入口转发不访问哨兵；wWinMain 无写入/无更新/无提权的顺序同时经过源码复核。它不等于完整产品GUI冷启动、真实文件更新/回滚或SuperTop三段式已实机运行。白板状态专项覆盖活动幂等、最小化恢复和原入口调用；真实PPT/桌面前台行为仍需人工证据。

错误用户的 EqualSid 分支可由不同合成SID测试，但没有真正启动另一用户的客户端；真实 UIAccess/跨完整性、Win7、跨架构更新及真实SuperTop/UAC也没有由本检查代理验证。最终报告必须分别注明，不能把同用户私有副本或静态检查写成这些实机边界通过。

`git diff --check --ignore-submodules` 本检查轮退出0。没有修改第三方子模块或通过关闭校验绕过环境问题；构建环境结果另见主会话 validation。

## 动态专项重新开启：首创建者退出后的服务端 PID（探针验证已关闭）

主会话 `--single-instance-tests` 的管道分段未通过，独立小型原生探针在相同生产头文件上复现。`Build/whiteboard-shortcut-validation/pipe-probe/normal4-stderr.log`：首个监听者 PID45756 退出后，第二监听者仍报告 ready，但客户端查询到的原始服务端 PID 仍为45756，停在身份检查阶段而拒绝，不是测试等待时间不足。此前单一共享管道名、多进程创建实例的假设未被实际 Windows 行为支持。

实施修正：每个监听者改用带 PID 后缀的独立管道名，客户端通过 Toolhelp32 只读枚举进程ID候选；仍要求系统查询的真实服务端PID等于候选PID，且同SID/会话/安装根。不放宽对端验证，不引入其他命令。有限发现、busy重试、连接及读写共用一个绝对截止；只有所有候选端点不存在才返回Missing，连接/身份/回执失败保持Failed，不允许第二实例兜底。

已读取 `Build/whiteboard-shortcut-validation/pipe-probe/normal5-stderr.log`：生产头文件构建的真实原生探针专项PASS，首份退出后第二份转发accepted、两份都退出后新监听者转发accepted、两个真实已接纳请求合并为一次、错误目录/伪装服务端、坏包/超长/版本、超时、延迟监听及handoff均通过。该结果关闭已复现的共享管道创建者PID缺陷；最终主程序完整构建和原 `--single-instance-tests` 需主会话继续确认。设计和规范同步由实施所有者/主会话完成。

## 纯关闭提前释放 presence 的复核（探针及接线验证已关闭）

主会话进一步核对发现：既有 ReleaseForClose 有意在纯关闭清理期释放原 presence，以允许普通用户重新打开；此时白板管道已拒收并关闭，Send 返回 Missing，原 Acquire 便可能接受白板新实例，与本任务“退出中的既有实例应提示并退出”不符。修正必须只影响白板请求且未允许多开，不能改写普通重新打开、允许多开或已验证 Adopt 的既有语义。

单纯在 Acquire 前扫描其他活进程存在反例：两个并发冷启动白板进程均看到对方但尚无一个准入，可一起等待并失败；目录内其他工具进程也不能当作本程序已准入实例。已向主会话及实施所有者建议使用仅在准入成功后建立、独立于原 presence/管道停止而保持到进程末尾的内核生命周期凭据，或同等强度的正向退场标记，并补真实冷启动 barrier 与 ReleaseForClose 后进程仍存活的回归。

最终实现为只在已准入 Server.Start 中建立的 `Local\<目录presence身份>.CommandLifetime` 内核事件，显式当前用户/SYSTEM DACL；客户端仅 OpenEvent(SYNCHRONIZE) 读取存在性，不信号、不写入。事件跨 Stop/ReleaseForClose 保持至 Server 静态生命周期结束，已验证的安装根重绑定保留原根与新根引用。仅白板且未允许多开在管道Missing后探测该正向凭据；存在则有限重试/失败退出，不影响普通重新打开、允许多开和Adopt。未准入冷启动者不建立该凭据，仍由原Acquire原子选出赢家，不引入目录内普通工具进程或文件名启发式。

Process32NextW结束已检查ERROR_NO_MORE_FILES，其他枚举错误failclosed。Server在寿命凭据成功后、管道创建前解除退休状态，因此仅IPC监听失败不会吞掉本地冷启动意图；真正BeginExit继续拒绝消费。主流程初次及安装根重绑定后立即检查LifetimeReady，缺少强凭据就提示并退出，避免无凭据继续运行；其他重启监听点复用已持有凭据。

已读取 `normal6-stderr.log`：阻塞本PID端点造成真实管道初始化失败时，本地意图仍一次消费、外部转发失败closed、BeginExit后不消费；两份真实冷白板进程barrier结果0/1（一个Accepted、一个Forwarded）；真实Stop+ReleaseForClose且旧进程仍存活时白板deny失败、普通重新打开Accepted20、allowtrue白板Accepted20。其余原管道专项同轮PASS。已直接复核IdtMain初始/重绑定LifetimeReady保护接线及diff --check退出0；完整产品重新构建与原专项仍由主会话记录。
