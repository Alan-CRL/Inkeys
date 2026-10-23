# 真实产品实屏夹具独立离线检查（2026-10-22）

本报告审查 `--scroll-follow-live-test` 的离线准备、安全门和真实产品接线，**不是产品实屏测试通过报告**。checker没有运行精确live参数、GUI、SendInput或截图。已将修复后源码/产物冻结交还主会话，再由其安排专用GUI运行；该后续结果须另用原始日志验收。

## 范围与上下文

当前任务 `.trellis/tasks/10-21-scroll-follow-persistence-regression`，基线HEAD `10abcac3`。已读更新PRD/design/implement/check JSONL引用、捕获与产品规范、原产品/保存检查、`live-product-test-design.md` 和实施者完整 `live-product-preparation.md`，核对实际 Business/IdtState/WindowService/Host 调用；没有按研究建议直接假定代码正确。

`get_context --mode packages`无packages，本轮影响native-desktop；index及可复用native Quality Check、跨层线程/呈现/生命周期约束已复核。本次不改生产接口、捕获512A正文、Tracker、输入后端、持久化格式、资源/工程、SDK或依赖，不commit/push/finish/archive。

开始时安全环境仍阻塞，checker只读；收到主会话明确capture进程已正常退出、桌面和构建交接后，仅修改新harness安全代码，完整ARM64 build和最小无GUI CLI。主会话确认用户再次手动解锁后，Capture实施者的实际生命周期日志 `Build/ScrollFollow/live-26916-212248453/result.log` 已记录20轮、2次Freeze、真实Read撤销及91个正确callback最终PASS；这解除前置门槛，但**不能代替本夹具的产品实屏证据**。checker仅读其末段，没有重复GUI或长核图。

## Findings（fixed）

所有checker修复只在新 `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`，没有触碰原Capture正文和其它生产源码。

1. **同次消息派发后的取消可能被true predicate掩盖。** 原Wait在Peek/Dispatch前检查取消，若WM_CLOSE/Esc/外来wheel在Dispatch中到达而predicate随后为true，就会返回成功，输入前CheckDesktop又只检查桌面。新增统一CheckFixture检查取消、source失败、state/mag线程失败及offSignal；Wait派发后再检查，CheckDesktop也统一检查，使刚到的取消不能继续进入输入阶段。
2. **发送滚轮前只检查旧固定center。** 先前确认鼠标移动后，滚轮前检查WindowFromPoint(center)，不能代表此时实际cursor命中。修复为读取当前cursor、要求仍在center两像素内、当前命中精确是本次source且foreground精确同source。输入阶段增加 `before-os-input` 的完整99点+逐像素专用ROI GT，不保存额外图。任何检查失败直接抛出，不用PostMessage/SendMessage或其它目标补发wheel。
3. **安全异常清理未及时关闭旧显示事务。** 原BeginShutdown在state/Mag/Host join之后，StateMonitoring已经进入本轮的显示请求可能继续提交。检测安全层或输入桌面不可用时立即调用WindowService::BeginShutdown（只设置原子门，非owner等待），阻止排队Show/Create/非Hidden的pair请求；Hide/销毁仍能排空。清理等待中若才遇安全层，最终再次设result4及门关闭，再做Shutdown/joins。正常Off保留真实反向ACK流程，没有提前关闭其显示事务来伪造成功。

修复后完整构建和最小纯CLI通过，源码/构建/测试冻结空闲已明确交主会话。没有启动或重试任何安全门来验证这些保护分支，因此保护分支运行覆盖仍未完成。

## 实际执行路径审查

### 最早门、私有数据与原线程

Main最早whole-argv表追加第六项，只有精确单参数live允许进入；其它suffix/错位/重复/mixed都exit2。harness-off分派和inl stub均返回2。新Product/Business/IdtState头在Window import之前，避免共同退出凭据重复定义；inl只在IdtMagnification.cpp末尾包含，不增构建系统或第二次编译单元。

Run入口最前检查安全层，早于Display/COM/source/所有产品HWND。已知安全层只读class/进程元数据，不读取标题、隐藏/移动外部窗口或改系统安全。后续等待和screen/input前复核，失败停在专用进程，不普通启动应用、配置、registry/update/PPT/Hook或UI3。

root在仓库Build/ScrollFollow下用PID/tick create-new，每级父目录拒reparse，核仓库标记和路径预算；log为CREATE_NEW。globalPath临时指向同私有root，Host autoSaveRoot显式指定其AutoSave，不读写用户配置/文档。内存默认setlist变更仅此专用进程，最后恢复globalPath/logger。

原Mag beforeCreate/host/child/shutdown回调、原MagnifierThread与readyPromise设计都保留。把Mag.cpp本次3个头和末尾inl include去掉后，原始字节精确恢复为捕获冻结SHA256 `512a650ea9007c9cc5080b51cd0d4a9af4f02c04e05c6d60b13d29758f147dd0`，没有更改其20轮或生产线程。

### 真正Business和系统pair ACK

使用唯一Window::GetService创建原Mag pair、真实Drawpad/DrawpadPresentation及隐藏Setting/Freeze/DisplayObserver。唯一StartProduct附着同两个HWND，原DrawpadMsgCallback和真实WindowService样式回调，没有强制鼠标backend或改渲染参数。

启动真实StateMonitoring和原MagnifierThread；真实ChangeStateModeToPen/SyncDraw3State建立工具。控制仅RequestToggle/RequestStop，不安装Business TestPorts，不直接调用Capture Start、Tracker、Follow Begin/Publish/End/Handoff，不写projection/ACK，也不调用隐藏contact注入。

实际Service::Tick在F0初始化后发布projection，再等ScrollFollowPresentationAcknowledged才Tracking。StateMonitoring消费dirty并发布真实bridge，通过Host ready输出及匹配revision/projection/tool检查后调用单例WindowService::SetDrawpadSurfaceVisibility；其返回成功才写真实ACK，Acknowledged还复核projection/mode/bridge，不是夹具布尔。

首例空页有意不画A。生产selection规则在空页/full-clean时是 **pair=(0,0)**，因此Tracking后两窗隐藏正确；它不验证有墨迹时Presentation可见。Off须真实反向ACK恢复 **pair=(1,0)**、Channel unblock/Complete/handoff、final已Applied、Capture occupied=false、工具revision与contentRevision不变。

初始ScreenSourceGt在Product前验证源；Pen主窗就绪后再验证透明结果，进入Tracking后再验证输入前源。固定世界坐标纹理、source收到OS wheel后真实repaint/QPC/hash是外部GT；不会把源offset写入Tracker/Follow。只有实际捕获像素驱动跟随。

### 输入只限专用source

只激活/调整本次source和本组root，不操作外部窗口；不能取得自身foreground或GT就失败。SendInput只有一次absolute鼠标移动和一次负wheel；wheel前严格检查安全/取消/失败、source foreground及当前实际cursor命中。source以extraInfo tag记录真正WM_MOUSEWHEEL并改变自己的offsetY16，外来wheel取消。检查失败后没有向其它窗口发wheel的fallback。

这些是发送前的真实条件检查，不是把全局OS输入原子绑定某HWND的公共API，也不提供抵抗检查与发送间外部抢焦点的普适证明。本例仅允许在专用、无人干扰的短测试条件下执行；不能据此扩大到个人应用自动输入。

ScreenSourceGt的99点是全屏抽样，保存的 `(64,64,256,192)` crop另做49,152像素精确比较，只保存确认属于专用纹理的画面；任何未知内容不落盘。首例无墨迹，保存图不能证明墨迹跟随或消失帧。

### 清理、资源和watchdog

正常路径先等待真实Business Off/逆ACK，再RequestShutdown、offSignal、StateMonitoring join；之后StopProduct真实Host drain、停止原coordinator/MagnifierThread join、WindowService Shutdown/StopAndJoin/Uninitialize，最后销毁source、DIB/Display/COM/log。state/magReturned只在原函数真正返回后置位，成功结果核两者及Product/Service/Mag/Capture停止。startup callback携带的f到StopProduct和owner joins后仍有效，source GWLP_USERDATA也到DestroyWindow后才释放。

安全异常路径先原子关闭显示门，再单调Shutdown，跳过正常Off显示恢复等待；不会用手工ACK硬解结束。没有新增Business worker join API，其原静态Service析构/jthread寿命保留，不能把Snapshot Off冒称其thread join证据。

90秒watchdog只结束GetCurrentProcess，明确exit3失败兜底，不能把它当同步PW有界返回或正常资源清理。没有按名称关闭其它Inkeys进程。

## Findings（not fixed / 结果边界）

- checker没有跑产品实屏：Tracking、真实OS wheel、viewport误差、Off、清理和安全异常均无本报告直接运行证据。后续core运行必须以新日志/最终exit和hash另行验收。
- 首版只有空页一次16px垂直wheel，不包含墨迹A/B、Pen/RTS接触、有墨迹Presentation、最终文件读回、长滚动/反向/水平/特征relay、Bar/快捷键/设置/白板/PPT、应用场景及硬件/旧系统验收。
- **最终数值须独立复核：** 夹具settled阶段已验证至少2个不同seq的total/viewport在GT两像素内，最终Off断言确认Complete/Handoff/finalSeq Applied，但没有再次把final.viewport直接比较GT。源码已冻结交GUI后才发现此断言范围差别，已立即通知主会话：运行结束须用bounded_case/source_delta/最终facts独立确认finalX=baseX、finalY=baseY+16，误差<=2，避免中途正确而End后漂移被漏判。此点不是已观察到生产漂移；若最终原始值正确，可在该次离线验收补断言，不将中途settled单独当最终正确。
- 全局SendInput的发送前条件与外部抢焦点没有原子绑定；不得把专用fixture的一次路由当作任意桌面安全自动化保证。
- Release/harness-off、x64/PE未在本轮重编；guard静态一致。无新增非Win7硬导入、WGC或capture affinity属性。算法/保存/完整headless未变化，未重复长矩阵。

## Verification

### 构建与环境

实施者初次product-live-first exit1/17.968s，第一个有意义错误为新inl的依赖头放在import Window之后导致FailedCleanupSignal重复定义，已在checker接管前修好顺序；这是源码接线问题，不是SDK环境，不改第三方绕过。

checker安全修复后复用原 `Build/ScrollFollow/build-motion-tracker.ps1 -Round product-live-offline-check`，vswhere定位ARM64原生MSBuild，同PS规范Path、禁node reuse，完整 `InkeysRepo.sln Debug|ARM64`，normal host，10分钟预算。**exit0/23.627s**，只有原hashlib++3条C4267，无新增警告或本轮ILK/环境故障。日志 `Build/ScrollFollow/motion-tracker-build-product-live-offline-check.log/-console.log/-stderr.log`，stderr空。

### 纯CLI、static、格式

使用已审 `product-live-stage/run_product_live_offline.ps1`，明确拒绝精确单live。结果 `Build/ScrollFollow/product-live-stage/offline-212551187/result.json`：

- 8个suffix/misplaced/trailing/duplicate/mixed：全部exit2，stdout/stderr0B，114–764ms。
- `--scroll-follow-protocol-test`：exit0/309ms，stdout0/stderr142B，真实Tick/Frame/Tracker控制矩阵无HWND，外部Capture/Host/ACK仍是其原seam。
- `--state-mode-production-test`：exit0/110ms，stdout0/stderr468B，原工具/shortcut/clear等纯生产合同通过。
- scoped git diff --check exit0。既有3文件UTF-8 BOM/CRLF；Main原无EOF newline保留，其余有；新inl保留UTF-8无BOM/CRLF和末尾换行，bareLF均0。

Lint/static：上述范围pass。C++ TypeCheck/完整ARM64：pass。无GUI定向：pass。产品实屏：本报告未运行。没有把授权、构建或纯协议seam称作实屏通过。

| 本次冻结产物/源码 | SHA256 |
|---|---|
| Build/ARM64/Debug/Inkeys.exe | 77e0f580b5ac155e930335a212a48088efaefa4d448fbca00de9f5e08c1d96c1 |
| Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl | 0da675dd3db97b290d01e3d4b01e57bf4beb1f1dbc7241f4fafe96acc47b7d74 |
| Inkeys/IdtMagnification.cpp | c8b1651708f2ef9ece15b577912195bf4c39ed2cdc6e768f1e1490e774c2f16a |
| Inkeys/IdtMagnification.h | 662efc46ca1dffb090ba0f275ffc1c44a9f7ec107f375411a47198a35091d8ec |
| Inkeys/IdtMain.cpp | 0bf398a0c3a31a7f0de1c0c8d1340c3169942eca81694115872dd74dab8c639a |

本报告对应77E0...binary。Capture生命周期PASS使用的是此前F84B...binary，不能称它运行了本次安全修复。源码/build/test已冻结空闲并交还，checker之后只离线写本报告；主会话保留任务in_progress，按实际新日志继续验收。


## 同日后续快速复审：专用source激活和最终Off断言

本节为主会话再次交接的只读复审，不覆盖上文77E0...的实际build/CLI记录。后续core拥有源码/build/GUI，checker没有并发修改、编译、运行CLI/GUI或截图。

首次真实产品尝试由core执行，主会话记录exit1/2.748s：`own-source-foreground` 在StartProduct前失败，尚未source屏幕GT、正式Capture或OS输入；security0且清理正常。这不证明Mag、Tracker或真实ACK失败，仅说明当前过程不能直接取得自身source前台。

主会话明确授权最小后续修复，core修改后的inl SHA256 **35c6902897c973fd96cd163d5f0a7a106d36e561161dd4763b34ae668bb624a7** 已由checker重读并核hash，scoped diff检查通过：

- SetForegroundWindow失败只记录，之后调整本组顺序并先通过source屏幕99点/逐像素ROI；没有GT就不会输入。
- 激活前检查安全/取消/失败、固定center命中本次source，OS移动后再检查实际cursor仍在center两像素内且命中source。只针对自己创建的全屏内容，不使用ALT/AttachThreadInput或修改外部窗口/系统前台限制。
- 先检查用户左键未按住，再单次SendInput插入tagged Down；成功后置ownLeftHeld。Up独立发送，最多3次有界尝试；成功才撤自己的凭据，失败明确held_unproven且不能PASS。异常清理仍仅归还自己成功插入的Down，不追加Down、不释放未归属自己的用户按键。Up的accepted表示OS接受该注入，不夸大为硬件输入完成证明。
- 激活后实际cursor命中和foreground必须精确为source，失败退出。单击发生在StartProduct之前，**不能算Pen/RTS输入或墨迹证据**。此版本每次都走该有限自建source单击，不只是API失败时才点。
- 上文“最终Off数值缺少直接断言”已在这次修正中关闭：final viewportX与baseX、viewportY与baseY+16均要求误差<=2，source delta精确(0,16)，clamp=false；Off后还要求source屏幕GT并保存经验证的专用ROI。原Complete/Handoff/final Applied、工具/content不变及真实逆ACK条件继续保留。

这份新diff没有发现阻止有限专用GUI运行的安全缺口。checker给主会话的放行条件是**该版本完整ARM64 build成功后**，继续按用户授权串行运行；不是未经编译就执行，也不是产品结果预先判通过。core当时正在 `product-live-own-click-lifetime` 构建，最终build/hash/CLI和后续GUI结果须从其独立记录确认，不能套用上文77E0...artifact。
