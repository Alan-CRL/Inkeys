# Research: 真实视图调整产品整链测试设计

> 2026-10-22 勘误/实施前置：原报告关于 preset 为空就能安全执行 Business 裸 UpdateRendering 的推断遗漏了 IdtPen 下 ThicknessDisplayUpdate 对未初始化 wordMap 的访问。原断言不足，不能按下文历史方案继续裸刷新。必须复用已有真实 RefreshToolState readiness 门，或完成真实UI图初始化；不能伪造 wordMap、ready、窗口 ACK。详见 live-product-initializing-investigation.md。

- Query: 在不使用 Computer Use 的新 GUI 授权内，用最小专用早退入口验证真实 Business→原 Mag→真实 Tracker→IdtState→WindowService 成对 ACK→Draw3 视口/墨迹→退出保存，识别不能用 seam 代替的边界。
- Scope: internal
- Date: 2026-10-22

## Findings

### 当前证据和测试边界

本研究只读源码，不 build、不启动应用/GUI、不操作桌面/截图；写入范围仅本 research 文件。父会话的捕获实现者正在实际取帧测试，不能与之并发占用桌面。

已读 `.trellis/workflow.md`、`.trellis/spec/native-desktop/index.md`、`scroll-follow-product.md` 和父任务 `research/delivery-current.md`。现有 capture/tracker/product/persistence 的软件证据均保留，不能将新的有限整链测试称为真实笔触控/全部主栏交互验收。

### 源码位置

- `Inkeys/IdtMain.cpp:1377`：wWinMain 的严格早退测试参数分派，在配置、单实例与普通启动之前。现有 `--scroll-follow-hidden-test` 在此运行。
- `Inkeys/Inkeys/Business/ScrollFollow.cpp:38`：仅协议测试的 thread-local TestPorts，真实产品未安装时走原外部绑定。`StartCapture` 约 109、Project/Acknowledged 约 140/146、Frame 约 190、Service::Tick 约 259、Available 约 378、RequestToggle 约 389。
- `Inkeys/IdtState.cpp:274`：私有 PublishDraw3State，把真实工具与临时 followProjection 的 selection OR 后发布。`RequestScrollFollowProjection:817` 只置 dirty；`StateMonitoring:2149/2164` 负责消费 dirty、发布，再 Reconcile。
- `Inkeys/IdtState.cpp:521`：真实 ReconcileDraw3PresentationState；它直接访问 `Window::GetService()`，在 output ready、期望身份/工具/projection 全匹配后调用 SetDrawpadSurfaceVisibility；约 673–675 成功后写 followAckBridge/mode/projection。
- `Inkeys/IdtState.cpp:830`：Acknowledged 会核对真实运行、非 offSignal、dirty 已消费、projection/mode/bridge 完全对应；不能只改某个 ack 布尔。
- `Inkeys/Inkeys/Window/Window.cpp:2481`：真实两窗批量显隐，先检查句柄、执行 DeferWindowPos/回退 ShowWindow，读回两窗可见性才成功。`GetService:2910` 是单例。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:3717`：现有隐藏 Host 测试用局部 Service，不是上述单例；约 3770 直接 BeginFollow，直接 PublishProductState(selection)、直接 channel Publish 目标；约 3801 的 channel->Handoff(101) 是明确的 ACK seam。
- `Inkeys/IdtMain.cpp:3370–3560`：生产原 Mag host/child、双画布、Settings/DisplayObserver 的 WindowSpec 和创建回调；`3600` 私有 HostStartOptions 可显式指定 autoSaveRoot；普通启动则固定 GetCurrentExeDirectory()+Inkeys/AutoSave。
- `Inkeys/IdtMain.cpp:3744–3769`：生产 TopWindow/UI3/FreezeFrame/StateMonitoring/MagnifierThread 的实际线程次序。
- `Inkeys/IdtMagnification.cpp:109`：当前排除列表由十个已知 WindowRole 取得，而不是 EnumWindows 任意同进程顶层窗；未注册为这些 Role 的专用 source HWND 不在列表，旧 PoC 约 851–857 也断言 source_unfiltered。
- `Inkeys/IdtMagnification.cpp:992–1110`：旧原 Mag PoC 的实际 WindowService callbacks 与 MagnifierThread 启动/停止顺序；它不等于正式 Capture 的整链。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:285`、`Bar.Button.cpp:947–979`：未建立 preset 时 UpdateRendering 的 StateUpdate 会早退，随后只发 RenderPipeline Request；不是必须启动 UI3 才能走 Business RefreshUi 的直接证据。
- `Inkeys/IdtState.cpp:791/116`：SyncDraw3State 还提交 Settings owner；夹具应提供真实但隐藏的 Settings HWND，避免每轮无效句柄重试。
- `Inkeys/Inkeys/Display/Display.cpp:1363/1449`：Initialize 发布真实主屏快照；Capture 必须有这个非 fallback 快照。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:2176–2203`：真实 RTS 初始化成功时选 RtsExtended/Basic，失败才启用 Win32Mouse。`Host.h:229` InputMode、约 246–268 的 options；没有现成公开“强制鼠标后端”配置。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:2539`：HiddenTestContact 是显式合成 ContactSnapshot mailbox 输入。它可以制造真实墨迹，但不能称为真实 OS/RTS 输入。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2004–2188`：WM_MOUSE 路径含光标/来源/兼容判定；RTS 已启动时仅 PostMessage 鼠标消息不能保证收到 RTS 墨迹。

### 最小建议：单个早退、专用可见产品夹具

不建议从旧 hidden-test 直接删除 invisible() 就运行。它绕过 Business/Capture/IdtState，而且用错了 ACK 所访问的 Service 实例。建议新增有限的 harness CLI（例如 `--scroll-follow-live-test`），严格 exact argv、harness-off 返回 2，仍在 wWinMain 配置/单实例/普通启动之前运行；不扩大现有 hidden-test 的语义。

必须是真实生产对象：

1. 原 DPI awareness helper 在 HWND 前执行；初始化 Display::Initialize、必要 COM，建立专用 DiagnosticOutput 和 null/file logger；config/setlist 使用进程内默认值，仅设保存 enable 和有限笔宽/颜色，不调用用户配置 Read/Write。Host options 明确 autoSaveRoot 到唯一新建私有 run 目录，绝不用空路径假装落盘。
2. `Window::GetService()` 单例创建真实原 MagHost/MagChild，保留 PrepareMagnifierWindow/MagnifierHostCreated/MagnifierChildCreated/ShutdownMagnifierWindow；再创建主屏像素尺寸、坐标与原产品一致的 Drawpad/DrawpadPresentation，使用原 DrawpadMsgCallback 与原样式策略/HostStyleCallbacks。附上隐藏 Settings 和 Freeze、DisplayObserver 即足够 Desktop-only 路径；只创建需要的 Role，不启动 PPT。
3. 启动唯一真实 ProductHost，采用生产 Automatic/DComp 可用性和原样式预置，不强改渲染参数。StartOptions 不打开隐藏contact注入即可测试真实输入；若后面不得不用合成墨迹，报告明确另列输入seam。启动真实 MagnifierThread；用真实 SyncDraw3State/ChangeStateModeToPen 建立工具。
4. 启动真实 StateMonitoring 线程：它消费文件私有 followProjectionDirty。只有循环调用公开 ReconcileDraw3Presentation 不够，因为 dirty 不会清、bridge selection 也不更新。无需新建替代状态机，也不要公开“写 ACK”API。
5. 控制线程只调用真实 Business::RequestToggle/RequestStop/PrepareAction，等待公开 Snapshot/真实 Channel 观察值，不安装 Business TestPorts。不调用 BeginFollow、Publish Target、RequestScrollFollowProjection 或 Handoff 来推动成功。这些可作为只读证据而非替代产品入口。
6. Source 的 main/UI 线程持续消息泵。既有 WaitUntil 仅 sleep，若 source HWND 放在夹具主线程而它阻塞等待，图案不会重绘；必须 MsgWait/PeekMessage 等消息感知等待，或专门 source owner 线程。

Desktop-only 没有进入白板、PPT、Settings可见页时，不要求启动 Bar::Initialization/RenderPipeline/FreezeFrame/TopWindow。Business 的 UpdateRendering 在 preset 未建立时安全早退；实际屏幕墨迹由 Draw3 独立 device/presenter 驱动。**此结论只支持最小整链，不涵盖主栏按钮像素/点击**。有限夹具应先验证无 UI3 的默认调用；若具体回调实际需要资源，应沿原 RenderPipeline Initialize 注册生命周期，不能 fake ACK 或手改全局 ready。

### Source、真位移与 GT

- 创建专用 popup source 覆盖主屏 workArea（最好覆盖全主屏，taskbar区不计Tracker ROI），不绑定任何排除 Role。可当前进程创建，因为现代码只排除固定 Role；启动时记录实际 filters 未含 source。如果将来捕获改成所有同进程窗口排除，应改独立专用 pattern 子进程，而不是给生产排除列表开白名单后仍声称测试原逻辑。
- 内容为确定 seed 的非周期纹理/文字/角点混合，母图至少十几屏高、横向也留余量；图像值只取决于世界坐标与 seed，不逐帧重新随机，避免将动画假当运动。绘制从母图固定 viewport offset 取样。不要只重复棋盘格，避免光流多解；初始就应有全 ROI 特征。
- source 的 WM_MOUSEWHEEL/WM_MOUSEHWHEEL 或 finite WM_APP 指令只改变整数 world offset，WM_PAINT 实际 BitBlt 母图，paint 完成后写自己的 monotonic paint sequence、offset、QPC/图案hash作为外部 GT。该值绝不传入 Tracker/Follow Target。
- 先停留取得 Tracking；移动每步例如 8 或 16 原图像素，每步 hold 足够 2–3 个捕获周期。先垂直、停止、反向、水平，最后十屏特征接力（纹理无限/足够大）。20 FPS 是采样目标，先据实际 PW 耗时决定动作 cadence；不要以超快 source 动画制造无意义追踪失败。
- 快照允许落后一帧，运动后留稳定 hold，等“样本序号继续增长且视口稳定”再对比总 offset。不能要求截图回来的每张图等于刚刚 WM_PAINT 的最新 GT；每帧可由原 source 的空间锚点/内容标记识别对应 GT。
- 底层输入穿透另测：Tracking 后用 OS wheel SendInput 在安全 source 中心触发，source 必须收到真实 wheel 并记录消息/offset，同时主 Drawpad 隐藏、Presentation 可见且固定透穿样式。直接 SendMessage(source, WM_MOUSEWHEEL) 只能证明滚动→视觉链，不能证明系统命中穿透。

### 输入、墨迹与屏幕像素

- 推荐优先真实 SendInput mouse Down/Move/Up，记录 input mode、Host input/undo counters、实际 Stroke 完成、屏幕墨迹。若 RTS 接管导致纯鼠标消息只移动光标，不要增加强制鼠标fallback或设置虚假RTS状态；先测试系统 SendInput 是否被真实 RTS 接收，不行则记录无法自动生成真实墨迹的阻塞，需要真实设备/用户一笔。
- Harness HiddenTestContact 可以在独立标注案例产生 A/B，确实走真实 Controller/Canvas/presenter，但属于“输入注入，真实捕获/追踪/ACK/像素”的有限证据；不能包装成完整真实输入，也不能替代硬件笔。
- A 用可见高对比宽笔（保留合理原属性）画一段足够长且留在跟随后的屏内。截图使用屏幕 DC BitBlt 的真实合成画面，包含 DComp/ULW 两窗；不要用 Capture Mag 图确认墨迹，因为 Mag 正好排除了墨迹窗口。
- 用 source 母图已知RGB对照选区做差，定位 A 的颜色/连通区域或中心线；比较跟随前后屏幕中心线平移应等于 source 内容平移，记录 max/median pixel error及等待帧数。截图可保存到私有目录由工具view_image检查；不能只看 Viewport 数值就说屏幕跟随成功。
- 另在同一阶段取 source与可见墨迹屏幕截图的非墨迹区域，确认 source本身位置/GT，画布透明背景没有污染底图。开始/退出交接的 screenshot 序列记录两窗可见对；窗口visibility ACK只证明状态，不证明两次截图间绝无黑/消失帧，报告限定采样覆盖。

### 退出、工具和最后位移保存

- 待 Tracking，走 RequestStop(Toggle) 或真实工具公开入口；ChangeStateModeToPen/Eraser 在跟踪期间先 RequestStop 并返回 false（IdtState.cpp:70/1489），需要等 Off 后重试或使用原 PrepareAction/PollAction/CompleteAction(真实 setter publication receipt) 模式。不能一次 setter false 就宣称工具已更换。
- 结束不直接 Handoff；等待实际 StateMonitoring/WindowService 逆交接成功、Business Off、Channel unblock，再起 B。检查 B 的首点=clientDown+真实viewport，A原点不变。
- 最后位移案例要让 Tracker 的最后可信结果被真实 Capture 接受/Channel发布后立刻 RequestStop，不预等 owner applied；停后文件 viewport 应与最后可信累计结果一致。不能要求尚未被任何Capture采到的 source offset 也保存，这会混淆未观察位移和屏障丢失。
- StopProduct 原 final PrepareExitAutoSave；文件 worker Drain，读取实际UInk文件，按v10检查viewport/A/B/GUID；复用既有私有保存验收的实际读取代码，写清无任意Host冷载入口，不能声称重启恢复通过。

### 退出和隔离

- 每次独立子进程一个case，唯一 run 目录；父launcher只等待自己创建的PID和process handle，不能按进程名杀现有 Inkeys。source/夹具各有自动timeout。正常路径先要求 Business stop/Off，停止StateMonitoring（offSignal真实退出标识，注意不要提前令原ACK失效），StopProduct/保存 Drain、StopMagnifierCoordinator+join，再 WindowService.StopAndJoin 在原owner反初始化，Destroy source、Display Shutdown、恢复logger。
- 超时不伪造正常释放通过。若 PW 永不返回，隔离child watchdog可以最终退出该专用进程，并记录 forced-timeout、资源未证明正常释放，生产捕获的资源独占规则不变。
- 退出协议精确次序需实现者对读真实原 shutdown/Host 要求；不要把普通 SetOffSignal 的监督/重启链随意拉入早退小夹具，亦不能把业务锁放到 ArmSupervisor 之前。

### 普通私有副本启动替代方案的风险

复制exe到私有目录可以隔离 `globalPath`、`Inkeys/Config/main.json`、`opt/deploy.json`、log、plugin和 `GetCurrentExeDirectory()/Inkeys/AutoSave`。这些位置见 IdtMain.cpp:1688/1745/2272/2600/3600，Other.Config.cpp:919。Release 单实例mutex也基于exe目录（IdtMain.cpp:1790–1820），Debug不走这段。

**但是它不构成全部隔离，不能直接作为无副作用fallback：**

- 普通启动在 IdtMain.cpp:3057–3064 查询并可能 `SetStartupState(..., L"$Inkeys")`，可改机器/用户共同的启动项。私有副本配置startUp=false也可能删改原项，不能靠复制目录解决。
- `shortcutAssistant.SetShortcut()`、StartDesktopDrawpadBlocker（约3131）、AutomaticUpdate（3228）、PPTLinkageMain（3782）及 UI3/Input全局hooks可能运行，SuperTop分支2270–2420可能派生/令牌交接，CrashHandler也有正常生命周期副作用。
- 首次UI start界面调用目前注释（3035），但不能据此保证所有resource/plugin/profile组合没有提示。复制文件必须包含同架构PptCOM依赖/资源和可用配置，并避免update.json/installer触发自动更新；不能复制整个用户私有资料。

因此优先专用早退夹具真实启动核心owners；若要普通完整启动，需有限内部私有启动mode显式禁止startup/shortcut/update/PPT/globalHook副作用，并把此模式的差异记录为fixture边界。这可能比窄Desktop夹具更大。未经确认不要启动任意正常副本，也不要用 `isolatedStartupFailure` 模式冒充产品成功：它故意跳过StateMonitoring/Magnifier等必需线程。

## External References

本次为纯内部源码调查，无网络、无新增API依赖。PrintWindow / Mag 运行可用性由实现者本轮实测；不得从文档推断同步更新成功。

## Related Specs

- `.trellis/spec/native-desktop/scroll-follow-product.md`：真实projection/owner身份/精确窗口ACK、End/文件证据边界。
- `.trellis/spec/native-desktop/scroll-follow-capture.md`：原Mag寿命、正式帧/超时、不可取消PrintWindow。
- `.trellis/spec/native-desktop/index.md`：唯一Draw3 Host与UI渲染独立、不得复活旧Draw2入口。

## Caveats / Not Found

- 没有现成“真实Business+StateMonitoring+实际可见窗口ACK”的测试入口；必须实施上述有限夹具，已有hidden-test无法只改一行成为整链。
- 没有公开的Desktop任意外部冷载Host入口，也没有现成强制Win32Mouse产品配置；不要为测试冒充其存在。
- UI3完整按钮/协程视觉点击、PPT/Word/PDF兼容性、真实笔/触控、锁屏和旧OS不在本有限夹具保证内；需要后续普通产品测试或硬件输入。
- 分析中所有代码路径均为当前工作区读到的状态；并行捕获实现者可能添加专用live入口，应复用其实际原 Mag准备，而不是另写第二套。

## 2026-10-22 后续故障调查更正

上述关于 preset 为空即可安全调用 Business 裸 UpdateRendering 的推断已被后续代码调查纠正：IdtPen 分支仍访问未建 wordMap 的 ThicknessDisplay。必须复用原 RefreshToolState readiness 门或真实初始化UI图；详见同目录 live-product-initializing-investigation.md。同时真实Main主线程COM为MTA，夹具不应STA迫使RTS失败后把fallback称为自然硬件结果。
