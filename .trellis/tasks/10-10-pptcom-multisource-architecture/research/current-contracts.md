# 现有契约与技术核查

> 2026-10-10；仅静态阅读及官方资料核查。本轮没有运行 Office/WPS、没有构建或 GUI 测试。代码行号基于当前工作树。

## 仓库证据

| 锚点 | 确认事实与设计影响 |
| --- | --- |
| `PptCOM/PptCOM.csproj:8`、`:11`、`:61` | OutputType=Library、.NET Framework v4.0、ExportTlb 构建步骤；新增 host 不应把现有类库改成 Exe 或顺便升级运行时。 |
| `PptCOM/PptCOM.cs:53`、`:80`、`:92`、`:145` | 原 IUnknown 接口、独立 session 扩展、三个 dynamic 长期根对象及三个 unsafe int 指针槽。原接口/GUID/顺序需兼容；旧指针不是跨进程地址。 |
| `PptCOM/PptCOM.cs:189`、`:208` | descriptor/session getter 输出缓存纯值；传输适配器应复用这种边界，不跨线程读取 Office。 |
| `PptCOM/PptCOM.cs:380`、`:419`、`:1616` | FullCleanup 先失效旧 binding，再解绑、释放根对象，finally 使用 final cleanup；包含 WPS 所需 GC。迁移先保持路径，不能凭“安全保护”重写 RCW 释放。 |
| `PptCOM/PptCOM.cs:829`、`:853`、`:890`、`:1315` | 放映页/开始/结束事件，typed 事件订阅为现有可选能力；dynamic 发现路径不以成功 typed cast/订阅为准入条件。 |
| `PptCOM/PptCOM.cs:944`、`:1059`、`:1128`、`:1194`、`:1233` | ROT 扫描返回单一 bestApp；优先级约为有文稿1、有放映窗2、Active或PID焦点3；当前赢家仅在更高优先级出现时重绑。多候选能力不能仅由抽象层补出。 |
| `PptCOM/PptCOM.cs:684`、`:1378` | 现有窗口活动判断含前台 PID/WPS 跨进程处理；应用内换文稿有检测。此逻辑提供建议证据，但不代替用户对多文稿策略的选择。 |
| `PptCOM/PresentationDescriptor.cs:13`、`:82`、`:266` | descriptor 使用纯值字段；PowerPoint HWND 有借用 typed 接口标量例外；文稿、页和拓扑整体 late-bound。不得用“所有强类型都移除”误删现有必要例外。 |
| `PptCOM/SlideShowSession.cs:19`、`:251` | session generation/binding 校验、owner mailbox 和可取消等待；这些约束适合扩展到 transport 命令，但不能声称现有全部翻页接口已拥有同等 mailbox 保护。 |
| `Inkeys/IdtPlug-in.cpp:212`、`:261`、`:351` | native 激活 COM、传指针并运行服务；适配器迁移须覆盖加载、控制和状态消费者。 |
| `Inkeys/IdtPlug-in.cpp:610`、`:826`、`:872`、`:878` | session JSON 读取 → Draw3 target → UI ready → PptInfoStateBuffer。抽象层不能提前把外部页码变成已呈现页码。 |
| `Inkeys/Inkeys/Business/PptSession.h:14` | 已有 native session/token 概念。统一服务优先放在 Business 边界并复用此 token 语义，不在 UI/render 实现第二套仲裁。 |
| `Inkeys/SuperTop/IdtToken.h:65`、`:222`、`:239` | 从 explorer/ctfmon 获取非提升 token；现有 RunTokenProgram 调 CreateProcessWithTokenW，使用 CREATE_NEW_CONSOLE，并立即关闭 pi.hProcess。需要后台 helper 专用启动包装。 |
| `Inkeys/SuperTop/IdtSuperTop.cpp:280` | 现有SuperTop使用ShellExecuteW runas提升；当前函数随后exit(0)，只能参考标准提升机制，不能照搬成helper启动流程。 |
| `PptCOM.Tests/Program.cs:311`、`PptCOM.Tests/SlideShowSessionTests.cs:425` | 现有 managed 无窗口测试入口，含 descriptor/fake acquisition 释放与 session 保护。 |
| `InkeysHeadlessTests/animation_tests.cpp:1642` | 现有 `--no-window` 路径，可用于后续 native 纯逻辑/门禁验证；不是实际 Office 联动测试。 |

没有在 PptCOM 或 Inkeys 主工程引用中查到已经接入的 NetMQ/libzmq；vcpkg ports/版本目录中出现相关包不证明主产品已有依赖。

## 历史证据边界

阅读 `.trellis/tasks/archive/2026-10/09-27-integration-and-release-check/research/pptcom-live-apartment-investigation.md`。该记录最终修正了“缺显式 COM 初始化可能导致服务创建失败”的早期假设，没有实施三线程 apartment 重构；隔离 Office 的后续日志证实了页1、页2、结束页的 target/Present/UI ready。这说明不能从未见日志直接推导需要 COM apartment 重构。

该历史记录不证明用户此次描述的“STM”是哪种模型，也不覆盖 COM 损坏、WPS 和全部退出场景。用户报告的兼容回归作为本任务 R1 的约束；真实损坏实例回归必须另行验证。已有足够相关任务记录，本轮无需重新挖掘全部聊天历史。

## 官方资料与推论

- [Microsoft：CreateProcessWithTokenW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createprocesswithtokenw)：token 权限、用户 profile、session 和进程初始化/句柄都有独立要求。设计推论：获取非提升 token 不等于 helper 已成功初始化；host 应核查同一交互 session/目标用户及实际子进程身份，并等待协议 Ready。现有 GetUserToken 没有为这个 helper 验证同 session/目标用户，其 TokenElevationType 查询也没有在调用者侧可靠暴露失败；需要由专用启动流程核查，不能静默选取其他登录用户的 token。
- [ZeroMQ：socket](https://libzmq.readthedocs.io/en/latest/zmq_socket.html)：ROUTER/DEALER 允许双向异步消息；常规此类 socket 不应跨线程使用，ROUTER 默认可能丢弃不可路由消息。设计推论：每个端点由一个通信 owner 持有，命令 ACK/状态版本由应用层定义，配置 mandatory 和有界队列/超时。
- [ZeroMQ：socket options](https://libzmq.readthedocs.io/en/latest/zmq_setsockopt.html)：默认 LINGER 可无限等待。设计推论：helper 和 host 退出须使用有界 linger/stop，不把“关闭通信”做成无限阻塞。
- [NetMQ 上游工程](https://github.com/zeromq/netmq/blob/master/src/NetMQ/NetMQ.csproj)：核查时 targets 为 net10.0/net472/netstandard2.1，不包含 net40。设计推论：不能直接把当前 NetMQ 塞进现有 .NET4 类库。只能据此排除该当前NetMQ实现直接进入net40，不能推断所有ZMQ都不兼容。用户补充后，较新helper运行时不再作为绕过Win7 SP1/.NET4基线的路线；可验证具体libzmq绑定或使用命名管道，不预先选择过时包、不升级类库。

这些是实现约束，不构成完整支持声明。实际采用的版本、许可证、部署、ARM64/Win32/x64 及既有 Windows 基线需在后续阶段 P0 核查。

## 用户补充后的已确认决策与增量研究

- 用户已确认活动放映优先：放映选择先于信息来源择优，后台文稿不因来源权限/backend等级抢占。
- 开关开启后双向补充权限域：管理员host启动普通helper；普通host启动管理员helper，主程序自己保持当前权限。
- 用户确认net40的采用目的是兼容Win7 SP1；具体ZMQ不可用时允许命名管道等替代。此为产品运行条件，不表示只有net40能够运行于Win7。
- [Microsoft：Shell启动与runas](https://learn.microsoft.com/en-us/windows/win32/shell/launch)说明runas以管理员运行并按UAC同意/管理员凭据流程交互；[SHELLEXECUTEINFO](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfow)说明SEE_MASK_NOCLOSEPROCESS可返回需自行关闭的进程句柄。设计采用专用runas启动路径，取消结束本次尝试，不复制SuperTop中的主进程退出行为。
- [Microsoft：NamedPipeServerStream](https://learn.microsoft.com/en-us/dotnet/api/system.io.pipes.namedpipeserverstream)、[RunAsClient适用版本](https://learn.microsoft.com/en-us/dotnet/api/system.io.pipes.namedpipeserverstream.runasclient?view=net-7.0)及[Win32 CreateNamedPipeW](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createnamedpipew)支持将Windows命名管道列为候选；.NET4时代已有此管道类型。API存在不证明跨权限/取消/性能合格；实现需要核对net40引用程序集，不能使用后续框架才增加的async重载。
- [Microsoft：命名管道访问控制](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)说明访问受DACL控制，logon SID可用于隔离session。设计推论：双向权限通信必须验证显式访问规则、实际peer及连接失效；默认ACL和同用户名称都不能替代测试。

本次没有启动管理员进程、弹出UAC或运行管道探针；以上为规划研究，真实Win7 SP1/.NET4与双向权限验证留在P0/P3。

## 活动性分值与防反复切换研究

- `PptCOM/PptCOM.cs:1067` / `:1080` / `:1097` / `:1106`：原本地等级为编辑文稿1、放映窗2、Active或PID/WPS判断3；`:1233`只在bestPriority高于targetPriority时重绑。相同分值稳定已有依据，但弱证据可能同档、动态升降也不等于有时间回滞。
- `PptCOM/PptCOM.cs:708` / `:719`：前台PID等于show PID或WPS/wpp进程名规则可认为活动，不能据此区分同进程多个放映窗口。新抽象层保留这些提示价值，归为弱证据，不删兼容实现。
- `PptCOM/PptCOM.cs:1253`：原重绑可Sleep(1000)，这是现有绑定流程事实，不能当全局防抖策略，更不能放到快速页更新路径。
- `PptCOM/PptCOM.cs:2051` / `:2057`及`Inkeys/IdtPlug-in.cpp:464` / `:482`：存在显式COM Activate及native FocusPptSession焦点交接；native当前会检查有效session、控件/设置/前台条件。本轮没有证明当前存在自动仲裁焦点循环；新设计应确保被动仲裁不新增自动激活，防止多个owner互相竞争造成反馈。
- [Microsoft：GetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow)指出切换激活期间前台句柄可能为NULL。因此单次无前台证据不能当放映结束或触发反复降级。
- [Microsoft：SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook)说明out-of-context事件排队及注册线程需要消息循环，回调还需处理重入。技术候选是在native已有合适消息线程收通知、只投递轻量revision再复核，并保留有界采样；不能为活动观察另改COM apartment或在回调中访问Office图。跨权限/平台实效未运行验证。

新分级及防抖是方案建议，用户已确认的产品要求仍是活动放映优先与避免两个候选反复切换。时间参数、提示粒度与前台观察方式由P0基线确定，不承诺单次样本可证明用户意图，也不以简单投票/最后收到的包决定绑定。
