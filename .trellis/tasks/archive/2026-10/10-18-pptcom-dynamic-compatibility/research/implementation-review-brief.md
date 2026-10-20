# 实施/检查上下文摘要

- 日期：2026-10-18
- 状态：2026-10-18用户已批准实施；开始前task.py start。GUI/Office启动及commit/push未授权。
- 权威顺序：用户本任务要求 → prd.md/design.md/implement.md → 本摘要 → 历史spec/研究建议。当前spec的统一InvokeMember条款被用户dynamic优先要求覆盖，实施时需同步修订。

## 用户不可变要求

1. 类型/PIA/事件异常但生产dynamic仍可用时，保住大部分核心功能；事件包装失败走轮询。
2. 常规属性/集合/命令使用dynamic，不把反射等同dynamic。仅逐成员有证据的能力例外允许包装。
3. 临时COM每次取得都具名、finally单次释放；赋值/as/cast不新增取得。不能靠GC/终结器/睡眠等待/killOffice使退出验收通过。
4. 保留.NET4、GUID/vtable/现有descriptor/session schema、现有PowerPoint2007/WPS2013+声明；重点核查2010/MSO14。
5. 最小diff、原编码/换行，中文关键注释，不改系统COM注册、全局配置、SDK/依赖。GUI/Office启动和commit/push均另需授权。

## 当前证据

- 当前20261014a相关源码与origin/canary相同；0813附近PptCOM.cs基线blob=baf935a7。
- 生产随附DLL故障注入确认：一次0x8001010A后legacy稳定缓存仍StableSlideIds，新session发布TransientBusy/Unknown；恢复后非force刷新仍受3秒deadline挡住。
- 另一注入：缺HWND可得到StableSlideIds/Valid、HWND0，随后native整体准入失败。
- 这不意味着可把旧stable重发为Valid；同场也可能换页，旧显示保留和新写入授权必须分开。
- 无事件重开风险：只有旧show为空/Active属性不能读才替换，旧show仍可读时新identity可能不安装；HWND0更难靠句柄识别换场。
- 当前Win32后备只按标题唯一，descriptor PID又是同HWND反查，不能据此声称已有独立COM→PID强关联。

## 所有权确定缺口（源行以planning基线为准）

| 路径 | 已确认的问题 | 拟修复不变量 |
| --- | --- | --- |
| PptCOM.cs:1132 + :1183 | ROT best A被B替换后A提前释放又在列表释放 | 列表持唯一候选取得；best只是选择；最终转移获胜项 |
| :1366 | bestApp同长期app identity时漏掉本次ROT返回 | 未转移的返回无条件单次释放 |
| :611/:638/:641 | 比较helpers同identity不释放新取得 | identity与ownership分离 |
| :1428/:1436/:1466 | 轮询新window按identity跳过，而非按实际转移 | 转移后置空临时槽，其余finally释放 |
| :1988/:2004/:2011/:2022/:2034 | View链式temporary | 命名View，finally释放 |
| :2044 | SlideNavigation链式temporary | 命名Navigation，缺成员只禁该功能 |
| :862 | 回调参数覆盖长期window | callback只提示，owner自己取得/安装；不盲目Release入站参数 |
| :1315-1344/:352-374 | 部分订阅失败未回滚，解绑首项失败阻断后续 | 各订阅记账，部分失败全清，轮询独立 |

reader已有主要finally形状要保留，不能为了替换反射破坏它。root与业务命令须收束到现有owner/mailbox，纯值getter不跨线程访问Office；锁外COM/取消事件/释放；超时只可靠取消尚未执行请求，已进入COM不可撤销。

## 访问例外/旧版本

- 内嵌PIA15元数据：SlideShowWindow.HWND带DispId2010和FRestricted；现代微软C# binder跳过受限成员，说明该标量需要窄例外。目标.NET4仍要实际dynamic/COM夹具验证，不能照搬现代runtime结论。
- 历史实机只证明typed HWND可读而InvokeMember报0x80020003，不是本轮旧版验收。
- SlideID在Office12文档已有，View.State/标准翻页/PointerType在Office2003文档已有；成员年龄无新增Office15门槛，不等于产品已通过旧版实机。
- WPS2013不能等同MSO15；现代WPS365 JSAPI文档不是WPP2013 Windows COM保证。
- 严重ITypeInfo/类型库异常可能连DLR一起失败；用生产运行时逐能力测试，不承诺dynamic修复所有COM。
- 本机仅找到Office16文件16.0.20430.20182、PE AMD64/x64；无2007/2010/WPP注册/文件证据，盘点部分HKCU权限受限。全部目标旧版实机=NOT VERIFIED。

## 最终拟实施决策（含对研究建议的取舍）

- ordinary成员改生产dynamic，事件保留包装，HWND按用户已确认的历史dynamic失败结论保留typed标量例外/可靠窗口后备；busy不触发有副作用重复调用。
- 同场busy保持画面/场次，页不确定暂停写入；独立≤500ms下一调度重试，不受稳定3秒deadline影响。fresh当前页+generation成立才恢复。
- 无事件按新取得show COM identity切换/推进场次；明确showCount0清旧root。Count缺成员不作总开关，可用fresh show.Presentation与View证据，失败不推断退出。
- 可靠窗口与页级墨迹仍有严格门；唯一标题、唯一Win32候选、process-local key都不能自己证明当场文稿归属。无COM进程锚不能伪造PID。
- 用户强调基本可用性，主会话决定保留现有分页控件的基础模式：COM场次可信但HWND未知时，基本页码/上一页/下一页/绑定对象退出仍可用；不新建窗口。token的HWND0不得被IsWindow误判退出。
- 基础页码须显式独立于PptInfoStateBuffer/Draw3 ready；该模式无pagePresented确认，不把旧Presentation墨迹附到新文稿；提升为正常墨迹模式仍走target→Present→UI ack。
- HWND未知时不做OS焦点/猜测定位；对象定向Activate须受提供方能力与会话检查。
- research/session-busy-polling-and-window-plan.md建议最窄范围暂不扩无HWND UI；主会话因首要用户目标采纳其能力隔离依据，并将“现有控件局部基础模式”正式纳入设计。research/legacy-version-compatibility.md矩阵对命令窗口前提的保守描述不覆盖本设计的对象定向控制决策。
- 现有ABI/schema保持不变；若后续必须新增跨ABI能力字段或更广UI，回到planning提审，不静默扩围。

## 验证与进一步证据

先读具体职责所需段落，不把全部长报告盲目注入：
- planning-baseline.md：历史、故障注入步骤/实际输出、程序集和环境事实。
- legacy-version-compatibility.md：逐功能旧版矩阵、HRESULT/损坏分类、官方源码/文档链接、受限HWND证据。
- com-ownership-and-dynamic-audit.md：完整逐笔账本、事件receipt疑点、owner/超时与真实退出测试。
- session-busy-polling-and-window-plan.md：生产路径、busy/轮询/窗口策略和时序回归矩阵。

批准后先补production adapter/隔离IDispatch+拒绝typed QI夹具、逐异常ledger、部分事件失败、无事件重开、busy跨页/跨文稿、窗口歧义/复用、基础模式绝不发墨迹ready的红例，再最小修复。
真实Office退出和下一文稿能立即打开，必须在授权的隔离/自有会话中验收；不用GC、kill或延迟自然回收代替。没有目标旧版本就明确NOT VERIFIED，不能以mock/Office16代替。

完整构建采用当前VS的ARM64原生MSBuild，InkeysRepo.sln Debug|ARM64，至少5分钟（拟900秒），同PowerShell先Remove-Item Env:PATH并设置MSBUILDDISABLENODEREUSE=1；托管DLL/TLB显式配套交接后再验证native。环境错误不靠改项目绕过。


## 本轮用户批准补充

用户确认以前测试过dynamic不能读取HWND，已使用包装器；HWND无需普通dynamic前置重试。只有此已证实标量和事件包装属于例外，其他成员仍dynamic优先。2026-10-18明确批准规划并要求开始实现。
