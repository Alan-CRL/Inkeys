# Research: PPT 页码状态与画布/控件层级

- Query: 翻页后显示 `-/-` 的生产来源，及 PPT 控件可能被 Inkeys 画布遮挡/无法命中的原因；区分已由 0eb1c7c5 或历史 owner 修复覆盖的情况。
- Scope: mixed（内部静态分析为主；Win32 语义以微软官方文档核对）
- Date: 2026-10-18
- Task: `.trellis/tasks/10-18-ppt-controls-drag-state-layering`；保持 planning。
- Baseline: 当前 HEAD `13783fe7`，含 PptCOM 修复 `0eb1c7c5`。用户可能使用 `20261010b` 或 `20261014a`，尚未确认安装包。此文件不把这些源码基线等同故障设备实际二进制。

## Findings

### 结论与证据等级

| 现象 | 当前可直接确认的事实 | 分类与本任务建议 |
| --- | --- | --- |
| 翻页时 `-/-` | 生产 PageControl 对两个负数分别输出 `-`；native 当前已保留同场 busy 前已显示的数字，并显式发布 Pending；HWND=0 但本轮 dynamic 页证据成立时可显示 BasicControl 页码 | 0eb1c7c5 已覆盖主要退化来源。先核修复版本回归；本任务不再无证改 COM、descriptor 或把旧数字冒充新页 ready |
| 新场次尚无可信页码时 `-/-` | 新 session 会重置 UI 页状态；新文稿/服务换代不能借用旧场数字，未加载成功的首帧也可能仍是未知 | 合法未知，占位符应保留；不能以消除所有 `-/-` 为验收 |
| 控件确实低于 Inkeys DrawpadPresentation | 当前通过 `Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar` 传递 owner，静态/动态创建均如此；历史修复 8a25cfcd 已在两个可能版本前存在 | 经典双画布同为 Freeze 兄弟导致层级漏洞的缺陷已覆盖。不能将用户这次报告直接归因该旧缺陷；需故障时实际 HWND/owner/Z 序证明新断点 |
| 控件被外部 PPT 放映窗口遮挡 | 外部 PPT 不在该 owner 树；根 TOPMOST、单窗 Show/Promote、外部前台切换是不同事务 | 与上行不同。前轮调查已有风险，尚无本轮动态证据；保留唯一根策略，不直接改成逐窗 TOPMOST |
| 控件看得见但点击无效 | 转场时可返回 HTTRANSPARENT；PPT 箭头本身未因 COM Pending 禁用，但业务队列按当前控制能力/场次拒绝过期动作，并抑制在途翻页重入 | “按不了”不等于已证明 Z 序低；应分别采输入命中、窗口层级和命令处理证据 |

目前没有从当前生产分支确认需要新增页码或 owner 链修复的缺陷，也没有运行 GUI 复现。主要实施范围应先放在其他研究已确认的拖动/吸附问题；本研究涉及两项先作为回归与证据收集范围。

### 文件地图

| 文件 | 职责 |
| --- | --- |
| `Inkeys/IdtPlug-in.cpp` | PptInfo 观测 managed 缓存、对照 raw 页码、维护 local session、向 Draw3 发布 target、接收 ready、向 UI 发布页码/模式；PPT 业务队列 |
| `Inkeys/Inkeys/Business/PptSession.cpp`、`.h` | 严格 session envelope 解析、raw/descriptor 一致性、BasicControl 与场次身份判断 |
| `PptCOM/PptCOM.cs` | dynamic owner 轮询、纯缓存 getter、descriptor 重试、明确结束/坏接口处理 |
| `PptCOM/SlideShowSession.cs` | session/binding/generation、生命周期和纯值状态序列化 |
| `Inkeys/Inkeys/UI/Ppt/Ppt.cpp`、`.cppm` | 页展示模式/epoch、转发 PageControl publication 与可信 UI ack、进入放映的根刷新 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`、`.cppm` | 真实文字构造、四个独立 surface 呈现、显隐/窗口提交、命中与方向动作、四窗 commit 汇合 |
| `Inkeys/Inkeys/Window/Window.cpp` | 静态/动态 owner 创建、Show/Promote、根 TOPMOST、双画布互斥显隐 |
| `Inkeys/IdtState.cpp` | Draw3 输出 ready 后的双画布选择/显隐及失败重试 |
| `InkeysHeadlessTests/ppt_session_tests.cpp`、`ppt_ui_tests.cpp`、`page_control_tests.cpp`、`window_tests.cpp` | 各层已有数据合同与隐藏窗口合同测试；本轮仅阅读 |
| `.trellis/workspace/codex/journal-2.md:457` | 前轮只读根置顶/外部 PPT 穿插调查，避免重复推断为新发现 |

### 1. `-/-` 的真实格式化入口

生产路径不是老 Ppt facade 的绘制：

1. `PageControl.cpp:533-537` 的 `PageNumber(value, maximum)` 在 `value<0` 时返回 `L"-"`，否则仅做数字位数上限。
2. `PageControl.cpp:688-705` 在 PPT compact 模式把 `ppt.currentPage` 写到 primaryText，把 `L"/" + PageNumber(ppt.totalPage, ...)` 写到 secondaryText。底部上限 9999，竖直侧栏 999。两者都为负数就显示 `-/-`；明确结束页 `current=-1,total=N>0` 显示 `-/N`，含义不同。
3. `Ppt.cppm:229-237` 的 `ResolvePageText` 也有相同负数占位规则，但该 helper 的测试不是整个当前生产 PptInfo 观测链的执行证明。`ppt_ui_tests.cpp:158-165` 只直接断言这个 helper 的占位与位数限制。

不要把 `current=-1` 一律当错误或结束。结束页需要额外的 `pageStatus=EndScreen`、可信 View.State Done、同轮总数及拓扑证据；未知两数不能授权结束或写入。

### 2. 页码从 COM 到 UI 的当前门禁

| 阶段 | 生产位置 | 合同 |
| --- | --- | --- |
| managed owner 观测 | `PptCOM.cs:1124-1233` | 本轮取得 ActivePresentation/SlideShowWindow/View、读取页码/总数；明确 showCount=0 才结束；busy/普通失败发布 Unknown；temporary 在 finally 释放 |
| managed descriptor | `PptCOM.cs:310-365` | 只在 owner 读取；同一 stamp 发布 session 与 legacy 当前证据；TransientBusy 独立约 100ms 重试，不能复用旧 Stable 伪装 Valid |
| 纯值 envelope | `SlideShowSession.cs:171-211` | 非 Active descriptor 变 Unavailable；generation/binding 不匹配的迟到读取丢弃；getter 只读序列化缓存 |
| native 观测 | `IdtPlug-in.cpp:594-697` | raw 两整数与 descriptor 同页/同总数才认可本轮页；检查 service generation；EndScreen 还核 fresh raw 与总数 |
| 基础控制 | `PptSession.cpp:14-32`、`PptSession.h:36-62` | Active/show/binding 有效；raw/descriptor 一致；允许 metadata Unavailable 中仍有本轮数字、允许 HWND=0；TransientBusy 不是 fresh basic 证据 |
| 文稿 target | `IdtPlug-in.cpp:797-843,903-961` | 文稿/页/窗口/场次可信才 Resolve/Publish target；HWND 仅授权墨迹/焦点，不是 dynamic 基础操作共同前提 |
| Draw3 已呈现 | `IdtPlug-in.cpp:955-961` | `running + workspace=Presentation + presentationReady` 与该 target 身份全等，才推进 `PptInfoStateBuffer`；否则 targetRevision=0 |
| UI publication | `IdtPlug-in.cpp:972-1007`、`Ppt.cpp:281-289` | 区分 Pending、BasicControl、InkReady；数字显示不是输入 ready |
| 真实 surface 提交 | `PageControl.cpp:1940-2064` | Present 成功之后仍要 SetBounds/Show/Hide 成功；过期 publication/direct-move 帧拒绝；可见四窗提交汇合后回执 |
| UI ack | `Ppt.cpp:166-173`、`Ppt.cppm:167-185`、`PageControl.cppm:618-649` | 数字、session、target、实际 frame 的 pageAckEpoch 一致，且 mode=InkReady 才向 native 转发；Pending/BasicControl 不能开放旧墨迹 |
| 最终放行 | `IdtPlug-in.cpp:137-152` | 再次核 expected target/session/page kind/页码/总数，才 PublishProductPresentationUiReady |

三种 UI 模式的含义：

- `Pending`：旧数字可以继续显示，但当前没有新页可写证明。Unknown、busy、等待 Draw3 或身份恢复均不能产生有效 UI-ready。
- `BasicControl`：owner 本轮确实读到了数字，可以翻页/同场退出；无可靠 HWND/文稿身份不创建墨迹 target。`IdtPlug-in.cpp:871-885` 会隔离旧 target，不能把基础数字写进 `PptInfoStateBuffer`。
- `InkReady`：Draw3 匹配页已经成功呈现；仍要等 PageControl 所有可见 surface 的实际成功提交和 epoch 回执后开放墨迹输入。

这三个模式不能通过仅保留 UI 数字合并。`Ppt.cppm:170-185` 的 epoch 随页码、target 或 mode 的真实变化推进，专门防止同数字同 target 的 Pending→InkReady 恢复误接收暂停前旧帧。

### 3. 0eb1c7c5 对 `-/-` 的已覆盖范围

当前分支已直接具备以下行为，与用户“可能由刚刚 COM 识别修复解决”的判断一致：

- `IdtPlug-in.cpp:738-744,775-808`：HWND=0 的已证实 COM 场次可保留基本能力；同场后来取得 HWND 只更新锚点，不伪造重新开始、不重置位置/页码。`basicControl` 在 `:975-979` 显示 fresh 数字。
- `IdtPlug-in.cpp:992-998`：同一 service/local session/show/binding 的 busy 或暂时不 ready，使用 `publishedPage/publishedTotal` 保留已显示数字，但 mode 保持 Pending。此前已显示 5/20 时，单次忙碌不会单因失去 fresh 页证据切成 -/-。
- `PptCOM.cs:349-354`：两种 getter 发布同次忙碌证据，约 100ms descriptor 重试独立于 3 秒稳定审计，不让短忙碌被拖成数秒旧状态。
- `PptSession.cpp:23-31`：metadata/SlideID 等失败，但本轮 raw 与 descriptor 数字仍相符，可 BasicControl；严重 IDispatch/type-info 故障仍可能完全没有有效数字。
- 0eb1c7c5 的新 `pageAckEpoch` 从实际 render snapshot 经 commit 返回，不能只证明“数字没变”就提前重新开放旧页写入。

以下 `-/-` 仍属合法或尚待独立查证，不能 blanket 隐藏：

1. 新 local session 开始时 `Ppt.cpp:323-330` 清空 pageState；native buffer 也是 -1/-1，首个可信数字/Draw3 ready 前可占位。进入/visible publication 先于稍后 PublishPageState，可能先出现一个未知帧；尚无视觉时长测量。
2. service generation、showSessionRevision 或 binding 已变化，`IdtPlug-in.cpp:992-999` 不借用旧场数字。不同文稿恰好页数相同也不能绕过。
3. 第一次识别就 busy、COM 完全不可访问，或从未有成功 publication：没有可保留的可信数字。应记录失败/重试原因而非填旧文稿数字。
4. 真实退出先发布 inactive/reset，淡出期可能呈现未知数字。不要为消除占位推迟真实退出或保存边界。
5. 用户实际安装包尚未确认。即便源码对应 20261010b/20261014a，也不能推定已经装了 10 月 18 日的 DLL/native 联动修复；不能只替 DLL 后便认定 native 的 mode/保留策略已生效。

严格边界：当前 native raw `CurrentPage/TotalPage` 仍为跨 managed/native 普通 int 对，并非统一原子快照；existing spec 明确将完整同步列为未定合同。一次 raw/descriptor 不一致当前应变 Pending 并保留同场旧显示，而不是放宽一致性校验。此任务不为此改 ABI。

### 4. “控件位于画布下方”须区分的对象

当前 owner：

```text
MagnifierHost（缺失时 Freeze 为根）
└─ Freeze
   └─ DrawpadPresentation（选择态/辅助 ULW）
      └─ Drawpad（主绘制画布）
         ├─ Bar
         ├─ PptBottomLeft
         ├─ PptBottomRight
         ├─ PptMiddleLeft
         └─ PptMiddleRight
```

- 静态创建：`Window.cpp:1000-1030`；动态创建：`:2118-2131`；创建期间 Drawpad 没有 Presentation owner 就拒绝，不能无声退回兄弟链。
- `Window.cpp:1976-2074` 只成对显隐两画布，DWP 使用 NOZORDER/NOACTIVATE；回读确认互斥。失败回退先双隐藏再显示唯一目标，不改变 owner。根/Drawpad 隐藏不等于 owned popup 自动隐藏，微软文档有明确区分。
- `IdtState.cpp:523-543,564-605` 等匹配输出 ready 才切 surface；不 ready 时保留旧已呈现画面；窗口命令带 bridge/tool-mode revision guard，失败留 retry pending。真实退出后的根刷新与普通翻页不是同一个触发。
- owned 保持高于 owner，因此 owner 完好的四个 PPT 理应高于 Drawpad 及其 owner DrawpadPresentation。该结论不等于保证高于外部独立 PPT 放映窗口。

历史确认（由主会话执行只读 git 核验，本研究者未运行 git）：

- `8a25cfcd22b01fac3c8792d06e9f002ac16750c7`，2026-09-21，`fix(window): nest drawpad owner chain`。旧 Presentation 与 Drawpad 同属 Freeze，Bar/PPT 只在 Drawpad 上方，缺少对 Presentation 的传递保证；该提交将静态、动态 Drawpad owner 改为 DrawpadPresentation，并增加测试。
- 该提交是 20261010b 准备提交 `a50ad143`、20261014a 对应 `5d319bbf` 和当前 HEAD 的共同祖先，所以两个可能的发布源码都已经有这一修复；不能把本次重报默认归因“尚未包含这个修复”。实际用户 binary 仍未校验。
- 两版本 PageControl.cpp/.cppm blob 相同；0eb1c7c5 对它们仅增加 ack epoch 传递，并没有新增一套控件 Z 序修复。
- 历史归档交叉证据：`.trellis/tasks/archive/2026-10/09-27-inkeys3-first-release-hardening/audit-coverage.tsv:50` 记录该 owner 补丁；`journal-2.md:480-497` 已区分内部画布与外部 PPT 穿插。
- 后来的 `4057b8ea` 是 PPT 退出表面交接与 Setting 即时层级修复，不能单凭题名作为普通翻页时四控件全组层级修复的证据。

### 5. Show/Promote 与翻页的实际时机

- 每个 PageControl 成功呈现后 `PageControl.cpp:2000-2007` 都向 owner 线程提交 SetBounds + Show（或 Hide）；并非每翻页后完全不触碰层级。失败返回 Retry，不能提前记已提交页码。
- `Window.cpp:1696-1721` Show(PPT) 用 SW_SHOWNOACTIVATE，然后 Bar→HWND_TOP、目标 PPT 插到 Bar 后，回读可见性。`:1890-1910` Promote 同样排序，不独立 TOPMOST、不抢焦点。它们没有显式逐一重新排序另外三个兄弟，但 USER32 可连带调整 owner，不能据此断言只有两窗会移动。
- `PageControl.cpp:2323-2326` pointer-down 的 Promote 在 Scene/render 锁外，必须先已经命中；如果窗口实际被遮住，不能指望这条点击后分支自救。
- `Ppt.cpp:30-45,244-278` 进入 visible 边沿请求根刷新；失败保留 pending，由既有 500ms 发布重试；成功后不会因页码变化/外部遮挡自动重新请求。
- `Window.cpp:1870-1888` 根刷新只判断根 SetWindowPos BOOL；没有对所有成员相对外部 PPT 的实际 Z 序逐一读回。`Window.Legacy.cpp` 周期刷新是前轮已记载的机制，不是本轮新缺陷证明。
- `IdtPlug-in.cpp:1131-1142` 上下页业务结束后可能 FocusPptSession；它没有同步发起另一个全组根刷新。不能把 foreground 自身等同 topmost，外部窗口是否提升/新建需现场证据。

当前内部 owner 树没有发现被普通翻页路径拆开的操作。如果报告说“画布压在控件上”，首先确认压住者确实是本进程 Drawpad/DrawpadPresentation，而非全屏外部 PPT、透明输入捕获窗或控件自身不命中。

### 6. “看得到但点不了”的其他可辨路径

1. `PageControl.cpp:962-964` 的 `inputLocked` 由显隐、layout transition deadline、白板 workspace switching 决定（helper `PageControl.cppm:901-905`）。`:2161-2176` 的 WM_NCHITTEST 在 inputLocked、不可见或坐标无法映射时返回 HTTRANSPARENT；这会让同线程下层继续命中，不证明该控件实际 Z 序更低。
2. PPT 模式箭头 `PageControl.cpp:700-702` 始终 enabled/interactive，并没有因 Pending/BasicControl 直接禁掉。进入/退出/工作区转场的短期锁是另一件事。
3. 已命中且入业务队列仍可能不执行：`IdtPlug-in.cpp:83-87,1125` 检查 pptControlAvailable、service generation、捕获场次；当前 Unknown 不能冒充可操作的 Active。`:108-113` 抑制在途上下页重入，不能据此称命中丢失。
4. 共享 PageControl render lock/单 surface 状态停住、动画/呈现持续失败，也可能让本地输入状态不再恢复；这与“只有被拖动控件停、主栏仍工作”的新反馈相容，但具体锁链由拖动研究负责。仅凭主栏仍响应不能判断 Draw3/该控件线程健康。

建议异常证据至少包含：每窗 hwnd/PID/TID、GW_OWNER、style/exStyle、visible/enabled、实际 bounds、前后 Z 邻居；压住点 WindowFromPoint/命中返回；对应 surface 的 configuredMode/targetVisible/inputLocked/transition deadline、committedPresentationReady、latest publication/directMove revision；当前 session/binding/target/pageAckEpoch、page mode、控制能力与 raw/descriptor 数字；触发前后 Show/Promote/根刷新/焦点交接及返回。所有日志纯数值/枚举，不加入标题、文稿路径或墨迹内容。

### 7. 已有测试与最小验证建议（本轮不执行）

已有源码证据：

- `ppt_session_tests.cpp:55-109`：wire parser、HWND0 基础页、raw 不一致、metadata 失败、TransientBusy 不授权 fresh 数字、结束页基础控制。
- `ppt_session_tests.cpp:111-148`：换 session/service/window/process birth 身份和同场 Unknown 保留资格，不借新文稿/新场次。
- `ppt_ui_tests.cpp:169-199`：BasicControl/Pending 不回执、同页恢复 epoch、EndScreen、target0 拒绝。
- `page_control_tests.cpp:390-445,447-465`：四可见 surface 汇合、隐藏/退场旧数字、same-page busy epoch 及 stale commit 拒绝。
- `window_tests.cpp:435-461`：两画布 owner、四 PPT owner、Bar 高于两画布的 surface 切换；`:507-532` 隐藏外部 topmost 竞争与整树根刷新；`:614-629` Promote/reshow 的 owner/Bar 顺序；`:744-761` 动态 owner 链。
- `PptCOM.Tests/ManagedCompatibilityTests.cs:148-188`：managed busy 两缓存一致、独立重试、metadata scalar 降级；实际跨提供方、实际用户设备不由这些断言证明。
- 已归档上一任务验证 `research/verification.md:23-35` 记录双架构真实 COM 夹具、solution、--no-window 通过；它没有执行本轮 Office/GUI/隐藏 HWND 层级回放，不能外推到本症状实机通过。

待用户批准实施后，有意义的最小回归：

| 场景 | 必须断言 |
| --- | --- |
| 已显示 5/20，同场短 busy，之后页6 ready | Pending 仍5/20，旧 ack 拒绝；target6成功 Present→四可见UI提交后才页6 InkReady/写入开放 |
| 首场 busy，或新 binding/新 show 恰好同页数 | 可合法 -/-；不能借旧场数字、旧 ack、旧 target |
| HWND0/typed接口坏，但 dynamic 数字有效 | BasicControl 数字/翻页保留；不 Focus、不生成旧画布 ready；稍后 HWND 恢复不重放 session |
| metadata失败但本轮页与总数一致 | BasicControl；若 raw 不一致或明确 TransientBusy，保留旧显示且 Pending |
| 结束页 ↔ 普通页，旧结束页 raw 已变化 | 不把旧拓扑当新 EndScreen；- / N 不是未知 -/- |
| Primary/Presentation/Hidden 切换，同时四控件可见 | 每个 PPT owner为Drawpad，始终高于两画布；Bar高于每个PPT，未激活，显隐/命中一致 |
| 一次 SetBounds/Show/Hide/Present 失败与恢复 | 保留 Retry、不能回执新页就绪，不以数字显示假成功 |
| 实际 GUI 用户场景（另获授权后） | 先核真实包/DLL/native hash，再复测鼠标/触摸拖动、连续翻页、画布选择/绘制切换；异常采 HWND 与命中快照 |

最小修复范围：目前没有额外生产变更可由本研究直接要求。若新版仍重现 -/-，先把 native publication 决策的实际生产路径补可复用测试边界，再按观测到的身份/生命周期断点修；若新版仍证实层级错误，修 Window Service 的具体创建/显隐/恢复失败点，维持唯一根 TOPMOST，而非顺手改为每页全窗独立置顶。现有窗口测试对 surface 切换后主要断言 Bar，上述“四个 PPT 均高于两画布且可命中”的组合可作为增强验收，但不得仅扩断言就称故障已复现或修复。

### Related specs

- `.trellis/spec/ppt-interop/index.md`：端到端 COM→target→Present→buffer→PageControl→input ready 及真实设备边界。
- `.trellis/spec/ppt-interop/native-session-ui3.md:19-39`：dynamic/owner/Unknown/BasicControl/epoch 与输入门禁、同页恢复及真实退出。
- `.trellis/spec/ppt-interop/com-contract.md:47-58,115-137,171-182`：唯一 owner、每笔 COM 获取显式释放、HWND例外、raw元组未定同步。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1708-1725,1734-1749,1775-1788`：单 owner 树、窗口命令归属、PageControl 成功窗口提交、唯一根置顶与隐藏窗口验证边界。

### External references

已于 2026-10-18 只读核对微软官方资料：

- [Window Features — Owned Windows / Z-Order / Visibility](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#owned-windows)：owned 保持高于 owner；隐藏 owner 不直接隐藏 owned；topmost 在普通窗前。由这些规则结合当前源码推导“owner完整则四控件应高于两画布”，不等同目标设备已验证。
- [SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)：TOPMOST 对 owned 的传播、NOZORDER 保留顺序、NOOWNERZORDER 只限制 owner 重排；不将 exStyle 传播当独立 TOPMOST 调用证据。
- [WM_NCHITTEST](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest)：HTTRANSPARENT 可继续向同线程下层窗口命中；此处相关窗口都属统一 overlay owner thread，不能把返回值自动扩展为跨进程输入可达保证。

## Caveats / Not Found

- 本轮只写本研究文件；未修改生产代码、测试、规范、配置，未构建/运行测试、启动 GUI/Office、操纵桌面或执行 git。
- 没有用户故障日志、dump、故障时 HWND/Z 序或真实版本 hash；没有当前版实机复现。`20261010b`/`20261014a` 为候选源码基线，尚非确认安装包。
- Git 历史/祖先信息由主会话只读核验并传入；本研究不运行被角色禁止的 git。8a25cfcd 的归档审计记录可独立交叉阅读。
- 经典 owner 漏洞已包含不等于任何“点不了”都已解决；新版 COM修复覆盖短 busy/损坏 typed 接口下的主要链路，也不等于所有损坏 IDispatch/类型信息都可用。
- 不为消除 `-/-` 放开未知页/旧页墨迹，不削弱现有 session/binding/target/epoch 门禁，不改变 dynamic 优先与每笔 COM 确定释放。
