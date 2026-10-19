# Research: Drawpad 双画布的场景化底边与全屏策略

- Query: 用户最新明确范围——Drawpad 与 DrawpadPresentation 在任何桌面和 PPT 下均让出物理底部 1px，且不应被 Shell 当作全屏；白板时取消该间隙，完整覆盖并保留白板全屏。比较真实 HWND 高度减1与双窗 region，调查场景事务、Host resize/RTS与全屏标记。
- Scope: mixed（项目源码、规范与微软官方 Win32 文档）
- Date: 2026-10-19

## Findings

### 1. 最新需求合同与推荐结论

本文件以主会话转达的最新用户明确范围为准；`bottom-edge-hit.md` 的“仅桌面自动隐藏时可选保障画布”已经不是最终范围。当前任务仍处于规划，没有实施授权。

| 场景 | 两个画布的物理最底行 | Shell 非全屏意图 | 既有白板 Shell 标记 |
| --- | --- | --- | --- |
| Desktop（绘制、选择预览、穿透） | 始终从窗口可见/输入区域排除，与任务栏位置、auto-hide无关 | 两窗显示前设置 NonRudeHWND | Freeze/root 不标全屏 |
| PPT Presentation（绘制、选择预览、穿透） | 同上，不因 PPT 本身全屏而取消 | 同上，只影响 Inkeys 两画布 | 不把 Inkeys 画布标为全屏；PPT应用自己的全屏保持原有行为 |
| Whiteboard（进入完成及活动） | 取消排除，覆盖完整窗口/屏幕 | 移除两窗 NonRudeHWND | 保留 Freeze 为唯一 taskbar/activation anchor，由既有入口标记全屏 |
| 白板退出或进入失败回滚 | 恢复两窗 1px 排除 | 恢复两窗 NonRudeHWND | 清除 Freeze/root 显式全屏标记 |

**最小推荐是两画布 owner-thread region + NonRudeHWND 的组合。** region负责真正让出可见/输入底行，NonRudeHWND负责明确禁止 Shell 的全屏自动识别；两者职责不可互代。该方案保留完整 `GetWindowRect/GetClientRect`、client原点、backbuffer和文档viewport，避免为1px跨场景切换重建Draw3尺寸资源。这里的“1px间隙”明确指窗口实际可见/输入形状排除该物理行；如果用户另外要求 `GetWindowRect` 本身也必须少1px，那才选择真实height-1方案，并承担resize和旧帧竞态处理。

PPT主栏既有5DIP贴底规则及进入/退出放映规则不由本画布策略改动。不要将“PPT里Inkeys画布明确非全屏”解释成要阻止PowerPoint/WPS自己的全屏或强制任务栏显示。

### 2. Files found / 代码位置

| 文件 | 已确认作用 |
| --- | --- |
| `Inkeys/IdtMain.cpp:3144-3148,3190-3209` | 公共完整主屏尺寸与双画布创建规格、DComp/ULW style准备 |
| `Inkeys/Inkeys/Window/Window.cpp:1157-1227` | HWND创建、record发布、首次ShowWindow、创建失败清理；属性/初始region应在首次显示前落实 |
| `Inkeys/Inkeys/Window/Window.cppm:58-80` | Whiteboard激活style策略，仅Freeze与主Drawpad发生style转换 |
| `Inkeys/Inkeys/Window/Window.cpp:1425-1540` | 白板window mode owner线程style/显隐事务与逆序回滚 |
| `Inkeys/Inkeys/Window/Window.cpp:1772-1791,2249-2267` | SetBounds(Drawpad)统一同步主/selection两个HWND |
| `Inkeys/Inkeys/Window/Window.cpp:2144-2246` | Primary/Presentation/Hidden互斥显隐、真实窗口状态检查与capture释放 |
| `Inkeys/Inkeys/Window/Window.cpp:607-616,2331-2349` | overlayFullscreen原子期望与Freeze/root的ITaskbarList2调用 |
| `Inkeys/IdtState.cpp:232-248,1774-1982` | Draw3就绪门禁与白板Entering/Active/Exiting事务 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1770-1783` | WM_SIZE发布跨线程resize请求 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:8227-8263` | renderer→presenter→CommitSize的真实resize事务 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp:505-550` | ResizeBuffers与尺寸资源重建，旧新左上角交集保留，不拉伸 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:566-592,1234-1256` | ULW以DIB尺寸重新提交HWND大小，以及双presenter resize |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h:249-273` / `.cpp:918-925,2185-2186` | committedWidth/Height、resizeCount可观测；不代表已完成新尺寸成功呈现 |
| `Inkeys/IdtFreezeFrame.cpp:55-72` | 桌面Freeze本来采用主屏高度减1px |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp:49-56,100-123,142-192` | 白板背景从PrimaryBounds取得完整显示器覆盖，ULW实际改变Freeze窗口呈现大小 |

### 3. Shell合同：MarkFullscreen(FALSE)与NonRudeHWND

微软官方 [`ITaskbarList2::MarkFullscreenWindow`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-itaskbarlist2-markfullscreenwindow)（本次读取页面更新日期2026-05-15）明确说明：FALSE仅移除显式标记，Shell仍会使用自动检测；Windows7起，可以在窗口显示前 `SetProp(hwnd,L"NonRudeHWND",reinterpret_cast<HANDLE>(TRUE))`，让Shell明确不将该窗口当作全屏，且不再回退到自动识别。

因此：

- **region单独不足以证明非全屏**。region并不改变client RECT，Shell启发式内部细节也不是稳定、可由源码假设的合同；完整RECT可能仍被检测为全屏。
- **region + 官方NonRudeHWND可以分别满足两个需求**。不依赖私有属性猜测，不引入高版本函数静态导入；语义最低Windows7符合现有目标。
- 属性必须由本进程Window Service在首次显示前设置。场景切回桌面/PPT时应先隐藏受影响窗口、设置属性/region并核对，再恢复原可见性，以满足“before showing”而非假设对已显示窗口改属性立即被Shell重新评估。
- 白板进入前移除两窗本任务添加的属性，并清region，恢复完整可见/输入形状；保持Freeze既有显式全屏及非TOPMOST窗口组策略。无需把每个owned popup都单独MarkFullscreen(TRUE)。
- [`SetPropW`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setpropw) 要求在窗口销毁前移除本应用添加的属性；失败读取GetLastError。该属性值是布尔标识而非真实可CloseHandle句柄。移除用RemovePropW，仅移除本任务自己拥有的属性，不处理外来属性；用GetPropW复核最终状态，NULL不能单独区分“本来没有”与移除失败。

### 4. 两条方案的技术比较

#### 4.1 推荐：双region + 属性

- 对每个实际HWND读取屏幕RECT，以窗口坐标表达region：完整window region减去目标显示器 `[monitor.bottom-1,monitor.bottom)` 一行与窗口的交集。现有主屏全屏窗口通常直接是 `(0,0,width,height-1)`；所有计算使用物理像素，不能乘DPI/UI倍率。
- 相同策略、bounds、HWND generation下无更新；任务栏升起/降下不影响两画布策略，不得每次Shell动画或每帧调用SetWindowRgn。白板才改变gap策略，普通Desktop↔PPT不改变策略。
- 双窗均设置region，即使DrawpadPresentation本已固定穿透，也保证其可见像素不覆盖底1px。辅助窗不创建RTS、Host、document，仍使用同一个Draw3输出，不破坏固定WS_EX_TRANSPARENT。
- `SetWindowRgn`不修改outer/client RECT和原点；原始画布、RTS标尺和最后一行文档坐标保持。region外像素只是不呈现在屏幕上，不删除Stored stroke/墨迹数据。
- SetWindowRgn会发送WM_WINDOWPOSCHANGING/CHANGED；Draw3可能请求一次full present（WindowControl.cpp:1762-1767）。不应改变size；即使系统默认过程投递同尺寸WM_SIZE，DrawingController.cpp:8232-8234会提前返回。验证要求是resizeCount不因region场景切换增长，而不是假设没有任何消息。
- [`SetWindowRgn`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn)成功后系统接管HRGN，失败时调用方删除。快照回滚不能重复使用已经移交系统的HRGN；需GetWindowRgn复制或保存纯几何描述后重建。
- [`DirectComposition Basic Concepts`](https://learn.microsoft.com/en-us/windows/win32/directcomp/basic-concepts#composition-target-window)明确各composition层裁到target window visible region，这支持region路线适配DComp的可行性；实际DComp/ULW和Win7命中仍需要验证。
- 白板恢复用SetWindowRgn(nullptr)清本任务region；如果原窗有自定义region，必须恢复原快照而非无条件清空。当前产品未找到已有SetWindowRgn使用，但事务不能把“不存在”写成永久假设。

#### 4.2 实际HWND height-1

该路线直接满足outer/client RECT比屏幕少1行的额外意图，减少依赖Shell自动识别完整RECT的风险；仍建议NonRude明确表达非全屏，单纯MarkFullscreen(FALSE)并不能表达此意图。

成本及不能省略的合同：

1. 初始Drawpad和DrawpadPresentation的两个创建规格都少1px（IdtMain.cpp），使Host从一开始拿到正确尺寸；白板进入/退出再通过Window Service同步改双窗，不能只缩当前可见那个。
2. SetBounds(Drawpad)→ApplyDrawpadBounds一次DeferWindowPos两窗，但当前只有返回值，没有双窗RECT读回、部分失败回滚或场景revision。新需求下需补齐成功与失败路径。
3. WM_SIZE只发布pending；真正尺寸重建在绘制线程。renderer.Resize会ResizeBuffers并重建L2/L1/L0/backbuffer/激光尺寸资源，只保留旧新左上交集；presenter.Resize同步主与selection输出。此成本在场景切换时发生，不是可忽略的1px整数赋值。
4. 现有Draw3WorkspaceReady只验workspace/output/content revision，没有尺寸条件；若选height方案，必须额外等待期望committedWidth/Height与新尺寸成功呈现，才能对外宣告白板输入/背景ready。Host committedWidth/Height是在resize成功后写入，并不单独证明真实帧已提交。
5. **旧ULW帧会重新改回HWND尺寸**：TransparentPresentation.cpp:566-592从GetWindowRect只取origin，却以旧DIB width/height为ULW.psize。owner刚缩窗而绘制线程还未消费resize时，旧帧可能把窗口放大回去（主ULW及selection输出都有风险）。必须有geometry revision/共享呈现事务或可延期的stale-size gate，拒绝过期提交，不能只对最终Height设一次值。普通resize延期不应误判为设备故障。
6. 强行只减ULW.psize且保持Draw3 window_.Size/backbuffer全屏，会分裂窗口/输入/selection映射；DComp模式也不受此修改控制，不是合格的小修。
7. 输入标尺Host.cpp:1319-1334读client rect、ClientToScreen及Monitor像素；无需重注册RTS或改Modeler，但需验证client覆盖/最后一行坐标、活动contact resize和selection输出原点一致。不得按新height缩放已有笔迹或保存数据。

### 5. 白板进入/退出与回滚具体接线

现状直接确认：

- `IdtState.cpp:1774-1804`：Inactive→Entering先CollapseAuxiliaryPanels/cancel pointer，再EnterWhiteboardWindowMode；成功后发布Whiteboard workspace、Freeze whiteboard状态并暂时关闭Drawpad激活/输入。
- `:1808-1822`：Entering被取消直接转Exiting并发布主workspace；尚未提交overlayFullscreen(TRUE)。
- `:1822-1858`：Whiteboard workspace ready、Freeze背景active匹配后，`SetOverlayFullscreen(true) && SetOverlayTopmost(false) && SetClickThrough(Drawpad,false)`；失败隐藏/回滚UI及Freeze、fullscreen false、LeaveWindowMode、Topmost true、转Exiting。
- `:1930-1948`：Exiting中重入白板也重新EnterWhiteboardWindowMode，不能复用半完成事务。
- `:1950-1982`：主workspace ready后撤白板背景；恢复DrawpadPresentation click-through，再`SetOverlayFullscreen(false) && LeaveWhiteboardWindowMode() && SetOverlayTopmost(true) && SetClickThrough(Drawpad,selectionMode)`；失败continue按250ms节拍重试。

推荐最小实现入口为Window Service现有白板window-mode事务，而非在三个IdtState分支里分别调用SetWindowRgn/SetProp：

1. 在首次CreateWindowFor完成HWND、**首次ShowWindow前**，对Drawpad和DrawpadPresentation分别应用当前scene默认策略（启动Desktop/PPT都是gap+NonRude），失败按既有创建失败清理，不对外发布假ready。
2. 在ApplyWhiteboardWindowMode快照加入两窗可见性、属性归属/值、region几何；模式相同可no-op，但前提必须是当前generation和policy已真实应用，不能在半失败后以whiteboardWindowMode_值直接短路。
3. 当前`RequiresOverlayActivationStyleTransition`只让Freeze与主Drawpad切style，**DrawpadPresentation会被跳过**（Window.cppm:58-80 / Window.cpp:1482）。新policy循环必须显式包含两画布，不能把region/property附在现有style分支内。
4. 进入：隐藏两画布，清gap region，移除本任务NonRude属性，检查两窗最终状态；成功后恢复各自原可见性/既有互斥surface策略，再提交whiteboardWindowMode_。IdtState随后既有ready/Freeze激活/Fullscreen TRUE链保持。
5. 退出：先既有Fullscreen FALSE，再窗口mode事务隐藏两画布、应用gap+NonRude、核对后恢复适当可见性；最后Topmost true。此路径对从白板回PPT与回Desktop一样适用。
6. 任一双窗policy或style步骤失败均逆序恢复完整快照，保持mode未提交；回滚本身失败保留阶段/角色/error与未收敛标志，并按现有状态循环重试；不能“第一窗成功、第二窗失败”仍声称整对已应用。owner线程不等待绘制线程，也不跨owner同步调用持Draw3渲染锁。
7. 窗口重建、真实SetBounds(Drawpad)和白板group最小化/恢复必须保持policy；region基于HWND局部，bounds改变需要重新计算。普通Show时可廉价核revision，不每帧重设。销毁前移除NonRude并清本任务资源。

### 6. 全屏标记现状与必须诚实记录的失败语义

- `SetOverlayFullscreen`（Window.cpp:607-612）先store期望bool，再调用RequestTopmostRefresh；不是Shell结果读回。
- `ApplyOverlayFullscreen`（:2331-2349）只选Freeze，缺失才root。没有对Drawpad或DrawpadPresentation调用MarkFullscreen(TRUE)。它是void，CoCreateInstance失败直接return，HrInit/MarkFullscreenWindow失败均不向上层传播。
- 因此`IdtState`的whiteboardWindowStateReady目前不能证明Shell COM标记成功。新任务若把“白板继续全屏”解释为保留既有机制，可保留调用边界并报告它的best-effort现状；若验收需要明确标记成功，应在本任务明确加入有限改动：ApplyOverlayFullscreen返回真实HRESULT/成功状态，在RefreshTopmost传播结果，且本轮白板rollback保持一致。不要无声地宣称已有同步API已保证Shell结果。
- Stop路径Window.cpp:949-952将overlayFullscreen=false并在销毁前调用ApplyOverlayFullscreen，符合清理意图，但同样不证明实际COM成功。
- 新NonRude应用对象是两画布，**不是白板Freeze锚点**；不要让Freeze在标记TRUE时残留阻止全屏属性。普通PPT/桌面不添加画布TRUE标记，白板继续通过Freeze锚点表达整个窗口组的全屏意图。

### 7. 验证建议

规划无窗口测试应直接调用生产策略/事务helper：

- 以Desktop/Presentation/Whiteboard为独立输入，覆盖任务栏四边、auto-hide两档、各DPI/UI倍率；前两scene始终1物理px+NonRude，白板始终无gap且允许fullscreen，scene策略与任务栏采样无依赖。
- regionwindow-local换算：主屏完整窗口、负原点、移动/resize、屏幕上下排列；PtInRegion断言底行排除、倒数第二行保留，白板完整恢复；两个角色相同几何，selection固定穿透样式不变。
- 事务注入SetProp/RemoveProp/region/Show/style任一步第一/第二窗失败，断言逆序rollback、原可见性保留、mode未提前提交、重复请求幂等、重建代不复用旧ack。
- Enter失败回滚、Entering取消、Active退出、Exiting重新进入、PPT进入/退出与白板相互覆盖；只有最新scene policy生效。
- region方案：Host committedWidth/Height、GetClientRect、文档viewport、RTS标尺不变；scene切换不增加resizeCount；policy不变零region/属性调用，Shell升降只影响Bar。
- height方案若采用：双窗RECT实际一致、期望尺寸resize+成功present后ready、ULW旧帧无法回写旧尺寸、资源失败恢复、活动stroke/selection两窗和持久化完整性。
- 完整solution Debug|ARM64+Headless --no-window；默认不启GUI。真实Win11 DComp/ULW、Win7 SP1+平台更新ULW、SuperTop/UIAccess、Explorer重启、多DPI显示器与物理笔/鼠标的任务栏升起/非全屏分类需要另经用户授权验收。

## Related specs

- `.trellis/spec/native-desktop/rendering-and-ui.md:1619-1620,1713-1719`：白板Freeze唯一activation/taskbar锚点，owner group非TOPMOST；退出顺序；桌面Freeze少1px、不得标全屏。
- `.trellis/spec/native-desktop/draw3-integration.md` “窗口与所有权”及“样式与透明度”：双窗同Host、selection只呈现、Window Service唯一窗口所有者、固定穿透与DComp/ULW代际。
- `.trellis/spec/native/runtime-and-rendering.md:20-24,762-788`：跨线程resize release发布、绘制线程资源重建、活动笔迹/ResizeBuffers flags合同。
- `.trellis/spec/native-desktop/startup-preview.md`：启动首次Show、UI3呈现的证据边界；不因本画布研究修改PPT/Bar5DIP。

## Caveats / Not Found

- 没有编译、GUI或真实跨进程命中测试，不能将主Drawpad/selection是否实际阻止Explorer触发描述为已复现，也不能用region/API成功替代Shell验收。
- region组合让出实际可见/输入区域，outer/client RECT仍完整；本次需求转达没有要求GetWindowRect物理变小，因此推荐它。如果用户明确要求真实窗口尺寸少1行，该组合不满足该额外合同，需要height方案。
- 微软官方NonRude文档提供明确非全屏意图接口；全屏启发式内部算法仍无稳定完整合同，不应尝试从region反推Shell检测结果。
- 本文新增调查不修改产品，也不覆盖其他研究文件；最终任务PRD/design应统一采用最新明确范围，避免旧文档可选范围A/B继续被当作最终需求。
