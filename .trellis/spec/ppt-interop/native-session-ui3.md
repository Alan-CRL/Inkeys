# PPT 会话、UI3 与可写页合同（2026-09-25）

## 1. Scope / Trigger

修改 native PPT 消费、UI3 位置/缩放/主栏场景、显式退出或 Draw3 页切换时适用。本页取代旧文档中把 `IdtDrawpad/PptImg` 视为生产页加载路径、把控件隐藏视为结束、把 dispatch 视为保存成功的描述。实际生产入口为 `IdtDrawpadFacade` + 单 Draw3 Host；旧键盘转译源码是 vcxproj 的 `None` 项。

## 2. Signatures

- 新独立 IID `D7A63F98-43B8-4BA1-A875-B55A5A04DC3E`：`IPptCOMSessionState.GetSlideShowState(long afterRevision)`、`EndSlideShowIfSession(long expectedShowSessionRevision)`。旧 IPptCOMServer 的 IID、顺序和 v1 descriptor 不变。
- 状态 envelope 恰含 `schemaVersion/stateRevision/showSessionRevision/bindingRevision/lifecycle/pageStatus/descriptor`。生命周期 `Active/Inactive/Unknown`；页状态 `Valid/EndScreen/Unknown`；descriptor 仍由原严格解析器校验。
- `Ppt::PublishSession(uint64_t,bool,HWND)`、`PublishPageState(int,int,uint64_t targetRevision=0,PageStateMode=InkReady)`、`ResetPositions()`；业务回执 `pagePresented(session,target,current,total)`。
- `PublishProductPresentationUiReady(PresentationReadyIdentity)`、`SetProductPresentationInputSuspended(expectedIdentity,bool)`；target/ready 均含 `sessionRevision` 与 `PresentationPageKind`。Slide 用真实 `pageIndex<N`/SlideID；EndScreen 用内部 `pageIndex=N`、无 SlideID，`totalPages=N` 不变。
- `MessageBox::FallbackPolicy.enabled` 默认true，本入口置false；`MessageBox::IsShowing()` 覆盖排队/模态及回退调用期。`Bar::SetEndShowRequestCallback(function<void(uint64_t)>)` 与 `CompleteEndShowRequest(requestId)` 按请求配对。
- `TracePptTiming(stage,session,target,observedQpc=0)`；环境变量 `INKEYS_PPT_TIMING=1` 开启，默认关闭。原生 `[PptGate]` 仅输出数字/布尔：`sessionActive` 是活动状态、`sessionId` 是 `localSession`、`trustedTarget` 是可信目标布尔；`publish_accepted/document_ready/page_ui_ready` 用同 `sessionId` 与 target revision 关联。日志不写文稿路径、标题或墨迹，单次阶段差不是光学或 P95。
- 真退出收尾使用 `StateModeTransitionRevision()` 与 `ChangeStateModeToSelectionIfRevision(expected)`；窗口事务使用 `Window::Service::SetDrawpadSurfaceVisibility(visibility, stillDesired={})`。`stillDesired` 在窗口 owner thread、释放 Drawpad capture/更改显隐之前同时校验 bridge revision 与工具模式 revision；false 不触碰 HWND，调用方按最新状态重试。`INKEYS_PPT_EXIT_TRACE=1` 开启限频退出诊断，默认关闭。

## 3. Contracts

### 状态来源、调度与身份

- COM getter只取owner发布的纯值缓存；相同revision返回空字符串。事件只作轻量代次/唤醒提示，owner负责常规dynamic读取和命令，不能在getter/事件handler跨线程扫描长期root。descriptor恢复优先于慢维护，busy重试不受稳定3秒复核deadline挡住。
- 可信COM活动场次与可信窗口/墨迹target分开：HWND=0不取消已证实的COM场次，不用IsWindow(0)推断退出。基础页码和对象定向上/下页、同场退出沿用现有控件；没有可信窗口时不抢焦点、不猜测OS目标。
- HWND保留用户已确认dynamic读不到的包装标量例外；Win32后备必须有关联文稿/提供方进程/放映窗口的证据，标题或候选唯一不是独立身份。页级墨迹和OS焦点仍验证非零窗口及当前场次，不能只用固定HWND/PID的FakeAccessor宣称真实兼容。
- 基础控制页码不代表画布已加载，不写PptInfoStateBuffer、不发送pagePresented/Draw3 UI-ready；以PageStateMode的Pending/BasicControl/InkReady显式区分发布模式，不能凭targetRevision=0假装ready。提升为可信墨迹目标仍走完整target→Present→UI ack，旧Presentation目标必须隔离。
- UI内部pageAckEpoch随页码、target或mode变化推进，相同publication幂等；该代次必须来自实际PageControl渲染快照，经commit tracker传至五参内部回执。Ppt在同一stateMutex内校验代次并转发原四参native纯值回执，防止同数字同target的Pending→InkReady恢复接收旧帧。它不改变COM/Draw3 wire schema。
- 无事件时比较本次成功取得show的COM identity；旧对象的Active属性仍能读取不代表仍是当前show。更换时明确转移窗口所有权并推进场次，明确showCount=0后废弃旧show。Count缺成员可由fresh show文稿identity与View/页证据判断，属性失败本身不证明结束。
- 每场放映有独立 session revision；begin/end/读取候选存在 generation 屏障。旧事件、迟到候选不能复活旧会话。异常和属性缺失属于 Unknown，不凭失败猜测结束；明确 View.State Done 才分类 EndScreen。
- `View.State==5` 的 EndScreen 可在 `View.Slide` 不可用时读取同一次 `Slides` 的完整、唯一、正数 SlideID 拓扑；descriptor 保持旧 schema、`currentPage=0/currentSlideId=null`。忙碌/坏拓扑为 Unavailable/TransientBusy，绝不把部分 SlideID 或普通 Unavailable 放入结束页目标。State 3/4 的临时黑白屏不是结束页。
- native 新路径在放映期有界16ms观测，未放映100ms；旧 DLL 放映兼容路径保留50ms。16ms 是调度策略，不是实测端到端延迟承诺。必须保留 descriptor/原始页一致性、文稿 key、binding/target/session revision、SlideID/index 校验。
- runtime snapshot 在字段前取得 wait revision；等待必须使用判定前的版本，不能在决定等待后重新取版本。Host 重启不能复用旧 target revision。
- 可见性与生命周期分离；白板覆盖仅隐藏控件并拥有其工作区，不保存位置、不重放 PPT 进入/退出。临时无效页只暂停写入；不同 binding 的未知目标隔离。若已捕获的show HWND确实销毁，或成功查询到其拥有PID已改变，即使COM为Unknown也结束旧会话；不能以暂时隐藏或失焦作同样判断。

### Draw3 页边界

- target 接受后关新 contact admission；绘制线程收尾已接受持久笔迹到旧页，保留 FIFO 已接受命令及保存边界，取消 Laser，停止重连/惯性。未抬起的物理接触隔离至真实 Up/Cancelled，不允许跨页续写，也不无界等待 Up。
- 文档切换和成功 Present 是不同阶段。只有 replay + Present 成功才发布 identity-ready；页码随后带同一 target 发布。可见 PageControl surface 全部成功提交（没有可见控件则直接回执）后，匹配当前 target 才开放新输入。
- Unknown 暂停后清除旧 UI-ready；恢复同一页也须取得新回执。完整、可信的 EndScreen 则走独立 target→页槽切换→成功 Present→`-1/N` UI 提交→输入开放，版本与普通页同等严格。旧 UI ack、load/save 完成和 Host 重启前结果不能重新激活旧目标。
- 真正退出放映时，PptInfo 的可靠 Inactive/已销毁或复用 HWND 边沿发布 Desktop，并以退出检测时的工具模式 revision 有条件地调和 Selection（即使工具枚举本来是 Selection）；后来的用户 Pen/Shape/Eraser 修改不能被旧退出覆盖。等待 Desktop Selection 输出帧期间保留旧可见表面并重试；最新工作区/模式/输出与内容帧就绪后成对交接，有墨迹走辅助穿透 ULW，空 clean 页允许隐藏画布而主栏保持。条件 Selection 成功后再次更新 Bar 样式/渲染；退出的 Desktop 就绪交接立即刷新根，较新工具不被旧退出覆盖，也不跳过该层级恢复。不能靠非 layered 主 HWND 的 `WS_EX_TRANSPARENT`、AdmissionBlocked 或 MA_NOACTIVATE 推断 USER32 穿透。白板、Unknown、结束黑页和同次 rebind 不触发此收尾。
- EndScreen 的 UInk 是同一文稿文件中 N 个正常页之后的独立第 N+1 个 active canvas：`pageIndex=N/pageNumber=N+1`、单个 `inkeysPageKind=end-screen` marker、无 SlideID、独立 pageGuid/history。真实 `slideIds`/COM 总数/索引 ID 列表仍只有 N 项。codec 仅对这个标记例外，严格应用导入拒绝重复/歧义；旧 N-canvas 文件冷读补空结束页。重排/增删页按 SlideID 投影正常页，结束页 pageGuid 随文稿保留并移至新的内部 N。
- parked/warm slot 先恢复旧 target，再比较有序 SlideIDs；普通 pageIndex/当前 SlideID/targetRevision 变化不是拓扑变化。

### 位置、保存和有效缩放

- facade 拥有运行偏好；PageControl 候选和成功提交按 session、layout epoch、pair version 交接。全局 direct-move revision 只用于过期帧门禁，不是另一组的提交版本。旧完整快照在 ownership 释放后也不得覆盖新位置。
- 拖动永不触发写盘。开关打开/关闭瞬间保存；记忆开启时真正结束放映或正常退出软件保存；关闭后保持冻结位置。每场进入恢复最近一次明确保存请求的冻结基线（当前进程可含尚未完成的请求），新进程恢复实际磁盘值。重置无论开关均更新默认值。
- 冻结请求基线与 durable saved revision 分开，不能将待保存称为已保存。两个业务队列统一版本仲裁；旧 JSON 跳过。临时文件完整写入/flush 后原子替换；失败保留原文件和原请求重试，不采集后续临时拖动。
- DPI、用户缩放、最终有效缩放分别归一化；最终值传入 Scene 后不重复 DPI 上下限裁剪。绘制、文字、命中、阴影、presentation 使用相同值。主屏布局边界与 DPI 来自同一显示快照；运行适配不写保存偏好，backing capacity 不决定呈现大小。白板继续独立布局。

### 主栏、确认和焦点

- 进入时展开低栏变居中低栏，主体下沿距放映屏幕底边5 DIP；普通展开保持；收起保持位置，原低栏解除约束不跳变。真实退出时展开居中并恢复工作区底边；收起保持。只在 session 边沿应用，白板场景优先。
- 主栏和四分页控件不互为位置障碍；共享光标光效接收区域不属于排斥逻辑。
- 仅主栏 EndShow 点击确认。PageControl 结束图标/滚轮/长按及原生 Esc/Office 退出不新增确认。请求携带 session 和 request id，过期完成不能解除新请求。MessageBox 在业务线程且不持渲染/窗口锁，默认取消、关闭取消、失败关闭，不走系统回退。
- 确认执行时再次校验managed session。返回1才代表匹配目标的Exit调用成功；0为过期/状态不匹配，-1为失败/超时。尚未执行的超时请求取消后不得迟到执行；已经进入Office的COM调用不能靠等待超时撤销，不得盲目重发。COM对象定向退出不以HWND非零为共同前提，但取得View后仍需核对同场generation/对象；取消、失败或新会话不改变模式。
- 焦点只在有效进入和明确业务动作后交接，验证 HWND/PID/当前会话/前台；设置、颜色编辑和任何 MessageBox 请求期间不抢焦点。绘制、拖动、渲染和轮询不调用焦点转移。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| remember off 拖动后页码/显隐/设置刷新 | 保留运行位置，保存基线不变 |
| end/toggle 保存未完成立即重入 | 恢复冻结请求基线，durable revision 不提前推进 |
| 文件替换失败或旧 Settings payload 后到 | 保留原字节、当前偏好及对应重试；旧 payload 不回写 |
| DPI2 × user2.5、最终<.5 | Scene、背景、控件、命中和呈现同尺度 |
| 按住笔/鼠标/触摸切页 | 旧页收尾，旧 contact 后续样本不写新页 |
| 画布成功但页码 surface 失败 | 新页输入保持关闭，成功重试后才开放 |
| 同页暂停再恢复 / Host 重启 | 旧 ack 无效，重新呈现/回执，不复用旧 target revision |
| 新场次在旧确认框期间开始 | 旧请求拒绝，不退出新场次、不切新场次模式 |
| 白板覆盖 / 临时 descriptor 失败 | 不触发正常 PPT 退出或位置保存 |
| 退出检测后用户主动切 Pen/Shape/Eraser | 模式 revision 已变化时旧 Selection 收尾不覆盖新工具；窗口 owner thread 旧显隐命令在释放 capture 前因 bridge 或工具模式 revision 不匹配而拒绝 |
| Selection 输出未 ready / Present 失败 | 保留旧已呈现表面，等待最新目标帧并重试，不主动双隐藏、不伪造 ready；旧主窗保留期不承诺系统穿透，最终以辅助帧完成交接 |
| EndScreen 已确认并成功 Present / 拓扑暂不可用 | 前者才用独立 `N` 槽和 `-1/N` 同 revision UI 回执开放输入；后者保留安全待定且不冒充普通页 |
| 无事件且同 HWND 的结束重开完全落在观测间隙 | 外部可观测性限制；必须设备验收，不能宣称仅缓存已证明该提供方全部场景 |

## 5. Good / Base / Bad Cases

- Good：记忆打开→移到顶部→关闭并冻结→临时移到底部→重入恢复顶部；重置关闭态也持久更新默认。
- Base：相同目标和页码 heartbeat 幂等，不重复布局动画/descriptor 解析/磁盘保存。
- Bad：FinishDragPersistence 当成磁盘完成；等 active.empty 才发现换页；取一个更新的 revision 去等待已经发生的 ready；只先显示新数字而仍写旧页。

## 6. Tests Required

- 生产 PositionState/WriteJournal 与真实临时文件失败测试：旧完整快照、交错 pair、session/epoch、freeze desired vs durable saved、失败重试。
- 实际 BarSurfaceScene offscreen 绘制/像素/命中与白板比例测试，不能只验证重复公式。
- ContactInput 并发 Up/retire/admission、Draw3 hidden held-Down→switch→Present→UI ack→old Up→fresh Down、late ack/暂停/Host reset/warm重排。
- managed owner/cache/退出及 acquisition-release ledger；native envelope 复用 descriptor parser，过期会话/请求确认状态机。
- 完整 Debug|ARM64 Solution、headless、hidden/offscreen、托管测试、i18n、diff check。真实 Office/WPS/设备与延迟/CPU 另列实际条件，未跑标 NOT VERIFIED。

## 7. Wrong vs Correct

~~~cpp
// Wrong: 刚发生的 ready 已被这个新基准吞掉，还要再等一个采样周期。
if (!ready) WaitForProductRuntimeRevision(ProductRuntimeSnapshot().runtimeRevision, 50);
// Correct: snapshot 在判断之前捕获，通知发生在任一侧都能保留进展。
const auto before = ProductRuntimeSnapshot();
CheckTargetAndPublishPage();
WaitForProductRuntimeRevision(before.runtimeRevision, timeout);

// Wrong: 隐藏分页就当作真正结束；mem-off 松手也丢弃运行位置。
PublishSession(session, presentationVisible, hwnd);
if (remember) CommitRuntimePositionAndSave();
// Correct: 生命周期独立；提交运行位置与保存触发分离。
PublishSession(session, authoritativeShowActive, hwnd);
CommitVersionedPairPosition(); // 始终；仅 toggle/end/reset 冻结保存请求
~~~

## Scenario: PPT 原生批注接管与旧加载页门禁

### 1. Scope / Trigger

改动 `GetSlideShowAnnotationTool`、`ExitSlideShowAnnotationTool`、native 自动接管或旧 PPT 加载页时，沿用本合同；两套设置仍分别归属 `main.json` 的 `AutoTakeOver*` 与 `pptcom_configuration.json` 的 `ShowLoadingScreen`。

### 2. Signatures

- 旧 `IPptCOMServer` 方法顺序/GUID 不变：`int GetSlideShowAnnotationTool()` 返回 0=无/不可识别、1=画笔、2=激光笔、3=明确命名的荧光笔；`bool ExitSlideShowAnnotationTool()` 只在 owner 确认原生工具退出后返回 true。
- native `ChangeStateModeToPptAnnotation(PptAnnotationTool tool, uint64_t expectedRevision)` 在同一工具模式锁内校验 revision、设置笔型与 Pen 模式，再发布 Draw3。

### 3. Contracts

- `PptComService` owner 每次维护时读取 `SlideShowWindow.View`，释放本次获取的 View；跨线程 getter 只读按 session/binding/window 配对的纯值缓存。WPS 可有活动会话但 typed HWND 为 0，managed仍维护基础控制与工具缓存；native自动接管须另外取得当前可靠窗口和已呈现的可信墨迹目标，避免退出Office笔后没有可用画布。
- 标准 `PointerType=Pen` 映射画笔；可用的 `LaserPointerEnabled=true` 优先映射激光笔。当前 PIA 无此属性，激光状态按名称 late-bound；荧光笔仅在提供方明确返回其名称时映射，不猜测未知整数，也不把标准 Pen 推断成荧光笔。
- 退出原生工具请求走同一 owner 邮箱，超时请求不能稍后执行；owner 重读工具、关激光/复位指针并确认已退出。native 仅在同一 service/session 且原生退出成功、用户工具 revision 未变化后接管，成功才消耗 `AutoTakeOverOnce`。
- `kPptLoadingPageEnabled=false` 同时隐藏设置卡片和阻断 `FreezePPT` 触发；旧配置值及加载画面实现仍保留。

### 4. Validation & Error Matrix

| 条件 | 行为 |
| --- | --- |
| Office 忙碌、未知工具或 native 原生退出失败 | 不切换 Draw3，不消耗一次性接管；下一次维护可重试 |
| 退出请求等待时换场或用户换工具 | 拒绝迟到接管，不覆盖新场次/新工具 |
| WPS Active 但 typed HWND 为 0 | 依靠 session/binding 缓存保留基础控制；窗口与墨迹目标未获证实时不自动接管 Office 笔、不抢焦点 |
| `ShowLoadingScreen=true` 的旧文件 | 设置入口不可见且本场不触发加载页，磁盘值不被改写 |

### 5. Good / Base / Bad Cases

- Good：owner 读到原生 Pen/Laser/明确命名 Highlighter，成功退出后同一修订切换对应 Draw3 工具。
- Base：无原生批注时 getter 返回 0，不动用户当前工具。
- Bad：native getter 直接跨线程访问 Office RCW；先切 Draw3 再忽略原生退出失败；把未知 `PointerType` 整数当作荧光笔。

### 6. Tests Required

- managed fake View 覆盖 owner 缓存、跨线程 getter 不读 live View、Pen/Laser/明确命名 Highlighter、活动会话 HWND=0、owner 退出确认与过期请求。
- 完整 `InkeysRepo.sln Debug|ARM64` 与 headless/managed 测试；真实 PowerPoint/WPS 的工具选择、原生退出、输入命中仍需非自动 GUI 验收。

### 7. Wrong vs Correct

~~~cpp
// Wrong: 先切应用工具，原生批注即使仍占用输入也当作成功。
ChangeStateModeToPen(); ExitPptSlideShowAnnotationTool();
// Correct: owner 确认退出后，同一会话与工具版本才发布 Draw3。
if (ExitPptSlideShowAnnotationTool(server) && SameSession() && SameModeRevision())
    ChangeStateModeToPptAnnotation(tool, expectedRevision);
~~~
