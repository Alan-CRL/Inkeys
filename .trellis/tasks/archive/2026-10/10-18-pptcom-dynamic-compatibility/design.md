# 修复设计（已批准实施，2026-10-18）

## 1. 目标、阶段和边界

- 用户已于2026-10-18批准本设计并要求开始实现；GUI/Office启动及commit/push仍未授权。
- 以用户最新要求为准：常规 COM 使用 dynamic；仅有逐成员证据的能力例外允许包装；确定性释放；事件不可用时轮询保底。
- 首要兼容场景是“typed QI / PIA / 事件不可用，但生产 dynamic 能完成核心读取与调用”。不能把所有类型库损坏都等同于 dynamic 仍可用，必须做实际能力判定。
- 保留 PowerPoint2007/WPS2013+ 现有产品范围，重点验收 PowerPoint2010/MSO14；不改最低版本文案，不升级框架或依赖，不修改全局 COM 注册。
- 沿用现有 service owner、mailbox、COM GUID/vtable、descriptor/session wire schema、Draw3 页/文稿身份与成功呈现/UI ready 交接。
- 本任务修复 COM 边界和必要 native 消费；不重写 COM 公寓模型、UI3、Draw3 存储和构建系统。

## 2. 访问方式与能力隔离

### 2.1 常规 dynamic adapter

保留可注入的访问/释放测试边界，但生产适配器必须使用显式、可审计的 C# dynamic 成员调用。现有字符串属性接口可在内部对实际使用的有限成员分派到 dynamic.Name、dynamic.View 等访问；不得让默认分支悄悄 InvokeMember。实现时按最小 diff 决定是否保留现有内部类名，不为命名做额外重构。

覆盖 application/presentation/window/view/slides/slide 的常规属性、Slides.Item，以及 Next/Previous/Exit/Activate/工具操作。返回对象显式交给调用者的具名局部变量管理；标量不保留 RCW。

集合 Item 的实际调用形式须经真实 COM 或隔离 IDispatch fixture 验证，不能把普通 CLR 字典访问成功当作 Office COM 成功。

### 2.2 包装例外

| 能力 | 默认 | 允许例外和边界 |
| --- | --- | --- |
| 核心属性、集合、命令 | dynamic | 无证据不得增加 typed/反射默认路径 |
| 事件订阅/取消订阅 | 明确所有权的内部事件包装 | 允许该已知能力例外；实施复核采用raw IDispatch sink+连接点/cookie，不物化回调RCW，失败回滚/重试并启用轮询 |
| HWND 标量 | 已验证必要的类型包装例外 | 用户确认此前测试过dynamic无法取得HWND；沿用类型标量包装，失败仍可进入受验证Win32后备，不作普通dynamic重试 |
| 激光/荧光/页面总览等可选成员 | dynamic | 不支持时关闭该能力；无成员支持证据不加新版本依赖 |

2026-10-18用户补充确认：此前已经测试过dynamic无法获取HWND，因此该标量保留包装器例外；不为重做该实验延迟实施。PIA的FRestricted证据作为机制补充，不能把例外扩大到普通成员。

错误分类应保留原始 HRESULT：
- busy（RPC_E_CALL_REJECTED、RPC_E_SERVERCALL_RETRYLATER、Office busy 等已有分类）进入有界重试，不能以另一种访问方式重复执行有副作用命令；
- 成员不存在/类型接口不支持仅降级相关能力或进入已列举例外；
- disconnected/对象过期按 binding 生命周期失效并重新发现；
- COM 激活、ROT 权限/可见性或核心 IDispatch 完全不可用，记录 Unavailable，不承诺靠 dynamic 恢复。

### 2.3 “大部分功能”的能力边界

- 类型接口/事件失败不能关掉仍可通过 dynamic 完成的识别、页码、上一页/下一页、退出、基本窗口控制与可靠页级画布。
- 缺 SlideID 可用显式 PageIndexFallback，但不能把 ordinal 假装稳定 ID，恢复范围沿用存储合同。
- 原生工具接管/总览等可选 API 不支持，只影响该功能。
- 若无法证明目标文稿、页或窗口身份，不得开放与旧文稿混用的可写/持久化目标。基础控制与持久化能力不能继续由一个“所有新字段齐全”的总开关统一决定；具体最小 native 分离见 research/session-busy-polling-and-window-plan.md。
- 为满足“大部分功能仍可用”，保留现有四个分页控件，内部增加明确的“基础控制页码/已绑定墨迹页码”发布区别，不新建窗口或设置流程。仅有可靠 COM 场次、没有可靠 HWND 时，也允许当前页/总数、上一页/下一页及按场次退出通过 owner 对绑定 COM 对象执行。
- 基础控制 token 由 service/binding/show session 及 owner 捕获的当前 COM show 约束；HWND=0 不得送入 IsWindow 退出判据。不得把 HWND=0 或缺 SlideID 当成所有命令失败的共同前提。
- 基础页码单独从有界 fresh raw/owner 事实发布，不能写入代表墨迹加载完成的 PptInfoStateBuffer；Ppt 模块显式抑制该模式的 pagePresented/Draw3 UI-ready 回执，不靠 targetRevision=0 的隐含约定伪造 ready。已有旧 Presentation 目标须隔离，不让其墨迹继续冒充新文稿；后续获得可信身份后走正常 target→Present→UI ack 再提升。
- 无可靠窗口时不进行 OS 抢焦点或猜测窗口定位，控件沿用现有默认屏幕/布局；动态 COM 对象定向 Activate 只有当前提供方支持且会话仍匹配才执行。
- 这是对研究报告“暂不做无窗口UI”的范围建议的明确取舍：用户将损坏设备核心可用性列为首要，故纳入现有控件的局部状态分离。研究报告仍保留其风险论证，本设计/PRD为拟实施范围。
- 此边界不引入新的不可信墨迹写入通道；不添加“任意前台窗口+旧墨迹”的回退。

## 3. COM acquisition 与确定性释放

### 3.1 统一规则

每次 ROT/GetObject/COM 属性/方法返回对象是一次需记账的 acquisition，即使映射到同一个 RCW。managed 赋值、参数传递、as/cast 只是 alias，不能因此额外 Release。所有权只能明确转移一次。

- 每个临时对象先命名，再在 finally 按子到父释放；禁止 View.Next、SlideNavigation.Visible 等遗失中间 COM 对象的链式调用。
- 禁止 foreach COM 集合；按索引获取并逐个释放。
- root 字段由 owner 持有；reader/类型标量包装只是借用，不释放 root。
- 去重列表只保留 alias，不凭列表条目再释放已转移/已释放的 acquisition。
- 相同 identity 不是跳过本次新返回引用释放的理由。
- FinalRelease 不用于临时对象、事件参数或共享 alias；对现有 root 的最终释放也必须先证明没有在途调用、没有其他所有者，不再用它掩盖账本不配平。
- GC.Collect / WaitForPendingFinalizers 不作为正确性、恢复或退出的必要机制；先修配平，移除仅为等待泄漏引用终结的依赖。若声称某旧 WPS 仍需特例，必须补独立实机证据再审，不把强制 GC 作为默认补救。

### 3.2 本次最小审计/修复集合

1. ROT 候选替换时旧 bestApp 与 foundAppObjects 的双释放，以及同 identity bestApp 的未释放。
2. 同文稿比较 helper 和轮询对新取得 Presentation/SlideShowWindow 的同 identity 跳过释放。
3. window 安装/替换区分“已转移到字段”与“仍是本次临时取得”，不按 identity 猜所有权。
4. Next/Previous/End/View 等中间 View/SlideNavigation 及时释放。
5. 事件部分注册失败回滚、逐订阅取消；一个取消失败仍继续取消其余已订阅项。
6. raw事件sink不物化回调WnObj的RCW，不直接覆盖长期字段。owner通过自身正常COM获取建立长期window；不能为了“释放严格”对包装器共享事件参数盲目Release/FinalRelease。
7. reader、annotation 和 exit 继续具备每个异常点的局部释放保障。

完整取得点账本与风险证据见 research/com-ownership-and-dynamic-audit.md。

## 4. owner 串行化与清理

- 复用现有 SlideShowOwnerMailbox，把触碰长期 root 的 Next/Previous/legacy End/View/Activate 业务操作排到现有 service owner；不另建 COM worker/apartment。
- SlideNameIndex/GetPptHwnd 公开读取改为 owner 发布的纯值缓存或受保护的 owner 请求，避免其他线程现场遍历 root。
- 同一业务请求携带当前 binding/session/window 身份（按现有接口能力内部捕获）；执行前校验。owner 自身调用不得再次同步排队等待自己。
- 锁只保护队列、代次和纯值；锁外执行 COM、事件取消与释放。事件回调仅推进轻量代次和唤醒，不持业务锁调 Office。
- rebind/cleanup 先停止接受旧 binding 请求、使代次失效，再由 owner 取消未执行请求、解绑事件并释放 root；服务停止在实际使用结束后发生。
- 排队尚未执行的超时请求取消后不得迟到执行；已经进入 Office 的 COM 调用不可撤销，必须区分执行中/已完成/未执行，不能承诺超时就绝无后效，不重复提交有副作用命令。
- 保持现有末页 Next(check) 保护语义和旧 COM ABI，补边界测试。
- 不以新线程大锁或强制终止 Office 解决清理竞态。

## 5. 事件与轮询

- 事件连接按真实IConnectionPoint/cookie记账；raw sink的Invoke仅分发静态DISPID/纯值hint，不经CLR事件包装物化入站参数。Find/Advise失败进入polling，成功Advise只取消一次；失败Unadvise保留可重试lease，不能复用已断开的app alias假重试。具体依据与停止失败边界见research/implementation-decisions.md。
- 轮询独立完成已在放映时的接入、当前页/总数、窗口更换、结束/重开和文稿变化，不以先收到 Begin 为前提。
- 每次成功取得本轮 show 都比较 COM identity；若与当前 root 不同，安全转移所有权并推进 show session/generation，HWND 仍为0也必须识别更换。showCount=0明确结束后释放/清空旧show root；旧对象还能读取Active属性绝不等于仍是当前放映。
- SlideShowWindows.Count 成员本身缺失时不能再成为总开关：可由 fresh show.Presentation 与绑定文稿identity匹配，结合成功的View.State/同次页数据确认活动；不能仅凭旧Active可读推断Active，也不能把属性失败推断Inactive。
- 默认沿用约500ms慢维护上限，避免同时把本任务扩展成激进轮询优化。事件是减少等待的提示，不能代替周期性恢复路径。
- 无事件、同 HWND 的结束/重开若完全发生在观测间隙且无其他可见证据，属于可观测性边界；不能虚称绝无漏边沿。
- 显式记录事件/轮询模式和切换原因，用低频状态日志支持后续现场诊断。

## 6. busy、快照和恢复

上轮故障注入证明新旧公开缓存对 busy 发布不同、恢复受3秒deadline影响；但简单把旧 stable 发布成 Valid 不安全。same binding/session 并不能证明 busy 期间没有换页。

| 观察 | UI/场次 | 写入及目标 | 后续 |
| --- | --- | --- | --- |
| 完整新候选且当前身份匹配 | 正常更新 | 按目标→Present→UI ack 开放 | 稳定低频复核 |
| 同场短暂 busy，当前页不确定 | 保留已呈现内容和场次 | 暂停新写入，不把旧页标新 ready | 独立快速重试，默认不超过一轮500ms调度间隔后再尝试 |
| fresh 读取证明仍是同页/同拓扑 | 保持场次 | 可复用纯值拓扑，需满足 generation 和 ready 约束 | 避免无必要重扫整份文稿 |
| raw 或页事件表明已换页，descriptor 仍旧 | 保留旧显示待交接 | 旧页不代表新页可写 | 获取当前页候选 |
| 新 binding/session 首次 busy | 隔离新旧身份 | 不借旧画布/窗口/ready | 重试新场身份 |
| 确认退出或窗口销毁/被复用 | 正常退出收尾 | 撤销旧目标 | 桌面交接 |

- retry deadline 与稳定3秒复核deadline分开；500ms 是下一次调度尝试上界，COM 调用本身阻塞时间不在此承诺内。
- legacy descriptor 与 session envelope 使用同一份经判定的快照语义；明确区分最后稳定数据与当前可写证据。
- 瞬态失败保持恢复所需上下文，不让一个 optional Name/Caption/SlideID 故障永久中断基本轮询。
- 拓扑复用以有效当前页证据和代次为前提；不因本任务贸然加入复杂拓扑缓存系统或放宽重排/插删校验。

## 7. HWND 后备和身份

- HWND沿用用户确认过的类型包装标量例外；包装不可用时尝试受验证Win32后备。其余普通标量保持dynamic。
- Win32 后备必须识别真正放映窗口，验证窗口仍存在、所属进程、提供方/放映窗口形态及与本次文稿/COM绑定的关联，拒绝编辑窗口、同名多候选及被复用 HWND。
- 现有“文件名+PowerPoint/WPS 标题关键词且唯一”只作候选筛选，不能独自当强身份；特别 WPP 名称与 WPS 名称/本地化不能误伤。
- 无 COM PID 关联时不得从进程名/标题唯一推导该进程就是绑定实例。经研究确认的最小关联策略作为实施前检查点，不能以安全校验为名让 typed cast 再成为全功能必要条件。
- 无法建立可靠窗口绑定时，记录具体降级原因；不伪造 descriptor HWND/PID 或选取任意前台窗口。基础控制按第2.3节保留，页级墨迹/自动文稿保存继续隔离。process-local键只限制未来恢复，不能证明当场窗口归属，不能据此解锁写入。
- 控制类 guarded Exit 应对 owner 捕获的当前 COM show 执行；同场 generation/对象校验成立且获取View后再次核对时，不以HWND非零为必要条件。窗口激活/OS焦点与页级墨迹仍单独要求可靠窗口。
- 同一最终解析结果同时供 descriptor、session 和原有 GetPptHwnd 使用，避免再次出现两个读取器得到不同身份。

## 8. ABI、产物、规范

- 不新增/重排旧 IPptCOMServer 方法，不改变 GUID 和两个当前 wire schema；若研究后确实需要外部能力字段，单独提出审批变更，不能实施时静默扩展。
- 保留 .NET4 / AnyCPU / 当前 PIA 依赖，不为测试旧版而升级 SDK/包。
- 规范更新范围：ppt-interop/com-contract.md、native-session-ui3.md、index.md；必要时 cross-layer-thinking-guide 的 COM 访问/释放检查。旧任务归档记录不回写历史。
- 明确去掉“统一 InvokeMember”的现行规定；写入 dynamic 默认、例外清单、release账本、部分事件注册回滚和busy恢复合同。
- 托管构建后显式交接同次 DLL/TLB 至 Inkeys，再完整 solution 构建；检查 native 实际嵌入字节，不用旧本地资源验证新源码。

## 9. 验证与验收边界

自动化：
- dynamic-only 对象、typed QI失败的隔离IDispatch夹具、ITypeInfo不可用边界；测试生产adapter，禁止只测字典。
- 逐取得/异常/转移/释放账本、事件部分失败、ROT多候选替换、重复扫描、链式命令替代、清理与请求交错。
- 同场busy/跨页/跨文稿/恢复deadline、无事件完整生命周期、窗口缺失/歧义/复用、raw/descriptor迟到、末页Next保护。
- 现有managed/native纯逻辑与 --no-window；完整 Debug|ARM64 solution、ABI/产物、编码换行和diff审查。

实机（需另行授权GUI/Office启动，或由用户人工执行）：
- PowerPoint2010 x86/x64、WPP2013可得位数，PowerPoint2007保留项及现代Office对照；每个版本记录具体build/OS/位数。
- 事件可用与不可用两路、typed接口异常而dynamic可用、关闭再开下一文稿。
- 自有隔离Office会话，记录文稿关闭/应用Quit返回、受控引用归零、进程实际退出及下一文稿成功；有其他文稿/用户进程时不把进程存在等同泄漏。
- 禁止删除Office/WPS注册或kill用户进程模拟损坏；禁止用强制GC/等待自然回收把退出测试变绿。
- 无旧版或损坏设备时逐项标 NOT VERIFIED，不以现代Office/mock代替，发布兼容验收保持待办，必要时由用户明确接受剩余风险。

## 10. 审批内容

用户已经批准本设计与implement.md，并确认HWND是已测试过的包装例外。修复范围涵盖dynamic默认、确定性释放、owner串行化、busy恢复、基础控件降级、受验证窗口后备与验证矩阵。
批准后才 task.py start 和实施；GUI测试、commit、push不在本次默认授权中。
