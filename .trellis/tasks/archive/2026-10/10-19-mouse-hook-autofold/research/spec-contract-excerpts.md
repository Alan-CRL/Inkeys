# 本任务相关规格摘录与按需阅读位置

四份原规格超过注入单文件32KiB上限。本文件仅从当前原文抽取相关段落，并给出行号；以后改动对应层时还应直接读取原文对应完整章节，不能把缺失的后半部当作没有约束。不修改全局context限制。用户本任务明确允许鼠标Hook改造，其他键盘/输入合同仍保持。

## .trellis/spec/native-desktop/input-and-ink.md : 102-122

102: ### UI3 Bar 接触消息归一化合同
103:
104: `【直接确认】` Bar 注册的 `WM_TOUCH` 会把主接触（包括当前系统上报的 Pen）转换成带内部来源标记的 `ExMessage`。Windows 同时可能为同一接触派发 Pointer 兼容鼠标消息，其 `GetMessageExtraInfo()` 的来源签名为 `0xFF515700`（掩码 `0xFFFFFF00`）。
105:
106: `【直接确认】` Pointer 兼容鼠标副本必须在 Bar 的窗口过程和 HiMsg 入队回调两个入口都丢弃；真实鼠标 Down/Move/Up 保留原有 HiMsg 入口及原生按压代次；普通点击不依赖 Raw。Raw 仅在连续拖动状态机中补充越界 Move/Up，不进入 HiMsg，不能替尚未拖动的候选提交点击。低位 `0x80` 只能用于诊断 Pen/Touch 分类，不能决定一次接触的输入归属，因为同一支笔的 Down/Up 可能出现不同低位值。触摸长按右键抑制依赖这条单一 `WM_TOUCH` 路径，不得重新保留 Pen 兼容鼠标分支。
107:
108: 按钮点击完成必须等待明确的 `WM_LBUTTONUP`；`WM_MOUSEMOVE` 的 `MK_LBUTTON` 缺失不能被解释为抬起，否则会把接触或窗口激活阶段的移动误判为第二次点击。
109:
110: ## RTS 初始化与回退
111:
112: `【直接确认】`：
113:
114: - `IdtRts.cpp` 的不同初始化路径包含关闭 flicks、启用多点触摸、注册 stylus plugin 等操作；
115: - 初始化/插件注册失败会写日志并允许鼠标回退；
116: - 画板窗口处理 tablet gesture、触摸注册和光标显示；
117: - 实际输入模式还读取配置和硬件/窗口状态。
118:
119: `【合理推断】` 现有 SafeRTSInit 和鼠标回退是启动兼容边界。改变 RTS 初始化时应保留可诊断的失败路径，除非任务明确决定取消回退并验证启动行为。
120:
121: `【待确认】` 仓库没有给出各 Windows 版本、触摸屏、数位笔驱动和 RTS 版本的正式支持矩阵。
122:

## .trellis/spec/native-desktop/rendering-and-ui.md : 949-965

949: 	const RECT& elasticBaseBounds, double zoom,
950: 	double horizontalOutsetDip = BarBottomDockVisualLimitDip,
951: 	double verticalOutsetDip = BarBottomDockVisualLimitDip) noexcept;
952: ~~~
953:
954: #### 3. Contracts
955:
956: - 水平捕获目标是当前显示器 `monitorBounds` 的水平中点；参与居中的几何只包含主按钮、主栏及可见描边的联合外框。扩展面板不参与中心计算或拉伸，只按其按钮锚点的二维映射差值刚性跟随。
957: - 水平捕获只在竖向已 `BottomDocked`、主栏展开且联合外框中心进入 `BarBottomDockCenterThresholdDip = 40 DIP` 时生效；边界值允许捕获，严格越界立即脱离。竖向继续使用 `BarBottomDockThresholdDip = 20 DIP`；折叠或竖向脱离必须结束水平捕获。
958: - 横纵 mode、phase、elastic offset、直接窗口位移和显示环境共用 `bottomDockTransitionSerial` 的同一发布事务。交互线程必须先计算完整两轴候选，再发布偶数稳定 serial；渲染线程不得提交只包含一轴新状态的帧。 deferred barrier 与屏幕抓手 X/Y 也必须在偶数 serial 发布前写入；首次抓取、普通采样和 DPI/显示屏障遵守同一顺序，不能在稳定 serial 之外补写其中一轴或 barrier。
959: - 输入、折叠和白板请求的写者必须先通过 `BeginBarBottomDockTransition` CAS 独占奇数 serial，禁止多个 `fetch_add` 写者重叠形成伪稳定偶数。渲染自动写回必须用 `TryBeginBarBottomDockFrameTransition` 同时满足未抓取且 serial 仍等于本帧已消费版本；资格失效就丢弃候选并保留 dirty 重试，不得替旧位图升级到更新输入的 serial。在 PrepareLightingAndDemand 之前作废候选时，必须调用 `BarPresentDecision::RequireVisualRetry()` 同时补 visual demand 和 full dirty；仅 RetainForRetry/RequireFullDirtyRetry 不会令 ShouldPresent 为真，不能保证静止后的正确帧上屏。写事务仅包含短暂本地/原子更新，不跨 D2D 呈现或外部锁。
960: - 这两组偶数/奇数版本快照（期望 `bottomDockTransitionSerial` 与已呈现 `bottomDockPresentedMappingSerial`）的写者在领取奇数后、写 relaxed 载荷前设置 release fence，写完后以 release 发布偶数。读者先 acquire 读偶数，读取全部 relaxed 载荷后设置 acquire fence，再重读版本并仅在相等时接受。新增读取点或写者必须保持这个配对；只靠末尾 acquire load 不能保证载荷先于校验。运行期形态变化仍用原有 deferred barrier 处理，不能把未来 barrier 当成成功呈现。
961: - 按住时 Free/Dragging 阶段由输入 tracker 拥有，渲染不得强制写成 Stable 或程序化居中；不一致的折叠/居中候选需等完整新 tuple。显示位置的拖动所有权也读取同帧 `bottomDockDragActive`，不另读实时 direct phase 拼接帧状态。
962: - 松手发布 `bottomDockDragActive=false` 后，`directWindowDragPhase` 仍可能短暂为 `Dragging`。只要仍有待吸收直移，渲染线程在取得稳定 release tuple 后必须返回 Retry；下一帧先把 phase 原子切到 `Absorbing`，在 `directWindowDragMutex` 内吸收 translation、重基准成功快照并执行 `PositionUpdate()`，然后才允许释放态布局和既有换向动画。
963: - 交互重基准、直接 `SetWindowPos` 失败回滚和下一手势起点只读取最后成功呈现快照。水平 tracker 的输入必须是指针驱动、未形变的主体中心；形态呈现 barrier 只能确认窗口位移已提交，不得用视觉主体中心改写抓取偏移或 tracker 基准。显示环境重基准也不能用当前 pointer 减已限位/滞后的呈现中心重抓；抓取偏移保持原逻辑点，仅通过 `ScaleBarDragGrabOffsetForZoom` 随实际呈现 zoom 比例缩放。水平捕获与脱离首帧必须从已显示像素播入恢复平移，不能把逻辑锚点切换表现为 HWND 跳变。
964: - D2D 几何计算完成后到 ULW 之间，交互线程仍可发布采样。最终目的地必须在 `directWindowDragMutex` 内读取同一稳定偶数 serial 下的目标/实际 translation、最新 deferred barrier 和成功呈现 serial：当前帧已包含尚未上屏的 barrier 时，位图必须与帧内 translation 成对提交，后续普通采样不能迫使首次吸附饥饿；若更新的形态/显示 barrier 晚于当前帧，或提交前读到发布中/不一致 tuple，则放弃 ULW、配对 EndDraw 并保留完整重试，不推进成功快照或伪报 ULW 失败。barrier 已成功呈现后的普通过期帧继续使用实际已呈现 translation，避免拉回已直移 HWND。
965: - 主按钮及 Logo 使用水平刚性抓手映射并保留既有竖向果冻；主栏背景、普通按钮、图标和文字使用独立二维主体映射。主栏近端随抓手移动，远端吸附稳定居中边界：右向展开从中心左侧进入时拉伸、越过中心后压缩，左向展开镜像；捕获、脱离和恢复中重新捕获的远端都必须从上一成功像素按新旧 HWND 位移反推。绘制、dirty、viewport/capacity、PointLight 逆映射、第三光源接受区和命中必须按刚性抓手/主体映射分类；竖向普通抓手输入保留 24 DIP 视觉保护；水平 40 DIP 捕获带内的刚性抓手偏移以及从成功像素反推的远端初值必须精确保留，不能按 24 DIP 截断而造成捕获或脱离跳帧。水平弹簧须允许连续从超出常规保护范围的有效初值恢复，保持原频率、阻尼与动画开关语义。水平捕获带、实际抓手/远端恢复范围、Gaussian 外扩和抗锯齿余量都进入保守包络。

## .trellis/spec/native-desktop/rendering-and-ui.md : 1521-1539

1521: ### Setting 统一渲染与业务队列合同
1522:
1523: #### 1. Scope / Trigger
1524:
1525: 修改 Setting 显隐、WndProc、ImGui session、swap chain/RTV/SRV、配置持久化、模态动作或退出顺序时适用。
1526:
1527: #### 2. Signatures
1528:
1529: ~~~cpp
1530: bool Setting::Initialize();
1531: void Setting::Shutdown() noexcept;
1532: void Setting::Show();
1533: void Setting::Hide();
1534: void Setting::Toggle();
1535: bool Setting::IsVisible() noexcept;
1536: WNDPROC Setting::WindowProc() noexcept;
1537: FrameResult RenderSettingFrame(const FrameContext&);
1538: ~~~
1539:

## .trellis/spec/native-desktop/rendering-and-ui.md : 1905-1905

1905: 低级鼠标 Hook 必须在创建 Hook 的受管 `std::jthread` 中卸载。停止端设置事件，Hook 线程用 `MsgWaitForMultipleObjectsEx` 同时等待事件和消息，不得使用 detached thread 或只依赖 `PostThreadMessage(WM_QUIT)`。

## .trellis/spec/native-desktop/configuration-i18n-and-assets.md : 5-24

5: ## 两套配置系统分别服务哪些代码
6:
7: | 配置系统 | 文件与磁盘位置 | `【直接确认】`的使用范围 |
8: | --- | --- | --- |
9: | 传统 `SetListStruct` | `IdtConfiguration.h`；`IdtConfiguration.cpp::ReadSetting/WriteSetting`；`globalPath + L"opt\\deploy.json"` | `IdtMain`、`IdtDraw`/`IdtDrawpad`、`IdtFloating`、`IdtFreezeFrame`、`IdtHistoricalDrawpad`、`IdtMagnification`、`IdtPlug-in`、`IdtRts`、`IdtWindow`、`Net.Update`、`Bar.Main`、`Setting` 等仍有读写 |
10: | `Inkeys.Other.Config` | `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`；`Inkeys::Config` class、全局实例 `Inkeys::config/configOnce`；`Config::ReadAll/ReadMini/Write`；`globalPath + L"Inkeys\\Config\\main.json"` | schema 中可见 `Config.AutoClean`、`Info`、`UI.Bar.Zoom`、`Experimental.Inkeys3.UI3.Animation/EdgeLighting`、`PlugIn.PPTHelper` 等；消费者包括 `IdtConfiguration.cpp`、`IdtMain.cpp`、`IdtPlug-in.cpp`、`Net.Update*`、`Bar.Zoom*`、`Bar.Main*` 和 `Setting*` |
11:
12: `【直接确认】` `INKEYS_CONFIG_SCHEMA` 集中声明新 module 已支持的字段；源码中还把启动读取区标注为“新配置 Test”。这说明新系统正在实际使用，但不足以证明迁移已完成或所有新字段必须进入它。
13:
14: ### 容易混淆的真实边界
15:
16: - `【直接确认】` `Inkeys.UI.Bar` 是唯一产品悬浮栏入口；传统 `setlist.Experimental.Inkeys3.UI3` 路由字段和 `useInkeys3UI` 已删除，写旧配置时清理遗留 key。
17: - `【直接确认】` 新配置中的 `Experimental.Inkeys3.UI3.Animation` 控制 UI3 动画；`Experimental.Inkeys3.UI3.EdgeLighting` 控制 UI3 边缘点光及第三鼠标光。它们都不是选择新旧悬浮栏的总开关。
18: - `【直接确认】` Bar 的 zoom 配置走 `Inkeys.Other.Config`；Bar 的其他行为仍可读取传统 `setlist`，因此不能把 Bar 简化成“只用新配置”。
19: - `【直接确认】` PPT helper 新字段由 `IdtPlug-in.cpp` 等读取，但 `IdtPlug-in.cpp` 同时仍使用传统 PPT/交互设置。
20: - `【直接确认】` 两套系统写入不同 JSON 文件；全仓未见通用的双向同步或完整迁移器。
21: - `【直接确认】` `Config.AutoClean` 会影响传统 `WriteSetting`，使其清空旧 `setlistVal`；这是一个明确交点，不代表两个 schema 普遍同步。
22:
23: `【历史/兼容】` `SetListStruct` 仍是当前大量主路径的真实接口，不能仅因其文件名较旧而标记废弃。
24:

## .trellis/spec/native-desktop/configuration-i18n-and-assets.md : 416-437

416: - UI.Bar.StartupPlacement 属于main.json，IdtAtomic<int>默认0；0居中底栏展开、1左角主按钮、2右角主按钮。读盘仅接受JSON int/uint且isInt()可表示的0..2；浮点、超宽整数及其他类型直接回退0，不能先窄化后Normalize。NormalizeStartupPlacement统一非法回退0，Mini/正式UI使用同一冻结值。
417: - 设置恢复Preset导航页，仅启动后位置说明和现有combo。旧deploy.json的Preset与画笔记忆继续退休，不恢复读写或旧条目；启动动画开关仍常规。
418: - 下拉仅更新偏好并QueueConfigWrite冻结副本，明确下次启动生效，不触发本次Bar移动/折叠或锁内窗口同步。
419: - 所有新标题/说明/三选项为zh-CN/zh-TW/en-US，通过i18n sync/check生成头与快照。
420: - 新增导航必须检测整个侧栏与固定页脚/分隔线边界，不以单个新页面回放替代整体布局验证。
421:
422: ## 选择与穿透操作模式合同
423:
424: ### 1. Scope / Trigger
425: 修改合并式/分离式配置、Select/Pierce工具身份、Freeze互斥、主栏Clear禁用、说明卡片及三语短标签时适用。仅UI3产品；不恢复旧Draw2穿透共存模型。
426:
427: ### 2. Signatures
428: - `UI.Bar.SelectAndClickThroughMode: IdtAtomic<int> = 0`，main.json中仅0（Combined）/1（Separate），不上传。
429: - `GetSelectAndClickThroughMode() noexcept -> int`；`SetSelectAndClickThroughMode(int) -> bool`，返回配置是否变化，不直接写盘。
430: - `StateModeSelectEnum::IdtClickThrough`末尾追加，既有枚举值不变。
431: - `IsComputerInteractionMode(StateModeSelectEnum) noexcept -> bool`判定Selection或ClickThrough。
432: - `ChangeStateModeToClickThrough() -> bool`显式幂等进入；`ToggleClickThroughFromButton() -> bool`用户按钮入/出；`ChangeStateModeToSelectionFromButton() -> bool`；`ToggleFreezeFromButton() -> void`。
433:
434: ### 3. Contracts
435: - schema缺失或非法值回Combined；读取严格验证JSON整数0/1，不允许大整数窄化误变Separate。
436: - 设置每帧取实时模式；先改变配置/工具，沿既有SyncDraw3State的render defer guard同步，再通过Setting FIFO冻结配置payload。重复选择不写盘；不得持ImGui锁同步等待HWND。
437: - 设置切Combined→Separate时，当前有墨迹Selection映射ClickThrough；反向ClickThrough映射Selection，始终保留内容；其他工具不变。

## .trellis/spec/native-desktop/draw3-integration.md : 89-103

89: 静态核对两处 gate、回退/清理边界和无激活合同，完整 Solution Debug|ARM64 与 `InkeysHeadlessTests --no-window`。后者不验证真实 HWND/RTS/Z 序；真实窗口/笔输入和白板行为须另按授权验证，不能将编译或属性检查认作视觉证明。
90:
91: ### 7. Wrong vs Correct
92:
93: Wrong：只等 RtsReady/首帧后刷新；Correct：每代窗口准备失败直接返回，成功后调用 StartProduct，保留后置恢复。实际 Windows/RTS 系统内部因果仍需故障现场证据。
94:
95: ## 设备与线程
96:
97: - Draw3 独立创建 D3D11.1 hardware-first/WARP-fallback 设备、context、DXGI factory、交换链和呈现资源，禁止引用 `Inkeys.UI.RenderPipeline` 的设备。
98: - Hardware 与 WARP 都可落在 FL11.0；这不保证动态 SRV 的 `WRITE_NO_OVERWRITE`。生产 renderer 必须在当前设备上查 `D3D11_OPTIONS::MapNoOverwriteOnDynamicBufferSRV`，不支持或查询失败时普通笔与 Shape 逐批 DISCARD 且 buffer offset 为零；支持设备保留环形路径。此上传门禁与 ULW 的 `FLIP_SEQUENTIAL` 独立，不能为修一项而替换交换链模式。
99: - UI 线程只向 bridge 发布不可变快照和命令；renderer、document、history、RTS 消费只在 Draw3 绘制线程进行。
100: - 进程只允许一套 Draw3 Host、绘制线程和 `RealTimeStylusInput` producer；Desktop、Whiteboard 及每个 `PresentationKey` 的 document/history 是该绘制线程独占的独立 slot。辅助 HWND 仅是同一最终 backbuffer 的 presentation target。退出顺序固定为停止命令生产、停止 RTS、唤醒绘制线程、执行最终保存屏障并排空 worker、释放双 presenter/设备，最后由 Window Service 销毁两窗。
101: - `DrawingControllerRuntimeObserver::drawingActivityChanged` 只按全部 physical contact 的聚合值发布 `0→1/1→0`；每个命令消费边界与帧末复核，避免 Down 后提前 `continue` 漏报。Host 通过独立 `HostRuntimeCallbacks` 注入产品通知、再次去重，并在 Run 正常返回、异常或 stop/join 后补发一次 false；Draw3 核心不得直接依赖 Bar。
102:
103: ## 样式与透明度
