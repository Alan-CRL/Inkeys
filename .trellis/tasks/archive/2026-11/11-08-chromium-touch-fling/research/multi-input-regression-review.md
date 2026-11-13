> 本轮最终自动验证与未验边界见末节：构建及专项通过，完整集同二进制复核通过；首轮未改动调度时序失败保留。GUI/Windows实际输入递送与设备恢复仍未自动验证。

# 多设备漫游现场回归：独立审查

## 当前范围与证据等级

用户真机报告Mouse/Touch/Pen/PenTail均不能拖动、PTP偶尔可用。当前按multi-input-regression-scope.md直接修复既有承诺，旧multi-input-review.md的构建/无窗口PASS仅为历史，不作为本轮修复通过证据。本代理仅写本审查文档，不并发改修复代理源码；不编译、不GUI、不commit/push/archive。

独立读取附件并计数：893条pan-step全部held=0/samples=0；872条touchpad，pan-register/input ordinal/rts-batch均0。日志从seq328中途开始；这些计数证明截段运动来自native数值而非已准入接触，但不能证明每类设备尝试均在截段内，也不能从rts-batch的非Pen过滤条件反推Pen回调绝对不存在。

## Findings（修复前确定合同缺口）

### F1：关联成功被冒充系统确认Running

WindowControl.cpp的TouchpadNavigationOwner::Contact在SetContact S_OK后直接Publish(Running)。[Microsoft SetContact](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setcontact)只定义接触与viewport关联，随后系统继续识别；[输入流程](https://learn.microsoft.com/en-us/windows/win32/directmanipulation/directmanipulation-processing-input-with-directmanipulation)在识别manipulation后才进入RUNNING/捕获。

若接触未越识别门限或仅是tap，系统可能始终READY而无RUNNING→READY转换，synthetic Running却会令bridge持有并让NativeRunning拒收其它来源。该状态假设是确定缺陷；是否解释现场全部尝试仍需生产边界与设备证据。

### F2：更新驱动与输入所有权被混淆

初始化仅MANUALUPDATE|DISABLEPIXELSNAPPING。[Viewport options](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_viewport_options)明确MANUALUPDATE要求应用驱动更新；它不是输入manual。该文档同时说明EXPLICITHITTEST缺失时，RUNNING/INERTIA中的新WM_POINTERDOWN可由DM自动命中，应用看不到输入；[SetInputMode](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setinputmode)说明默认AUTOMATIC消费驱动manipulation的输入。

INPUT控制关联contact输入对UI的可见性，EXPLICITHITTEST控制运行中命中；[输入模式枚举](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_input_mode)与[ProcessInput](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationmanager-processinput)还需区分input/update及鼠标键盘转发。修复必须核对真实初始化参数与接线，不可把某一常量测试当作OS集成成功。

### F3：拒收的DM命中落入默认路径

WindowControl仅在外层条件满足且type==PT_TOUCHPAD时处理DM_POINTERHITTEST，其他类型、未知类型或SetContact失败落DefWindowProc。此分支没有建立“本适配器明确拒绝非PTP”的完整命中边界，且与F2的自动命中合同叠加。独立阅读[Chromium helper](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/direct_manipulation_helper_win.cc)与[当前窗口dispatcher](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/legacy_render_widget_host_win.cc)可见（OnPointerHitTest），helper仅SetContact(PAD)，但整个DM_POINTERHITTEST由窗口返回0；不能只复制helper的类型判断，忽略外围dispatch。

此处将默认处理列为隔离泄漏风险，不将“DefWindowProc必然独立SetContact”当作已由Microsoft明确证明的事实；确定的自动关联机制是运行中delegate hit-test。修复应在真实dispatch覆盖整个消息族与拒收路径。

## 生命周期审查要求

- 区分associated/pending/manual-pump责任、真实Interaction BEGIN/END、系统确认Running/Inertia/Ready；只有真实Status可发布互斥Running。
- 移除synthetic held后，manual pump仍需bootstrap/pending或真实Interaction驱动，否则会出现等Running才Update、等Update才Running的循环。第一次Update见READY不能立即当作有效待识别手势已结束。
- [Interaction枚举](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_interaction_type)定义BEGIN/END；[Chromium EventHandler](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/direct_manipulation_event_handler_win.cc)在真实BEGIN加入动画观察、END移除。待识别序列必须从真实生命周期结束，不以S_OK、静默超时或伪Up代替。
- 26100 SDK的Viewport2没有SetInteractionEventHandler；修复使用实际可用接口，不能创建不存在API。Handler可以同时实现真实viewport/interaction回调，注册和停止仍必须沿已证明owner责任。

## 新验证必须跨过的边界

实际生产setup/dispatch/association/status/interaction routine必须可被无HWND测试注入：核对传入真实viewport的完整options、失败传播、非PTP/unknown/模式/准入拒收不会落默认DM接管，S_OK持续READY、BEGIN未识别END、真实Running/Inertia/Ready、取消待识别和失败均保持正确责任。可用负对照验证测试能发现旧策略，但fake OS策略本身仍不是Windows/RTS共存实测。

原bridge/consumer回归保留，新增测试不能仅直接PublishHiddenTestTouchpadNavigation或检查bitmask即宣称本问题修复。必要诊断用现有CanvasPan gate，记录native命中类型/SetContact结果及系统状态、接触Down拒收原因，帮助区分OS上游与consumer仲裁；普通Move仍不高频控制wake。

## 当前验证状态

已完成修复前静态/primary API/附件核对；修复代理尚未冻结新代码，本轮没有编译或运行产品测试，也没有本轮PASS结论。真机Windows输入递送、PTP与RTS共存、系统自然滚动方向及光学体验均未自动验证。

## 修复冻结后的完整只读审查

本轮修复冻结六文件后复核，所有产品/测试源码保持只读，没有发现剩余确定源码缺陷。本轮尚待主会话实际构建/测试结果，不沿用旧PASS。

### Findings（由repair代理修复，已核对）

- 真实Owner::Initialize调用ConfigureTouchpadViewport，一次设置固定平移/原生惯性配置及完整MANUALUPDATE|INPUT|EXPLICITHITTEST|DISABLEPIXELSNAPPING；Contact仅Enable/关联，不再在可能处理中ActivateConfiguration。初始化任一步HRESULT失败沿原清理责任传播。
- 整类DM_POINTERHITTEST在Window实际branch处理，只有明确PAD、模式/准入有效才到真实Associate；非PAD、unknown、关闭门禁及失败均return 0，不落DefWindowProc。其它普通消息/原RTS与Win32Mouse producer路径不改。
- Wrl Handler实际同时实现ViewportEventHandler与InteractionEventHandler，并将该对象注册到真实AddEventHandler；Interaction BEGIN/END直接进入共享Frontend routine，不是独立测试模型。
- Frontend在SetContact调用前武装关联身份，S_OK只证明关联、不发布Running或永久订阅帧。同步BEGIN/Running/Ready/失败/epoch取消后S_OK尾部不复活旧候选；真实Status才开启Running区间。
- 审查发现并促成补修初始READY竞态：CanPublishStatus由Owner raw Status和测试共用；未确认motion的初始/重复READY不终止候选，真实Interaction END终结未识别手势；已确认Running/Inertia的Ready仍正常收末点、关活动。
- NeedsUpdate为已确认运动或真实Interaction责任，独立于RunningAt互斥持有；S_OK未识别不阻挡其它输入，BEGIN驱动manual pump，不从静默超时猜Up。END不截断仍Active的原生惯性。
- 审查发现并促成补修真实BEGIN抓停：Controller在有效未识别BEGIN责任下、实际推进own曲线前停止旧free，零额外几何；不制造Running/held，真实Pen等仍可接手。原epoch/admission与末点/关闭惯性尾差保持。

### 新测试的实际覆盖与限制

- 新production probe调用同一Configure/Dispatch/Frontend routine；Headless四类D/M/U确实经真实ContactInputCoordinator出队，并核对3条导航事件、倒置身份与Up，非PAD不能显式SetContact，PAD只关联一次。旧manualupdate-only负对照能显示缺少输入策略的模型失败。
- 模拟OS递送前半仍是按primary文档定义的options策略模型。它不是Windows input dispatcher或RTS服务实测，也未调用真实COM viewport。F3整类消息不走DefaultProc目前由实际Window分支的源码核对，helper bool测试主要检验是否关联，不额外伪称真实DefaultProc计数。
- 共享Frontend测试覆盖持续READY、未识别BEGIN/END、Running/Inertia/Ready净几何、END不截惯性、同步完整确认/结束、失败与epoch取消；Controller真实probe覆盖无BEGIN旧own coast继续、BEGIN零漂移抓停、候选不阻挡Pen及接手后退休。

### 静态验证

六文件严格UTF-8、CRLF及已有tracked文件BOM检查通过；git diff --check --ignore-submodules exit 0，仅原任务jsonl已有换行提示。本轮未改UI/默认值/摩擦/尺度/工具链，未执行GUI或编译。主会话正在构建与运行本轮CLI/Headless，实际退出码待补。

## 本轮已完成的实际验证（完整回归尚待退出）

独立读取multi-input-regression前缀结果JSON：本轮原生ARM64完整solution最终exit 0，112.672秒；第一尝试仅已知LNK1000 IncrBuildImage缓存问题，主会话保存attempt1日志、仅清已验证生成的Inkeys.ilk后重建，未修改项目/SDK。quick实际全部exit 0：control-fence 13.797秒、pan-feel 2.719秒、headless-pan 2.875秒、diagnostic-output 2.313秒。pan-feel为2112行、零失败。旧multi-input-*证据未覆盖。

canvas-pan、input-and-ink及cross-layer guide本轮新增合同已只读复核：更新/输入维度分开，完整flags与整类DM命中由应用决定；关联不是Running，BEGIN/END驱动待识别泵、确认状态驱动持有，初始READY许可、取消/失败及BEGIN抓停边界均匹配实际源码。前端测试只覆盖生产routine的参数/状态/真实Coordinator合同，不冒充OS输入递送；该测试边界已写规范。三处UTF-8/CRLF通过，无发现剩余确定spec冲突。

本轮完整Headless新进程仍运行，尚无最终exit；不以quick或历史PASS补写全量通过。源码保持冻结，待主会话提供最终snapshot/hash/full结果再收尾。

## 完整首轮失败与无改码复核边界

完整Headless首轮实际exit 1，550.938秒；唯一失败为未改动render_scheduler_tests.cpp约1036的“pending idle diagnostics flush at the limit without extra client callbacks”，首轮日志/JSON由主会话另存attempt1。该断言同时要求两条日志、间隔至少990ms、Bar/Settings精确调用数，真实线程等待3秒；失败日志没有打印各合取分量的实际值，不能确定具体失败项或武断认定唯一环境根因。

只读核对该测试只使用独立UI RenderPipeline Scheduler、诊断和Bar决策头，无本轮DM/Window/Controller依赖。Headless工程不编译WindowControl/Controller实现；新TouchpadInput只有模板定义，没有静态对象/COM初始化。animation_tests.cpp先在1859运行Scheduler，1881才创建本轮Frontend/Bridge的ContactInput测试；首轮日志第3行已报该失败，后续新增前端/接触测试未报错。未发现本轮六文件能在此前初始化/修改Scheduler状态的实际依赖链。

主会话按完全同一二进制复核一次完整集，未改源码/断言或随机修无关调度；新运行尚无退出结论。独立重算multi-input-regression-artifact-hashes.json的43份源码与两个EXE SHA256全部一致，确认复核不是换码后重试。此阶段不以首轮失败JSON作为新运行完成，也不把它删掉充作一次性全PASS。

## 本轮最终核对与审查结论

独立核对本轮build/quick/all JSON的精确case集合与退出码，首轮保留的attempt1 JSON/log，以及43份源码和两个EXE的SHA256。全部当前哈希仍匹配主会话冻结manifest；完整复核不是换码重试，审查代理没有修改生产或测试源码。

| 本轮验证 | 结果 |
| --- | --- |
| 原生ARM64完整solution | exit 0，112.672秒 |
| control-fence | exit 0，13.797秒，含新前端/BEGIN/候选生产probe |
| pan-feel | exit 0，2.719秒，2112行0失败 |
| headless-pan | exit 0，2.875秒 |
| diagnostic-output | exit 0，2.313秒 |
| 完整Headless首轮 | exit 1，550.938秒，唯一未改动Scheduler时序断言失败 |
| 完整Headless同产物复核 | exit 0，494.547秒，末尾PASS animation correctness |

首轮失败证据完整保留，不改断言、不随机改调度器，不将同产物复核通过表述为首轮从未失败。未发现与本轮六文件的实际初始化/状态依赖，且相关新输入测试在它之后；无法由失败日志区分该合取的具体分量，也不把系统负载/不稳定性宣称为唯一根因。

最终canvas-pan/input-and-ink/cross-layer三处合同与源码一致，静态diff及原编码/EOL检查通过。已修复已确认的生产前端契约缺口，自动构建/目标回归及完整集复核完成，没有剩余确定源码问题；**Windows/RTS真正输入递送、PTP与其它设备共存及用户真机“恢复可拖动”尚未自动验证**。策略模型与回调routine测试不能替代该现场结果，不能重复将无HWND PASS外推为设备恢复保证。

本轮未GUI/Computer Use、未commit/push/archive，任务保持in_progress。旧multi-input-review.md仅保留历史验证，本轮证据统一使用multi-input-regression前缀。
