# Research: 三工作区状态机的白板全屏窗口合同

- Query: Desktop / Presentation / Whiteboard 三工作区设计中，白板是否已使用完整显示器矩形、哪些路径仍保留1px、如何复用Freeze稳定全屏且分离显示与前台结果？
- Scope: mixed；只读当前冻结工作树与项目规范，外部语义仅核对Microsoft官方文档。没有产品/测试/spec修改、编译、CLI、GUI、运行时窗口/进程/PDB采样或Git操作。
- Date: 2026-10-27
- Context: 任务现为planning，状态机由主会话撰写并等待用户审核。下文行号来自当日冻结工作树（含未验证的三份Window补丁），不能作为已验收实现或新实机结论。

## Findings

### 1. 文件与现有规范

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp` | 主屏背景尺寸、共享UI3渲染、Freeze提交及可见性ACK |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp` | 背景Scene物理包络、DIP→像素换算与零外扩 |
| `Inkeys/Inkeys/Display/Display.cpp` | `MonitorInfo.bounds=MONITORINFO.rcMonitor` |
| `Inkeys/Inkeys/Window/Window.CanvasPolicy.h` | Desktop/PPT底部1物理行region、NonRude属性和Whiteboard原策略恢复 |
| `Inkeys/IdtFreezeFrame.cpp` | 旧桌面定格的1px DIB/Freeze写者与白板surface所有权互斥 |
| `Inkeys/IdtMain.cpp`、`IdtMagnification.cpp` | 窗口创建样式/owner链输入，旧Magnifier高度与source范围 |
| `Inkeys/Inkeys/Window/Window.cpp/.cppm` | 创建固定owner链、无边框/APPWINDOW/激活、内部层级及Shell发布 |
| `Inkeys/Inkeys/Window/Window.ShellFullscreen.h` | Freeze/Drawpad整批绝对全屏通知、代际和最新版本 |
| `.trellis/spec/native-desktop/whiteboard.md:23–29` | 主屏、任务栏锚点、NOTOPMOST、首帧与激活分离 |
| `.trellis/spec/native-desktop/draw3-integration.md:462–468` | 双画布Desktop/PPT底行与NonRude合同，Whiteboard恢复原region |
| `.trellis/spec/native-desktop/rendering-and-ui.md:1643–1644,1746–1754` | 固定owner树、窗口线程归属、白板激活/任务栏例外及桌面定格不得全屏标记 |

### 2. 白板背景已经以完整rcMonitor为目标，没有显式减1

可直接从当前源码确认：

1. `Display.cpp:683–686`把 `monitor.bounds` 设为 `native.rcMonitor`，pixelWidth/Height也是其完整右减左/下减上。
2. `Whiteboard.cpp:55–61`的PrimaryBounds直接返回主屏 `monitor->bounds`；fallback为完整SM_CXSCREEN/SM_CYSCREEN，没有减1，也不使用rcWork。
3. `Whiteboard.cpp:109–128`按完整宽高除DPI scale创建背景：零圆角、零描边、fillOpacity=1，并把完整bounds传给Scene.SetBounds。
4. `Bar.Scene.cpp:719–736`设置backgroundOnly和logical bounds；`:292`背景的Outset返回0，`:489–493`只做像素包络和屏幕位置映射。`:46–49`使用floor/ceil转换，不含人为留底行。
5. `Whiteboard.cpp:154–168,204–223`按该PresentationBounds分配并ULW提交完整背景；`:251–263`仍需窗口SetBounds/Show成功后才确认背景Active。

因此新设计应表述为**保持/明确白板完整rcMonitor合同并消除交接中的旧几何覆盖**，不能把“去掉白板本来的1px”当成本次新增修复。用户现在看见的旧1px或任务栏覆盖，并不由PrimaryBounds里减1解释。

精确像素注意项：背景仍经DIP除乘和floor/ceil包络，不能仅凭“没有-1”断言所有非常规DPI下实际RECT逐像素恒等。设计验收应在96/120/144/168/192及项目支持的非整倍率下核对最终Freeze RECT、ULW bitmap包络及最底行覆盖恰为目标rcMonitor，不以rcWork、浮点推算值或任务栏高度替代。此为待验证边界，不是本研究已复现的另一个像素缺陷。

[Microsoft MONITORINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-monitorinfo)区分显示器矩形rcMonitor与工作区rcWork；白板全屏目标应使用前者。

### 3. 目前保留1px的路径应分开管理

| 路径/角色 | 现有尺寸或区域行为 | 三态设计约束 |
| --- | --- | --- |
| Whiteboard背景 / Freeze的白板渲染者 | 完整主屏rcMonitor，无显式减1；Whiteboard.cpp:55–61,109–128 | 保持全屏，不沿用桌面定格DIB尺寸 |
| 主Drawpad与DrawpadPresentation，Desktop/PPT | 完整HWND/client/backbuffer/RTS标尺；Window.CanvasPolicy.h:78–86仅对监视器最底1物理行与HWND交集做region排除，125–136从原region扣除 | 保留，不改成缩短HWND/文档坐标，不受UI倍率或任务栏方向影响 |
| 同两画布，Whiteboard | CanvasBottomExclusion在:80直接返回空，恢复创建时原region；NonRude在:13–15恢复创建原属性 | 不额外裁底行；原region/属性真实恢复仍需读回和失败责任 |
| 旧桌面定格 / FreezeFrameWindow | IdtFreezeFrame.cpp:59–70将DIB和Freeze初始bounds高度设为主屏height-1 | 只属于桌面定格/旧Freeze surface，不从全屏白板反推要删除它 |
| MagnifierHost与MagnifierChild | IdtMain.cpp:3282–3304，创建高度SM_CYSCREEN-1 | 保留旧定格能力，不能因为白板全屏而扩大它们 |
| Magnifier source | IdtMagnification.cpp:157–160使用SM_CXSCREEN-1、SM_CYSCREEN-1 | 当前源矩形右/下也减1；不是白板背景计算 |

尤其不能为这次白板设计统一删除所有 `-1`。Drawpad/Presentation是窗口region底行保护；旧Freeze/Magnifier是既有表面或窗口尺寸，二者不是同一实现。

`CanvasNonRudeValue`（Window.CanvasPolicy.h:13–15）在Desktop/PPT设置TRUE，在Whiteboard恢复原值；`EnsureCanvasPolicy`（Window.cpp:2099起）仍由Window owner处理region/样式和成对恢复。Freeze不是该双Canvas policy的目标，不能把其全屏背景误套两画布底行region。

### 4. Freeze可以继续作为普通可激活无边框全屏APPWINDOW

现有结构已经提供需要的基础：

- `IdtMain.cpp:3269–3274`创建overlay用WS_POPUP|WS_CLIPCHILDREN及完整主屏初始宽高；Freeze在3374–3375注册，带原生图标和白板关闭/最小化回调（3337–3348）。它本来就没有caption/thick frame，无须新建专用全屏HWND。
- `Window.cppm:87–109`：Whiteboard中Freeze是唯一APPWINDOW，清TOOLWINDOW/NOACTIVATE；Drawpad可激活但保持TOOLWINDOW，辅助窗不进任务栏。Desktop/Presentation恢复NOACTIVATE|TOOLWINDOW并清APPWINDOW。
- `Window.cpp:1499–1502`明确Freeze保持无边框尺寸，设置窗口的外框校正仅作用Setting。
- 白板背景用原Freeze和共享RenderPipeline客户端，墨迹仍用既有Draw3单Host/设备/主Drawpad；不需要新Host、D3D device、第二套doc、swap-chain独占全屏或显示模式切换。

[Microsoft Extended Window Styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles)说明APPWINDOW使可见顶层窗进入任务栏，TOOLWINDOW不显示任务栏；NOACTIVATE与前台激活另有规则。这里应保留单一锚点，不能把Drawpad或Setting也变成APPWINDOW来修任务栏。

### 5. 保留隐藏Mag owner，不应无理由reparent成独立新窗

当前创建链不是child窗口树：

`Mag → Freeze → DrawpadPresentation → Drawpad → Bar / 四个PPT`

Window.cpp:1184–1243由已有owner逐级创建；DrawpadPresentation显式own于Freeze，Drawpad own于Presentation，Popup own于Drawpad；ExpectedGroupOwners在1881–1891复核同一传递链。Mag可选，缺失时Freeze自然为根。这与rendering-and-ui.md:1749的固定创建合同一致。

[Microsoft Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#owned-windows)区分owned popup和WS_CHILD：owned须在owner之上，owner销毁会销毁它，owner最小化会隐藏它；child才受父客户区裁剪。

所以隐藏Mag的height-1不应作为裁短Freeze全屏的理由；Freeze是顶层owned popup，既有APPWINDOW合同可让它继续有任务栏身份。普通Hide Mag与最小化/销毁Mag必须区分：白板切换不得最小化或销毁这个owner来“整理层级”，也不能把ShowOwnedPopups一类整组动作当成只操作Mag本身。既有Freeze::SetWhiteboardActive导致Magnifier coordinator只透明/隐藏Host/Child的合同保持（rendering-and-ui.md:1752–1754）。

没有发现必须reparent、新建窗口或把Freeze owner清为null才能实现完整rcMonitor的源码依据。重建owner会扩大生命周期、动态Setting owner、输入/任务栏、窗口重建和Shell代际风险，应留在此次设计之外。

### 6. 全屏、可见、前台、任务栏让位是独立事实

[Microsoft MarkFullscreenWindow](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-itaskbarlist2-markfullscreenwindow)说明：TRUE标记在目标窗口active时使任务栏降到Z底；FALSE撤销显式标记，但Shell仍可能自动识别全屏；Win7起NonRudeHWND(TRUE)才明确禁止这种识别。

[Microsoft SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow)说明前台分配受系统限制，即便满足条件仍可拒绝；[ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)提供不激活显示，且其返回值是原可见状态，不能当显示成功回执。

因此状态机的窗口合同应为：

- 完整rcMonitor、真实可见/非iconic、对应后台帧与内容版本提交，是白板正常后台就绪的必要事实。
- 达到这些事实后可提交Whiteboard稳定状态，并只为本次显式打开排队一次尽力激活；前台成功、Shell最终任务栏让位和其他应用是否遮挡不参与切换完成门。既有RequestWhiteboardActivation在Window.cpp:620–631异步接纳，继续保留过期/隐藏/最小化/stop保护。
- 白板处于后台时其他应用及其任务栏可能仍在它上面，这是前台策略；不能靠循环激活、强制TOPMOST、直接隐藏任务栏或全局连续排序来维持“已进入”。
- 真正用户激活Freeze或Drawpad时，只让既有Shell mailbox重申绝对全屏事实，不重建Canvas/APPWINDOW、不重播背景和入口，不重新发布工作区。
- 关闭/最小化/隐藏和stop发布最新FALSE，迟到TRUE不能覆盖；失败COM不是卡住业务状态机的理由。

当前Shell接线：Window.ShellFullscreen.h:47–63为Whiteboard标记Freeze/Drawpad两面并使隐藏普通root不作为白板锚点；Window.cpp:3497–3520再按visible/iconic/SafetyHidden收束。任一面WM_ACTIVATE的观察回调在Window.cpp:1295–1305触发邮箱失效，任务栏重建消息在191–193也只是重申；实际COM在独立worker，state machine无需同步等Explorer调用。

### 7. Win7 / 8 / 8.1 / 10 / 11能力边界

| 系统 | 这一设计可依赖的API基线 | 不应外推的结论 |
| --- | --- | --- |
| Win7 | 顶层WS_POPUP/layered/APPWINDOW、MonitorInfo、非激活Show均早于Win7；MarkFullscreenWindow官方最低XP，NonRude文档明确自Win7 | 没有实机证明Aero开关、驱动/Explorer与ULW组合已通过；图形运行库能力仍走现有产品fallback |
| Win8 / Win8.1 | 同一顶层popup/Shell方案，不需要利用Win8新增layered child支持；Freeze仍不是child | 不能因Windows Store/UWP的前台限制文字误判本Win32应用不能使用；也不能承诺Explorer具体视觉时序 |
| Win10 | 同一能力合同，不必为白板全屏新增Mica/桌面窗口管理私有API | 尺寸和S_OK不是任务栏实时让位证据 |
| Win11 | 同一能力合同，仍要尊重前台与用户切换 | 不保证所有任务栏版本、自动隐藏、跨屏/DPI/Explorer重启已实测 |

这只是所用Win32/Shell接口的能力下限，由上述Microsoft文档支持，不是新增平台支持声明。完整产品在Win7的D3D/D2D运行库、DComp/ULW回退、ARM64与CPU/驱动等条件仍沿既有规范；本研究没有运行任一系统的GUI。设计不引入只在Win10/Win11存在的新全屏前提。

### 8. 必须由三工作区协调者统一发布的窗口动作

下列是窗口动作的所有权约束，不是状态机正文或新增状态定义：

1. 一份目标工作区事实派生同代窗口政策：Desktop/Presentation的非激活辅助层、双Canvas底行/NonRude；Whiteboard的Freeze完整主屏背景、唯一APPWINDOW、非置顶和Drawpad激活例外。局部Bar按钮、绘制线程和WM_ACTIVATE不能各自改mode。
2. 只在真实边沿切APPWINDOW/TOOLWINDOW、region/NonRude和有效band；正常focus变化/全局排序失败不能触发Hide/FRAMECHANGED/Canvas重建。
3. Freeze surface在旧定格与白板背景之间只有一个拥有者。IdtFreezeFrame.cpp:38–44的SubmitFreezeSurface以互斥和前后owner检查防止旧提交，:116旧线程在白板持有时暂停；统一交接须在最后旧帧退休、背景隐藏/接管及真实bounds确认之间保持这条合同，不能让旧height-1 DIB重写完整白板。
4. 协调者发布目标，Window Service owner执行HWND样式/owner/显隐/bounds/click-through；RenderPipeline执行背景ULW，Draw3执行墨迹。PageControl仍是四分页HWND显隐唯一owner；协调者通过同代view目标和ACK收束，不能直接跨线程Show所有窗口。
5. 最小化是同一Whiteboard的窗口显示状态，不是Desktop切换；保留文档/页/工具，恢复同代窗口与背景。最小化暂停active绘制不应阻塞inactive隐藏清理。WM_CLOSE/Alt+F4只提交离开意图，不销毁共用Freeze及owned树。
6. Display/DPI/设备变更经现有Display订阅和渲染客户端更新目标rcMonitor、surface和物理readback；不重拍Desktop主栏基线、不重新创建Host或循环激活。Whiteboard.cpp:298–303已有布局/背景唤醒。
7. Shell绝对TRUE/FALSE、激活请求、隐藏清理均归属当前意图/窗口generation；取消、快速往返、重建和stop拒绝旧回执。Shell worker的可用性和焦点结果不得反向驱动工作区。
8. 白板交接及物理回滚只确认owner/band/内部Bar/PPT关系，不经过常规Desktop全局TOP/continuous硬门。普通Desktop/PPT显式刷新仍保留旧正常职责，但不能阻止完整模式退出、又把工具/主栏保留在旧状态；进入后台不抢全局位置。

[Microsoft SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)也把显示、激活、owner排序和band分成明确flag/动作；当前设计应保持同样分离，而不是用一次全局置前推定所有事实。

## Caveats / Not Found

- 本文件仅为主会话的最终三工作区状态机提供窗口/全屏事实和设计约束；不撰写三态正文，不改变按钮可见/焦点三态。
- 目前源代码目标全屏并不证明用户现场实际Freeze矩形一直全屏，也不证明Shell让位；既往授权采样只覆盖旧首开/后台闪烁，不能代替将来的全屏验收。
- 原region、显示拓扑、任务栏方向/自动隐藏及非常规DPI的实际readback须进入审核后的验证矩阵；不把这些未执行项写为兼容性通过。
- 三份现有Window补丁仍冻结、未验证，本研究没有继续实施或回退它们，也未新增Host/窗口/owner切换。
