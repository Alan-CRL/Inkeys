# 本期上下文摘录
以下为原规范的带行号摘录。修改具体层前仍须读取相应原文件完整相关章节；摘录避免native注入截断，不替代原规范。

## .trellis/spec/native-desktop/configuration-i18n-and-assets.md

1: # Configuration, I18n, and Assets
2:
3: 本文使用 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。尤其注意：“新配置 module”是代码年代/结构描述，不表示维护者已宣布它取代传统配置。
4:
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
25: `【待确认】` 维护者需要决定：未来字段按什么标准选择系统、是否计划迁移 `deploy.json`、发布包是否预置这些文件，以及向后兼容周期多长。Codex 在答案明确前不应自行把字段迁到“看起来较新”的 module。
26:
27: ## 配置修改的证据型审查清单
28:
29: 以下是从并存状态推导的 `【合理推断】`，不是声称项目已有统一迁移政策：
30:
31: 1. 搜索字段、JSON key、默认值和 UI 写回的全部读写点，先确定真实所有者。
32: 2. 已在 `INKEYS_CONFIG_SCHEMA` 中的字段继续通过该 module 读写，除非任务明确更改 schema/持久化格式。
33: 3. 仍由 `SetListStruct` 管理的字段保持当前文件兼容；迁移需同时设计旧值读取、默认值、写回和回滚。
34: 4. JSON key 改名属于持久化格式变化；没有迁移代码时不能假设旧 key 会自动升级。
35: 5. 记录设置窗口写的是哪套配置，避免 UI 显示值、运行时缓存与磁盘文件分叉。
36:
37: ## 设置页废弃配置清理合同

270: | 路径 | `【直接确认】`的角色 |
271: | --- | --- |
272: | `Inkeys/src/i18n/zh-CN.jsonc` | 基准语言 |
273: | `Inkeys/src/i18n/zh-TW.jsonc`、`en-US.jsonc` | 其他语言 |
274: | `Scripts/i18n.ps1` | `check`/`sync` 工具 |
275: | `Scripts/i18n.zh-CN.snapshot.jsonc` | 基准语言同步快照 |
276: | `Inkeys/IdtI18nKeys.g.h` | `sync` 生成的 key header |
277: | `Inkeys/IdtI18n.cpp/.h` | 运行时加载和查询 |
278:
279: `【直接确认】` `Scripts/i18n.ps1 sync` 以 `zh-CN.jsonc` 为基准，更新其他语言结构、快照和 `IdtI18nKeys.g.h`；生成 header 带有 “Do not edit manually” 标记。
280:
281: 因此，文案变更应：先改基准 JSONC，再按任务授权运行 `sync`、处理翻译标记、运行 `check`，并审查所有生成差异。只审计时使用 `check`；`sync` 会写文件，不能在未授权的只读/文档任务中执行。不要手工编辑 `IdtI18nKeys.g.h`。
282:
283: 实验选项的控制台诊断由旧 `SettingsUI/Experimental/ConsoleOutput/TouchArea` 文案键和 `Experimental.Inkeys3.ConsoleOutput.TouchArea` 保存键继续承载；扩大输出内容时只更新三语言可见名称/说明，不改键以保留已有用户选择。
284:
285: ### 生成键标识符与 Win32 宏合同
286:
287: #### 1. Scope / Trigger
288:
289: 新增或重命名 i18n JSONC 节点时适用。`i18n.ps1 sync` 会把每个路径段直接生成为 C++ 成员标识符，因此这不只是 JSON schema 变更，也是生成 C++ API 变更。
290:
291: #### 2. Signatures
292:
293: ~~~text
294: "Dialogs/Common/OK" -> I18nKey.Dialogs.Common.OK
295: ~~~
296:
297: #### 3. Contracts
298:
299: - 每个路径段必须能生成合法 C++ 标识符，并且不得与目标翻译单元可见的 Windows/项目宏同名。
300: - 特别禁止使用被 `<Windows.h>` 定义的 `MessageBox` 作为节点名；对话框文案使用 `Dialogs`。
301: - 不通过调整 `#undef` 顺序掩盖 schema 命名冲突，因为生成头可被多个全局模块片段以不同顺序包含。
302:
303: #### 4. Validation & Error Matrix
304:
305: | 条件 | 必须行为 |
306: | --- | --- |
307: | 节点名与 Win32/项目宏冲突 | 在基准 JSONC 重命名节点，重新 `sync`，再更新调用点 |
308: | 非默认语言出现新 key | 填完翻译标记后 `check` 必须 100% 通过 |
309: | 生成头与 JSONC 不一致 | 重跑 `sync`，不手改生成头 |
310:
311: #### 5. Good / Base / Bad Cases
312:
313: - Good：`Dialogs/Common/OK` 生成 `I18nKey.Dialogs.Common.OK`，在包含 `<Windows.h>` 的单元中可直接编译。
314: - Base：普通非冲突节点按现有生成规则使用。
315: - Bad：`MessageBox/Common/OK` 会让成员 token 在预处理期被改写为 `MessageBoxW`。
316:
317: #### 6. Tests Required
318:
319: - 运行 `pwsh ./Scripts/i18n.ps1 sync` 和 `check`。
320: - 搜索生成头中的新成员名，并完整构建至少一个包含 `<Windows.h>` 且消费该 key 的翻译单元。
321:
322: #### 7. Wrong vs Correct
323:
324: ~~~cpp
325: // Wrong：MessageBox 可被 Windows.h 宏展开。

## .trellis/spec/native-desktop/rendering-and-ui.md

39: ## `Inkeys.UI.Bar`
40:
41: `【直接确认】` `Inkeys/Inkeys/UI/Bar/` 按 `Bar.State`、`Bar.Atomic`、`Bar.Animation`、`Bar.Theme`、`Bar.UI`、`Bar.RenderingAttribute`、`Bar.Button`、`Bar.Layout`、`Bar.Rendering`、`Bar.RenderLoop`、`Bar.Interaction`、`Bar.Initialization`、`Bar.Main`、`Bar.Zoom`、`Bar.Format` 等文件拆分状态、主题、布局/动画、渲染、交互和窗口初始化逻辑。
42:
43: `Bar.RenderLoop.cpp::BarUISetClass::Rendering` 把 Bar 注册为共享 UI3 调度器客户端。Bar 从共享 D2D device 创建自己的 device context 和 GDI-compatible、BGRA premultiplied target bitmap；单帧入口经 `ID2D1GdiInteropRenderTarget::GetDC` 和 `UpdateLayeredWindowIndirect` 提交计算出的脏区，idle 等待与最多 60 FPS 节拍由共享调度器统一负责。
44:
45: ### 定格状态机合同
46:
47: `Inkeys.UI.Freeze` 是定格业务状态的唯一所有者。普通绘制、选择和工具按钮切换不清除或恢复定格；独立ClickThrough与Freeze互斥，进入穿透关闭定格；再次点击穿透按钮保留墨迹并恢复记住的Pen/Shape/Eraser，定格继续关闭。穿透时点定格则恢复记忆工具后开启定格，不恢复旧定格状态。PPT或Whiteboard进入时关闭并标记不可用；不可用定格按钮从主栏隐藏，退出工作区只恢复可用性，不恢复进入前状态。合并式/分离式的尺寸与显隐见[选择与穿透合同](configuration-i18n-and-assets.md#选择与穿透操作模式合同)。主栏Clear空页且已Selection时使用同一 `Disable` 和 `BarButtonDisabledContentOpacity`，不参与悬停/按压/点击；空页仍在绘制或穿透工具时保持可点击以进入Selection。
48:
49: 以下仅是对现有 Bar 的 `【合理推断】` 修改约束，不适用于所有窗口：
50:
51: - 保持 target/current 动画状态与最终绘制值的现有分工；
52: - 状态变化需要触发现有渲染唤醒/脏区路径，静止时避免无意义刷新；
53: - 维持 premultiplied alpha 语义；
54: - `GetDC/ReleaseDC`、`BeginDraw/EndDraw` 在成功与失败路径成对；
55: - 不在没有线程安全依据时跨线程传递 device context/target。
56:
57: ### 主栏按钮统一状态派生合同
58:
59: - 范围为主栏A1/B/A2与运行时Pierce/More/Divider。配置/注册基础尺寸、用户可见性、成员顺序是输入；运行时有效size/hide/业务Selected/Disable与相关内容由StateUpdate单一计算/应用入口给出。不同业务事件不能直接覆盖有效字段，组件Load后所有特殊/普通按钮均一致化，不能只修Freeze或只补Recall。
60: - 工具、内容、Whiteboard/PPT/Freeze可用性、RecallView、fold/More状态按同组快照计算。内容缓存只抑制重复内容动画，不抑制尺寸/显隐重投影；重复同状态不重启内容或几何动画。 Recall的view与所选锚点可见性在网格锁内捕获到帧对象，主栏颜色和历史属性Advance显式使用同份帧值，不在解锁后重新读全局view或有效hide拼接旧目标。
61: - Recall保持3秒steady_clock保护、接受命令前事实、灰色点击延期及popup身份；灰色表观不能以通用Disable拦截所有点击而丢弃延期资格。事件持Recall锁只更新state/deadline，锁外请求；不能在该锁或Draw3回调内盲目同步整套UI更新。
62: - 列表重建与投影使用短而明确的同一串行边界。不得持该锁跨D2D呈现、Window Service/SetWindowPos/owner等待；UpdateRendering(false)轻请求与timer不能等待可能同步等待owner的渲染锁。面板Close归位等可重入副作用与纯投影分开，避免锁内Close重新取得Recall/更新锁。
63: - 主栏布局失效必须比较稳定成员身份/顺序、有效size/IsVisible以及隐藏成员锚点语义，不能只比较总宽度。等宽的两枚2×1与单枚2×2、成员替换/顺序变化仍进入现有统一批次；扩张曲线继续由实际宽度决定。Selected/Disable、文字/颜色、hover/emph/pressScale/动画val不属于几何指纹。
64: - RenderLoop继续持有像素目标SetTar、current/target、CanJoin与换边后半程重开、隐藏锚点、命中和旧新dirty；统一业务入口不得SetDirect跳过动画。主按钮脉冲/停靠及独立Surface/属性按钮保持原所有权，不扩大为全UI属性框架。
65: - 真实Bar无窗口回归须调用RegisterBuiltInComponents/SyncLegacyExtensionButtons/Load/UpdateRendering与生产timer/state入口：相同业务styleKey下Separate绘制增删组件仍为Pierce上2×1/Freeze下2×1；覆盖0/1/2/3+组件及overflow、模式/内容/PPT/Whiteboard、历史期限与精简态、等宽转换/动画中途/重复刷新。源码合同只作补充，不以复制逻辑或字符串替代真实生产调用；未启动Draw3 Host/HWND时不能声称验证Accepted命令或系统WM_TIMER投递。
66:

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
1540: #### 3. Contracts
1541:
1542: - HWND 所属线程只处理 capture/cursor/IME、生命周期和 `ImGui_ImplWin32_WndProcHandlerEx`；专用 recursive mutex 串行访问 ImGui IO，因为 Win32 backend 的 `SetCapture`/`ReleaseCapture`/IME 调用可能同步重入同一 WndProc。ImGui context、DX11 backend、NewFrame/draw/Present 只由 RenderPipeline 线程使用。
1543: - Setting 可见且可呈现时返回 `Continue`，因此独自按统一 16,666,667 ns 上限连续绘制；隐藏时逆序释放 backend、SRV、swap chain/RTV，返回 `Idle`。resize、遮挡和 epoch 变化由 `SessionState` 决策，不在 WndProc 直接操作 D3D。
1544: - 文件写盘、Shell、模态确认、重启和 DDB 操作进入单一 FIFO。配置命令在生产者线程冻结 JSON 或 `Inkeys::Config` 副本；配置 I/O 命令的 worker 不读取实时 `setlist`、`pptComSetlist` 或 `Inkeys::config`；绘图状态同步命令按下文专用合同读取最新运行时状态。停止时禁止新命令，并按 FIFO 排空已接收命令。
1545: - 自动更新是既有长期网络服务；FIFO 只串行化其启动命令，不把长期下载循环占用为业务 worker 本体。
1546: - 退出顺序固定为：停止显隐/输入生产者，隐藏并请求 Settings，渲染线程 drain session，`Unregister(Settings)`，排空业务 FIFO，join Bar/PPT，停止 Window Service，最后 `RenderPipeline::Shutdown()`。
1547:
1548: #### 4. Validation & Error Matrix
1549:
1550: | 条件 | 必须行为 |
1551: | --- | --- |
1552: | `Present` / `ResizeBuffers` 返回 device removed/reset/internal error | 返回 `DeviceLost`，由唯一管线线程恢复 epoch |
1553: | 普通 Present/resize 失败 | 只返回 `Retry` 并重建 Setting session，不影响 Bar/PPT |
1554: | `DXGI_STATUS_OCCLUDED` | 后续帧用 `DXGI_PRESENT_TEST` 探测，成功后恢复绘制 |
1555: | 隐藏或 generation 变化 | 在渲染线程逆序释放旧 session；可见时按新 epoch 重建 |
1556: | 业务命令完成 | 发布不可变 completion snapshot 并只请求 Settings |
1557: | 退出时 FIFO 尚有配置写盘 | 排空后 join，不得 request_stop 后清空未执行命令 |
1558: | Bar 在 Setting drain 前观察到 `offSignal` | Bar 返回 `Idle`，不得以 `Stop` 提前结束共享线程 |
1559:
1560: #### 5. Good / Base / Bad Cases

1707: ### 3. Contracts
1708:
1709: - Window Service 的受管线程拥有 Mag host/child、Freeze、DrawpadPresentation、Drawpad、四个 PageControl HWND、Bar、Setting 和 DisplayObserver；Whiteboard 复用两个 `PptBottom*` HWND，不再创建独立左右窗口。创建结果通过 promise/future 返回，stop callback 用事件唤醒 `MsgWaitForMultipleObjectsEx`。Setting 仍是普通 app window，但不再自带绘制线程。
1710: - style、owner、显隐、bounds、click-through、HiMsg bind/unbind 和销毁必须投递到 HWND 所属线程。`UpdateLayeredWindowIndirect`、D3D present 和明确要求 HWND 的外部 API 是受控跨线程例外。
1711: - 四个分页 HWND 的业务显隐只由 PageControl 提交：`Acquire()` 注册客户端后以 `RequestAll()` 唤醒，成功呈现后 `SetBounds/Show`，Hidden 目标使用 `Hide`。`TopWindow()` 不得无条件显示分页窗；`IdtWindowsIsVisible.pptWindow` 只表示客户端注册就绪，否则尚未布局的 `(0,0)` 初始矩形可能滞留左上角并干扰新接触。
1712: - 基础 overlay owner 链只在创建时建立：`Mag -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar`；Mag 缺失时 Freeze 为根。Drawpad 是 DrawpadPresentation 的顶层 owned popup，不是 `WS_CHILD`；该传递链必须保证 Bar/PPT 高于两套画布表面。Presentation mode 中 overlay 保持 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`。Whiteboard mode 是显式例外：Freeze 切为唯一 `WS_EX_APPWINDOW`、可激活和任务栏锚点；Drawpad 清除 `WS_EX_NOACTIVATE` 但保留 `WS_EX_TOOLWINDOW`；其他成员仍为非任务栏辅助 UI。Bar 必须高于所有 PPT；共享底窗或其他 PPT show、`PromotePptWindow` 只把目标窗放到 Bar 正下方。置顶刷新只对链根调用一次 `HWND_TOPMOST` 或 `HWND_NOTOPMOST`，且 Whiteboard mode 强制 NOTOPMOST。Win32 会把根的 topmost band 变化传播给 owned popup；刷新后非根出现 `WS_EX_TOPMOST` 不能证明代码对它执行了独立置顶，必须审查 `SetWindowPos` 调用点。白板期间对 Freeze 调用 `ITaskbarList2::MarkFullscreenWindow`，退出和销毁前清除。
1713: - PPT 可见性 `false -> true` 发布完成后立即请求一次根置顶刷新；成功后连续可见状态去重，失败时保留 pending 并由既有 500ms 发布节拍重试，离开放映取消 pending。PageControl 的 present 成功不代表 HWND 提交完成：`SetBounds/Show/Hide` 任一步失败都返回 RenderPipeline `Retry`。Draw3 surface 切换失败同样保留 reconciliation pending，由既有 250ms 状态节拍重试；只有窗口提交成功后才更新 drawpad ready 事实。
1714: - 主栏定格按钮在 `Freeze::Toggle()` 前后比较 `IsActive()`；只有 `false -> true` 转换立即显式调用一次 `RequestTopmostRefresh()`。`true -> false` 和 PPT/白板导致的不可用点击不提交。Bar 入口不得持有 HWND 或直接调用 `SetWindowPos`；立即刷新失败不回滚定格状态，由既有周期性根刷新继续收敛。
1715: - `Inkeys.UI.Freeze` 把业务 flags 与单调 `revision` 放在同一原子状态字中，每次真实 Toggle 或 PPT/Whiteboard 工作区切换发布完整 `StateSnapshot`。observer 只能把 `{revision, active}` 投递到 latest-wins mailbox 并唤醒 `MagnifierThread`，不得直接操作 HWND；迟到通知按 revision 丢弃，失败回退只能调用 `DeactivateIfRevision(request.revision)`，禁止无条件 `Toggle()`。
1716: - `MagnifierThread` 是桌面定格唯一的 Magnifier 更新/显示协调入口。一次有效 active revision 依次校验 Window Service 当前 Host/Child、保持 Host 透明并显示 Host/Child、从当前角色 HWND 重建并提交排除列表、提交一次 source、invalidate/redraw，最后再校验 revision 与当前句柄后揭开 Host；Reveal 后仍需复查，过期结果必须立即 conceal 且不得提交 applied/visible。过滤、source 或必要显示步骤任一失败都停止成功链、条件撤销对应 Freeze revision，并允许后续新 revision 重试。inactive、工作区切换、停止和退出只透明并隐藏 Magnifier Host/Child，不隐藏 Bar/Drawpad，不改变 owner/topmost/focus/taskbar。`FreezeFrameWindow` 只保留 Freeze/PPT/Recall surface 绘制，不得调用 Magnifier 更新、显隐或 alpha API。
1717: - 桌面定格不得调用 `SetOverlayFullscreen`：Magnifier 和 Freeze surface 都继续使用显示器高度减 1 像素，Shell fullscreen 标记只属于白板工作区。Window Service 重建 Magnifier 生命周期时先停止旧 coordinator，再在新 `PrepareMagnifierWindow` 生命周期 reset；每次事务重新读取当前 Host/Child 和完整排除集合，不能缓存旧 HWND。
1718: - Setting 创建时及电脑操作状态（`IdtSelection`/`IdtClickThrough`）的 owner 必须为 null；绘制状态通过 `SetSettingOwnedByDrawpad(true)` 把 Drawpad 设为 owner，回到电脑操作状态通过 `SetSettingOwnedByDrawpad(false)` 清除 owner 并以 `HWND_NOTOPMOST` 退出画布置顶链。owner 修改必须投递到 Setting 所属线程，重复请求幂等；目标 HWND 缺失、Win32 调用失败或最终 `GW_OWNER` 不符合请求时返回 false，部分修改必须回滚原 owner。`SyncDraw3State()` 保存带版本的最新期望值，提交失败后由 `StateMonitoring()` 的 250ms 节拍重试；提交与 applied 写回必须串行，并在持锁后丢弃过期版本，避免旧命令覆盖新模式或产生假收敛。该动态 owner 是 Setting 唯一受控例外，不得直接从工具按钮、快捷键或渲染线程修改。
1719: - Setting owner attach 与 owned Show 必须立即完成根/相对层级并核验位于两块画布上方，不能只设置 GW_OWNER 后等周期刷新。不独立执行 HWND_TOPMOST；根策略造成的 WS_EX_TOPMOST 传播允许变化，其余应用样式保持。重复 owner且实际层级正确时幂等；刷新失败返回 false 保留重试。Setting owner-thread 不得同步等待 overlay 命令造成 USER32 互等，正常业务/状态协调线程在 Setting 命令完成后进行根刷新。
1720: - Setting 始终是可激活的顶层 owned/unowned popup，而不是真正的子窗口：style 固定为 `WS_POPUP | WS_CLIPCHILDREN`，不得包含 caption/thickframe/minimize/maximize/system-menu；ex-style 包含 `WS_EX_APPWINDOW` 且排除独立 topmost/layered/noactivate/toolwindow。owner 切换不得改变 style、除根传播 TOPMOST 外的 ex-style、图标、窗口线程或任务栏按钮；显示时由所属窗口线程主动 restore/show 并请求 foreground/active/focus；`WM_GETMINMAXINFO` 把最小/最大 track size 固定为配置尺寸。
1721: - Setting 自绘标题栏的可拖动空白区必须在 ImGui 消息转发前处理 `WM_NCHITTEST` 并返回 `HTCAPTION`，由 Win32 非客户区移动循环负责拖窗；当前几何为缩放后的 `0 <= y < 32 DIP` 且 `0 <= x < 914 DIP`。右侧 `914..960 DIP` 关闭按钮和正文返回 `HTCLIENT`，继续由 ImGui 处理隐藏与内容交互。该命中合同不得因 Setting 是否拥有 Drawpad owner 而分支，`WM_MOVE` 继续记录最终位置。
1722: - 主栏设置按钮调用统一 `UI::Setting::Toggle()`：隐藏时显示并激活；已显示但真实 Setting HWND 不是前台线程的焦点窗口时，只重新提交 Setting `Show` 命令以 restore/foreground/active/focus；仅在已显示且前台窗口与真实焦点 HWND 都是 Setting 时隐藏。Bar 保持 `WS_EX_NOACTIVATE`，只读取焦点状态，不得从 Bar 输入线程直接调用 `ShowWindow`、`SetForegroundWindow`、`SetActiveWindow` 或 `SetFocus`。
1723: - `DibSurface` 是 top-down 32-bit BGRA DIB Section。HDC、HBITMAP、旧选入对象和像素地址由 RAII 管理；复制为深拷贝，移动为 `noexcept`，resize 先成功创建新资源再交换。
1724: - HiMsg 成功 `Get/TryGet` 即消费；合成输入通过 `Enqueue` 原样进入同一队列。触摸转单指的 mouse message、坐标、按键状态和 marker 字段不得丢失或重新解释。
1725: - HiMsg 默认接受 Win32 系统生成的触摸兼容 mouse；这是公共库行为。只有已经自行处理 `WM_TOUCH` 并合成单指输入的 Inkeys Bar/PPT binding 才设置 `WindowSpec::messageCallback`，在 HiMsg subclass 自动入队前对 `IsTouchGeneratedMouseMessage(message, GetMessageExtraInfo())` 返回 `Action::Discard`。该 callback 仍继续原 WndProc；真实鼠标和不带 touch flag 的笔兼容 mouse 必须保留。
1726: - 所有使用 HWND 的渲染/交互 `jthread` 必须先 stop/join，最后才调用 Window Service `StopAndJoin()` 逆序销毁窗口。
1727:
1728: ### 4. Validation & Error Matrix

## .trellis/spec/native-desktop/draw3-integration.md

55: ## 窗口与所有权
56:
57: - `WindowService` 是 `WindowRole::Drawpad` 与 `WindowRole::DrawpadPresentation` HWND 的唯一创建、显示、隐藏和销毁者。Draw3 只把主 Drawpad 交给 `AttachExternal(HWND, callbacks)`；辅助窗不得绑定 RTS、WndProc mailbox、document 或第二套 Host。
58: - Draw3 不创建/销毁顶层窗口，不修改标题、owner 或 Z 序；样式和 `Primary/Presentation/Hidden` 可见性变化必须通过 Window Service 的 owner-thread 命令完成。
59: - owner 层级保持 `MagnifierHost -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar`，使 PPT/Bar 通过传递 Owner 关系始终高于两套画布表面。Drawpad 仍是顶层 owned popup，不是 `WS_CHILD`。Draw3 presenter 不得调用 `SetWindowPos`、`SetParent` 或直接改 owner；`SetBounds(Drawpad)` 必须同步两窗。
60:

116: ## Scenario: Desktop UInk 自动保存事务与退出屏障
117:
118: ### 1. Scope / Trigger
119:
120: 修改 Draw3 workspace、Desktop Clear/正常退出、`saveSetting.enable`、Desktop UInk 完整保存、Clear 后恢复、每日索引或 Host/controller/worker 生命周期时必须应用本合同。Whiteboard 与 PPT 不读取 Desktop 开关；PPT 使用下一节的独立事务。
121:
122: ### 2. Signatures
123:
124: - `Bridge::Workspace { Desktop, Whiteboard, Presentation }`。
125: - `DesktopAutoSavePolicy::ShouldCapture(workspace, enabled, hasVisibleContent) -> bool` 与 `CompleteDesktopClear()`。
126: - `DesktopAutoSaveService::{Submit, SubmitLoad, TryTakeCompletion, CloseAndDrain}`。
127: - `DesktopPersistenceCompletion { operation, status, trigger, fileGuid, loadedSnapshot }`。
128: - `Bridge::CommandType::PrepareExitAutoSave` / `CanvasCommandType::PrepareExitAutoSave`。
129: - 产品根路径：`GetCurrentExeDirectory() + L"\\Inkeys\\AutoSave"`。
130:
131: ### 3. Contracts
132:
133: - 自动保存只接受 `Desktop + saveSetting.enable + 当前页非空`。Desktop slot 与 Whiteboard/PPT slot 独立；访问 PPT 不会污染 Desktop 保存资格，也不得将 PPT 内容导出到 Desktop 目录。
134: - 每次有效 Desktop Clear 都替换全局最近恢复点。开关关闭、提交拒绝或保存失败时保留一份 `Draw3UInkCanvasSnapshot`；保存和索引 durable 后 completion 以 `fileGuid` 匹配恢复点并立即释放该快照，只保留 committed path locator。
135: - Undo 总是先撤回当前区间 Stroke；到 `undoFloor` 后，内存恢复点直接物化，磁盘恢复点经 `SubmitLoad(fileGuid)` 严格读取并在绘制线程全量 replay。成功后消费恢复点，以 `undoFloor=0` 物化恢复画布并关闭再次跨 Clear 的资格；恢复出的 Stroke 可继续逐笔 Undo 到空，之后再次 Undo no-op。
136: - 触发点只有 Desktop Clear 的破坏操作之前和正常退出安全点。Undo/Redo（包括未来跨 Clear 撤销）不得调用自动保存；空画布或开关关闭时不得捕获快照、入队或创建目录。
137: - DrawingController 线程只按 runtime history 捕获当前可见、完全自有的 CPU 快照；UInk 编码、文件和索引 I/O 由 Host 拥有的单一可 join 串行 worker 执行。worker 不得持有 controller/GPU 引用，也不得 detach。

194: ## Scenario: PPT 三态 slot、UInk 自动保存与当前进程恢复
195:
196: ### 1. Scope / Trigger
197:
198: 修改 Presentation target、Desktop/Whiteboard/PPT 切换、PPT 页切换、dirty revision、Presentation UInk 元数据/导入、索引、Host 退出屏障或多放映切换时必须应用本合同。本期只自动恢复当前 Inkeys 进程成功保存的 entry；稳定 COM 下的 SlideID 重排、插入和删除属于当前进程范围，跨进程冲突和用户决策仍留待后续。
199:
200: ### 2. Signatures
201:
202: - `Bridge::PresentationTarget { key, bindingMode, sourceIdentity, bindingToken, pageIndex, totalPages, slideId, slideIds, targetRevision, processLocalIdentity }`。
203: - `Bridge::ProductState::presentationTarget` 与 scene-stamped `Bridge::Command` 共享不可变 target；`PresentationReadyIdentity` 是不含字符串/拓扑的固定大小 runtime ready 值。
204: - `PublishProductPresentationTarget(Bridge::PresentationTarget) -> optional<uint64_t>` / `ClearProductPresentationTarget()`。
205: - `PresentationAutoSaveService::{Start, SubmitSave, SubmitLoad, TryTakeCompletion, CloseAndDrain}`。
206: - `PresentationSaveRequest { target, mutationRevision, snapshot, clearPageGuid?, clearIntervalOrdinal? }`。
207: - `PresentationLoadRequest { target, kind=Current|PreviousInterval, pageGuid?, intervalOrdinal }`。
208: - `ShouldQueuePresentationSave(mutationRevision, queuedRevision)` 与 `ShouldEvictPresentationSlot(hasCommittedFile, mutationRevision, queuedRevision, committedRevision, loadPending)`。
209: - `StablePresentationTopologyChanged(previous, next) -> bool`；只比较同一 StableSlideId 文稿的有序 `slideIds`。
210: - `ImportApplicationOwnedPresentation(document, expectation) -> Draw3UInkImportResult`。
211: - 产品布局：历史 Base 轨仍用 `<AutoSave>/presentation/index.json(.bak)`；同一 source/key 的另一 binding 按 `slide-id/` 或 `page-index/` 独立 sidecar 轨保存。每轨的 schema v2 index 指向 `files/<fileGuid>_<transactionGuid>.uink` 版本文件；旧 schema v1 `files/<fileGuid>.uink` 仍按原路径只读/续存，不删除未知文件。
212:
213: ### 3. Contracts
214:
215: - 绘制线程持有 Desktop singleton、Whiteboard singleton 和按 `(PresentationKey, SlideBindingMode)` 分 lane 的 Presentation slot；每个 slot 自带 document、history、当前页、页 runtime、非零 slot generation 与持久化 revision。切换必须先准备目标 lane，再 park/swap 整个 slot 并执行完整 GPU reset/replay；旧 page-index 与新 StableSlideId 不共用 document、页面或 fileGuid。

345: ## Scenario: 选择模式、当前页内容与可撤回 Clear
346:
347: ### 1. Scope / Trigger
348:
349: 修改产品模式桥接、Drawpad 显隐/消息穿透、Bar 选择布局、runtime history、Clear/Undo/Redo、页面切换或高精度计时器时必须应用本合同。
350:
351: ### 2. Signatures
352:
353: - `Bridge::ProductState::selectionMode : bool`，默认 `true`；由 `IsComputerInteractionMode(StateModeSelect)` 将 `IdtSelection`/`IdtClickThrough` 统一投影，保留独立 UI 工具身份；不得从 `WS_EX_TRANSPARENT` 反推模式。
354: - `DrawingControllerRuntimeObserver::currentPageContentChanged(void*, bool, uint64_t)`。
