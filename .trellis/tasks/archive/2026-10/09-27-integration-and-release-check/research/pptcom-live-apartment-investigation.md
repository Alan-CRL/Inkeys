# 真 Office 联动与 native COM apartment 调查（2026-09-29）

## 事实与复现边界

- 本机 PowerPoint 16.0 在正常 Windows COM 权限下可创建并退出；两轮仅使用 `TestResults/release-hardening/ppt-office-own-*` 自建 EXE/PPTX/PID，实际 SlideID 为 256/257、放映页 1→2→结束页 3。默认沙箱激活失败 `0x80080005` 是环境条件，不能归咎产品。第三轮可见编辑窗口且 `Marshal.GetActiveObject("PowerPoint.Application")` 返回真，说明 Office 已在 ROT。
- 三轮 Inkeys ARM64 Release 均完成 Bar/Drawpad/RTS/COM资源启动并经自有 Drawpad WM_CLOSE 退场；第三轮在隔离配置打开 `Experimental.Inkeys3.ConsoleOutput.PptCOM=true`、环境 `INKEYS_PPT_TIMING=1`/`INKEYS_PPT_EXIT_TRACE=1` 后，标准流仍无托管 `PPT Monitor ReStarted`、无 `[PptSync] target_published/canvas_presented`。只记录到 `PptExitSurface session=0 target=0`。这是 **Inkeys 未证实接入放映**，并非凭空证明每页/结束页已保存；没有用户墨迹输入。
- 源码 `IdtMain.cpp` 只在主线程调用 `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`；`PPTLinkageMain`、其新建的 `GetPptState` 和 `PptInfo` 线程没有本线程 COM 初始化。`GetPptState→CheckPptCom` 在该线程调用 COM `CreateInstance(_uuidof(PptCOMServer))`；`PptInfo` 在另一线程对其快照做 `QueryInterface/GetSlideShowState`；`PPTLinkageMain` 本线程处理业务 COM 调用。`StatusGuard` 只登记线程活跃，不初始化 COM。微软 [COM 初始化文档](https://learn.microsoft.com/en-us/windows/win32/com/the-com-library) 要求**每个使用 COM 的线程**调用 `CoInitializeEx`，否则 `CoCreateInstance` 可报 `CO_E_NOTINITIALIZED (0x800401F0)`。`CreateThread` 继承当前 activation context 的 [微软说明](https://learn.microsoft.com/en-us/windows/win32/sbscs/using-threads--asynchronous-procedures--and-window-messages) 不等于继承 COM apartment。

## 最小设计与验收顺序

1. 先给 `CheckPptCom` 的 CreateInstance/CheckCOM/Initialization 失败点加**仅显式环境开关**的低频数值 HRESULT 诊断，不输出文稿名/路径/墨迹；完整 Debug|ARM64 Solution 编译，再用同一隔离 PPT 轨迹取红错误码，确认是 `CO_E_NOTINITIALIZED` 还是别的 COM/ROT 条件。不得仅凭“没日志”改 apartment 或把 Office 能放映当 PPT 联动通过。
2. 若红证确认为本线程未初始化，在三条真正使用 COM 的 native 线程各用 RAII `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`，仅成功（含 `S_FALSE`）时在该线程所有 COM 智能指针销毁之后 `CoUninitialize`；`RPC_E_CHANGED_MODE`/其它失败记低频诊断并不继续无效 COM 调用。沿用主线程 MTA、既有 activation context、PptCOM GUID/vtable/ABI、业务队列和 COM owner，不在 UI/render 线程同步新 COM 工作。若 Office event 需要 STA/message pump，不擅自切模型，先取得调用证据再另设计。
3. 绿证需相同 PowerPoint 16.0 隔离轨迹实际打印 `PPT Monitor ReStarted` 与 `[PptSync]` 的 descriptor accepted/target published/canvas presented/page UI ack；分别核 SlideID 256/257、结束页不伪造 SlideID、退出后目标撤销。若只能看到 COM 创建成功而 page target 不发布，继续按当前调用链追首个错误，不把这一单元冒充通过。最后跑 `InkeysRepo.sln Debug|ARM64`、Release可得架构、Headless/PptCOM.Tests/相关无窗探针，和独立最终diff复审。

本次测试不会触碰真实 Office 文档或已有进程，工作目录和配置仅在忽略 TestResults；无 PowerPoint/WPS/Win7 SP1仅KB2670838 的其它组合，仍保留人工门禁。

## 2026-09-29 红诊断后的假设修正

上文“未见 COM 初始化→CreateInstance 可能失败”只是事前假设。临时仅显式开启的 numeric HRESULT 诊断在完整 Debug|ARM64 Solution Build0 后进入第四轮隔离 PowerPoint 16.0 测试：PowerPoint `GetActiveObject`/ROT 可见，日志没有 `[PptComActivation]` 错误；产品既有 `INKEYS_PPT_EXIT_TRACE=1` 反而记录 `edge=inactive session=1 service=7 show=1 binding=1`，证明 PptCOM 服务至少检测并建立了放映会话。微软文档说明新线程可继承创建者当前 activation context；仅静态未发现显式 `CoInitializeEx` 不能推导本轮服务创建失败。**因此没有实施上文设想的三线程 COM apartment 重构；用于证伪的临时诊断已从 `IdtPlug-in.cpp` 撤销。**

第四轮 `ppt-office-com-apartment-red-arm64.log` 的 Office 页为1→2→结束3，产品 `PptExitSurface` 在结束前 runtime workspace=Presentation，但 `ready/uiReady` 和 `session/target` 仍为0，标准流无 `[PptSync] target_published`。这收敛到 descriptor/target 接受与画布 ready 交接，而不是直接指向 COM CreateInstance。下一步应先复用现有 `INKEYS_PPT_TIMING`，只在 descriptorChanged/目标解析失败时加一次**无文稿路径**的数值状态诊断，核 `stateReliable/lifecycle/pageStatus/rawPage/rawTotal/descriptor.status/currentPage/totalPage/slideIds.size/showWindow/ownerPID`；若红证定位具体逻辑再做最小修，不能凭没有成功 trace 放开身份门。真Office/Win7/未持久化墨迹仍未验证。

## 最终本机真 Office 结果：目标与结束页已证实

后续两轮在 `IdtPlug-in.cpp` 只加 `INKEYS_PPT_TIMING=1` 时的**数值身份门/状态交接**诊断（不记录路径、标题或墨迹）；完整 `InkeysRepo.sln Debug|ARM64` 每次构建 exit0。第五轮 `ppt-office-gate-reject-arm64.log` 核 COM lifecycle active、Office owner PID 与 descriptor PID 相等、稳定拓扑 `ids=2`、页号与 raw 1/2、2/2、EndScreen 0/2 对齐、白板未占用、Host running；当时缺 `PptSync` stderr 是 `InitializeDebugConsole` 重新绑定 Debug 控制台后的**观察位置变化**，不能据此断定 `PublishProductPresentationTarget` 失败。

第六轮 `ppt-office-gate-ready-arm64.log` 中唯一自建 test root `TestResults/release-hardening/ppt-office-own-7b879a9b762a413eac7489c409d9d883/` 的 app log 给出生产目标和成功呈现后状态：

| Office 动作 | 产品观测 | 限定结论 |
| --- | --- | --- |
| slide 1，Office SlideID 256 | `publish_accepted session=1 revision=1 pageKind=0 pageIndex=0 slideId=256`，约47ms后 `document_ready ... presents=4`，约4ms后 `page_ui_ready` | native 稳定页1身份、Draw3 文稿和 UI ack 实际连通 |
| slide 2，Office SlideID 257 | revision2/pageIndex1/SlideID257，约30ms后 document ready `presents=6`，约13ms后 UI ready | 同会话第二页身份未误用页1 |
| 放映结束页 | revision3/`pageKind=1 pageIndex=2 slideId=0`，约31ms后 document ready `presents=7`，约9ms后 UI ready | 独立结束页不冒用最后真实 SlideID |

这些毫秒是从单次日志时间差得到的**观察值**，不含真实笔输入和重复样本，不作为性能 median/P95 或光学延迟。Office COM 脚本自然完成 show 1→2→3→Exit、自有 Inkeys 与 PowerPoint 随后均退出，只有忽略测试目录数据被创建；没有操作用户既有进程/文档。当前可写为“本机 ARM64 Debug 隔离 Office 会话的身份/ready 自动验证通过”；真实墨迹分开保存、退出 durable UInk、跨进程可见恢复、WPS、Win7 SP1仅KB2670838 及 Release 构建配置仍未由此测试通过。临时的 CreateInstance失败诊断已撤销；保留的 opt-in 数值 gate/accepted/ready 诊断默认关闭，供后续独立复核。
