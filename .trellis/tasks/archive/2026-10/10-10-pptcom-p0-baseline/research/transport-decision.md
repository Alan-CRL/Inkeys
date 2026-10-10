# P0 首发 helper 通信决策

> 2026-10-10。本页为P0技术选型证据，不是Win7/跨权限运行验收。实测以probe-results.md和../validation.md为准；生产helper和adapter尚未实现。

## 决策及适用范围

首发选择 **Windows本机双向命名管道**；内置类库继续按总体设计采用共享内存状态通道与类库控制接口。统一消息/候选/仲裁合同不依赖传输。后续P3实现管道adapter，不同时加入ZMQ与pipe的运行时自动回退。

选型依据是本项目只需要同机跨进程通信，net40可使用已有管道APM/安全描述符接口，native已有Win32工具链，不需增加托管消息库与分架构的第三方消息DLL。功能、部署和退出控制成本优于当前未选定具体版本的ZMQ路线；不据此宣称管道更快或全部ZMQ不支持Win7/.NET4。

首发选择与运行准入分开：目标仍为Win7 SP1/.NET4；Win7与跨权限实机验证未完成前，不宣称helper兼容性达标。若这些门禁暴露明确问题，回到选型决策，而不是提高.NET最低版本或改COM线程模型。

## 候选证据

| 候选 | 确认事实 | 对首发的影响 |
| --- | --- | --- |
| 当前NetMQ master | 官方csproj目标为net10.0/net472/netstandard2.1，未提供net40目标；为纯C#移植，不能简单等同原生libzmq | 此份实现不能直接用于本任务net40；不降版本盲试或升级产品运行时 |
| clrzmq4 + 原生libzmq | 官方ZeroMQ.vs.csproj明确v4.0，README说明AnyCPU包装及i386/amd64原生库加载、预构建libzmq 4.1.n | 证明存在net40包装候选；未验证具体包Win7运行、ARM64原生部署、维护/发布条件及取消退出，不作为首发依赖 |
| 直接libzmq C API + 自有P/Invoke | 可能绕开现有托管绑定目标，但仍需选定库版本、x86/x64/ARM64构建、加载/发布/许可与双端封装 | 本仓库主vcpkg manifest未依赖ZMQ；子模块ports中有条目不代表已经安装/集成，不为P0引入依赖或新构建链 |
| Windows命名管道 | Win32 CreateNamedPipe/ReadFile/WriteFile/CancelIoEx等；managed针对真正.NET4引用程序集编译探针，使用APM和PipeSecurity | 无新消息库；首发采用，生产跨权限/安全/拥塞与Win7仍须实机验证 |

官方证据检索日期均为2026-10-10：

- [NetMQ项目文件](https://github.com/zeromq/netmq/blob/master/src/NetMQ/NetMQ.csproj)。
- [clrzmq4项目文件](https://github.com/zeromq/clrzmq4/blob/master/ZeroMQ.vs.csproj)与[官方README](https://github.com/zeromq/clrzmq4#readme)。README是提供方声明，未当作本项目运行结果。
- [CreateNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipea)：双向、byte mode、overlapped、远程客户端标志和安全描述符。
- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)：最低Windows Vista；只请求取消，必须检查IO实际完成，不能立刻释放OVERLAPPED或缓冲。

ZMQ若未来重新评估：socket由单一通信owner维护、配置有界队列/发送等待/linger；默认无限linger会使context退出等待，ROUTER默认无法路由可丢消息。该问题属于通信退出条件，不能归因于Office RCW，也不能通过改COM apartment解决。见[libzmq socket规则](https://libzmq.readthedocs.io/en/latest/zmq_socket.html)与[选项说明](https://libzmq.readthedocs.io/en/latest/zmq_setsockopt.html)。

## .NET4 与 Win7 证明层级

- 本机已有`C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.0/`；使用这些引用编译证明所调用的managed API属于该目标框架，不证明Windows7运行行为。
- managed端采用.NET4 APM Begin/End；不依赖WaitForConnectionAsync、CancellationToken新重载或PipeOptions.CurrentUserOnly。停止需取消/关闭IO并观察操作完成，再收束owner。
- 生产native端优先用CreateNamedPipeW、FILE_FLAG_OVERLAPPED、Win7可用的CancelIoEx和完成状态；避免PIPE_NOWAIT伪装异步。CreateNamedPipe基本API最低系统早于Win7，但每个实际使用标志和退出行为仍按Win7实机检查。
- native主程序为管道server，managed helper连接。P0的managed server/Win32 client探针用于字节互通验证，不能冒充最终native server/managed client实现，也不覆盖ARM64/x86/x64所有部署。
- 当前运行设备为Windows 11 ARM64，系统版本10.0.26200，安装.NET Framework Full Version4.8.09221/Release533509。CLR 4.x的Environment.Version字符串不等同原版.NET4。P0不会卸载/切换系统CLR。

## P3 必須落实的安全与退出合同

1. 每host instance使用独立随机pipe名、启动nonce、source epoch；server限制本机连接。单靠客户端指定`serverName='.'`并不拒绝远程客户端，必要时native创建带PIPE_REJECT_REMOTE_CLIENTS的handle，managed通过net40 SafePipeHandle构造接入。
2. 根据本次实际host/helper SID、交互session和完整性级别创建明确ACL。禁止以Everyone FullControl解决连接失败；当前SID同权限探针不能外推另一个管理员账户。
3. token身份、进程创建身份和本次启动nonce参与握手；双方检查内核返回的peer PID与本次创建进程，随后检查实际token/session。PID可复用，消息中自报PID不可代替验证。GetNamedPipeClientProcessId/GetNamedPipeServerProcessId最低系统为Vista，见[client API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientprocessid)与[server API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeserverprocessid)。
4. 高host与普通helper的访问不能只检查DACL；还需确认对象完整性标签。普通用户通常medium，已提升用户high，不能把普通用户称为Low integrity。见[MIC机制](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control)。UAC其他账号凭据有独立SID/logon，需独立组合验收。
5. 普通helper到高host的通道允许范围仅限本次授权的peer与纯值协议；通信验证不在COM owner上RunAsClient/模拟对方token，避免改变Office观察权限域。[管道访问文档](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)说明默认ACL及logon SID；最终peer按launch结果限制，不能假定所有提升场景logon SID相同。
6. 4字节固定宽度长度头+UTF-8 body（具体Envelope版本在P1冻结），先检查长度再分配；严格处理部分IO、半帧EOF和无进展deadline。连接断开撤销route，重连需Hello/全量，旧命令不补发。
7. 有界通信队列：绝对页状态可合并，命令/ACK/lifecycle不随意丢弃；读、写、等待连接都可被停止。P0探针的取消通过不代表写端背压、生产heartbeat/父进程检测或COM阻塞退出已验证。
8. 正常停止先撤销路由和请求，Office按原释放边界退出，通信停止独立处理。COM实际卡住时不能保证timeout撤回；永不结束用户PowerPoint/WPS。

## 活动优先与防反复切换的接口边界

活动仲裁仍交由native统一抽象层。后端提供事实、质量和原因，不直接跨backend比较旧整数priority，不让重复来源形成多票。当前COM事件/旧命令有既有调用路径，P0不统一它们的线程。

统一前台观察可由现有native消息循环接收EVENT_SYSTEM_FOREGROUND的out-of-context hook通知，并在服务中重读GetForegroundWindow/进程/已验证show HWND；短轮询是兜底。hook回调只投递纯值通知，不操作COM、不执行业务命令。须在实际消息循环线程安装/卸载并处理重入；无需在COM线程上新建消息循环或切apartment。[SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook)要求安装线程有消息循环；[GetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow)允许过渡期返回NULL，不能把NULL解释为放映已结束。

P0源码采样与后续测量清单见[com-baseline.md](com-baseline.md)。保持以下策略边界：

- 核验真实show前台的硬证据高于provider Active/PID/WPS弱提示；同等级保留当前目标。
- 弱挑战观察连续的新样本并满足窗口；心跳/重传不增加样本。软反向切换/来源恢复有稳定窗口，硬前台变更与已确认失效可绕过软冷却。
- 正常PageChanged立即更新当前绑定，不等待切换防抖。来源页新鲜度不等于通信心跳。
- 当前native新会话创建存在Focus排队。P2/P4接管时需识别该路径，避免被动换源重复激活窗口或自身Focus产生新的活动挑战；不能写成现状只有按钮显式Focus，也不以P0名义删除现有行为。
- 弱观察窗口、最少新样本数、软回切间隔、来源恢复窗口、前台复核周期、失联判定和准备deadline暂不固化为产品毫秒值。P1先可控时间测合同，P3-P5实机校准。
