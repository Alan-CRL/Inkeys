# Research: DM 与 RTS callback 状态及寿命交叉

- Query: 触控板 DM 初始化之后各类拖动和画笔接触失效，但 hover 可跟随；调查是否由本程序关闭 RTS callback/publication、RTS 状态写入或 COM apartment/reentry 导致。
- Scope: mixed（当前产品源码只读、Microsoft primary 文档；不重复主会话的系统二进制研究）
- Date: 2026-11-12

## Findings

### 1. 确定的两套 COM 与 HWND 寿命

- `Draw3.Host.cpp:2464–2471` 在 Host startup caller 创建 RTS，初始化成功后 EnableContactPublication，选定唯一 RTS producer。`Draw3.RealtimeStylus.cpp:2672–2688` 为 caller 建立 MTA COM 计数，随后 CoCreateInstance(RealTimeStylus)、put_HWND、AddStylusSyncPlugin、put_Enabled(TRUE)（:2690、:2706、:2844、:2861）。
- 同步插件构造时聚合 free-threaded marshaler（:1058–1059），QueryInterface(IMarshal) 转发（:1066–1067），不依赖 UI STA 的代理来执行全部普通 callback。初始化会拒绝 marshaler 失败（:2820–2830）。这符合 Microsoft AddStylusSyncPlugin 对同步插件 FTM/非STA对象的要求。
- Drawpad HWND 的 owner 属于 Window Service overlay 线程；`Window.cpp:1256–1259` 建立 STA COM 计数。
- 首次允许 native pan 时，`Draw3.WindowControl.cpp:2130–2136` 在窗口 owner 消息创建 TouchpadNavigationOwner；其 Initialize 在同一 UI 线程 CoInitializeEx(STA)、CoCreateInstance(DM manager)、CreateViewport 绑定同一个 Drawpad HWND、Activate/Enable（:381–400）。
- DM 自己的 CoUninitialize 只在自己的成功 CoInitialize 对应计数下执行（:383、:483）；没有调用 RTS 对象的 Shutdown、put_Enabled、RemoveStylusSyncPlugin 或修改其 gate。RTS COM count 与 UI STA DM count 位于不同职责/线程，不存在“DM正常清理自动把RTS MTA一起CoUninitialize”的应用调用链。

Microsoft 官方说明同步插件通常在 RTS 自己的高优先级线程调用，而不是 UI async 插件线程；不能仅以 UI 发生一次 COM pump 推断 RTS 普通 Packets 必须停止。

### 2. CallbackGate 不会因进入或退出 Pan 自行关闭

`Draw3.RtsStartup.h:142–219` 的 CallbackGate 只有单调 Close，无重开。全仓实际关闭入口是 `RealTimeStylusInput::Shutdown:Draw3.RealtimeStylus.cpp:2887` → StylusSyncPlugin::CloseCallbacks(:1100)。

Host 只有 `StopInputProducer:Draw3.Host.cpp:2568–2583` 调用 stylus.Shutdown；该流程同时永久封 touchpadNavigationAllowed、停止 DM、关闭 Mouse compatibility，属于 Host 停止/启动失败清理，不是 Selection 或 pan Enabled 切换。

Host startup 后的普通设置/Selection 同步在 :1740–1830；它只更新 drawing/window 状态。未找到因 DM Initialize 失败或退出移动模式而关闭 RTS CallbackGate 的直接应用路径。

`ContactPublicationGate:Draw3.RtsStartup.h:119–140` 更严格：接口只有 Enable，没有 Disable/Close。Host :2470 开启后同插件内不能再变 false。因此“进入 Pan 把 RTS Down 发布 gate 关掉”的具体假设不符合当前代码。

### 3. stateGate 的拒绝/等待路径与 DM 没有直接写入交叉

- `Draw3.RealtimeStylus.cpp:84–109` 的 RtsPacketStateGuard 对 writer-bit 或 CAS 竞争只做一次非等待尝试；失败后 Packets(:1418–1427) 返回 S_OK，并可输出 Cursor 诊断 state-busy。
- writer guard(:111–134) 先锁 writerMutex、设置 writer bit，等待已有 readers 退出，析构清零 state，之后 unique_lock 析构释放 mutex。
- 生命周期、Down、Up 用 writer；Packets、InAir 用 reader。DM owner 不持有/访问这些 gate 或 mutex。
- writer 中可能调用 RTS COM 查询以重建 decoder（Enabled/InRange/TabletAdded/Removed/UpdateMapping；BuildContextDecoder:916–974 等），也可能经 cursor sink 发无锁 mailbox/control wake；但未找到新增 DM 路径反向获取 RTS writerMutex 从而形成确定应用锁循环。
- 若某个 writer 在 COM/query/callback 中永久不返回，读 Packets 会持续 state-busy，Down/Up writer 会等待；此时 WM_POINTER UI 光标仍可能存活。没有故障时线程栈或 state-busy 日志，不能断言这个候选已经发生。原生 COM/系统内部锁关系也无法由应用源码穷举。

COM 查询本身不是已知违禁操作：Microsoft GetPacketDescriptionData 官方示例就在 StylusUp callback 调 GetAllTabletContextIds 和 GetPacketDescriptionData，不能笼统将“callback里有COM”定为本次根因。

### 4. RealTimeStylusDisabled 是一个可具体验证的状态分叉，当前没有自恢复

若实际收到 RealTimeStylusDisabled：

1. `Draw3.RealtimeStylus.cpp:1139–1150` 获取 writer 后 ResetDecoderLifecycleState(true)，并清 Pen cursor。
2. ResetDecoderLifecycleState(:1716–1720) 先闭合 active bindings，再 decoderCache.Reset。
3. cache.Reset(:713–720) 清 decoder、shared scale 和 lifecycleEnabled=false。
4. 后续若还有 StylusDown，但没有新的 Enabled/Tablet重建，EnsureContextDecoder(:1839) 因 lifecycle disabled 返回 nullptr，StylusDown 输出 no-decoder(:1198–1206)并早退，不能创建 ContactInput Down。
5. impl.initialized 只在应用 Shutdown 清零(:2928)，Host 不定期读取 get_Enabled 或自动重建 producer；因此系统状态若变化，不会由运行中 health check 修复。

该条件链是确定源码行为，不是“DM必定会发Disabled”的证明。Microsoft 仅保证该通知对应 RTS disabled 或插件从集合移除；现有文件没有记录它在本次 DM 初始化后发生。需避免把该条件推断写成实测根因。

RealTimeStylusEnabled(:1121–1136) 会 Reset → BeginLifecycle → 重建；TabletAdded/Removed、UpdateMapping也有明确重建路径。Error(:1522–1539) 只清 active contacts，不禁 decoder lifecycle，也不关闭 callback gate。仅发生一次 Error 通常不足以解释所有未来 Down 永久失败。

### 5. hover/Down固定/Up恢复无需 RTS 正常即可形成

这是目前最能解释用户外观反馈的确定应用链：

- `Draw3.WindowControl.cpp:2398–2420` 的 WM_POINTER* 路径可独立从 GetPointerInfo 发布 Pen cursor mailbox，与 RTS Packets 不同通路。
- :2386–2390 在 WM_POINTERUP 清 Pen cursor；之后 Pointer hover 或 RTS InAir 任一路均可恢复悬停外观。
- `PublishPenCursorSample:1562–1621` 对 Down/Up 状态边沿请求绘制；:1615–1619 明确接触期间依赖活动绘制帧读取 mailbox，连续 inContact 纯位置不请求逐点重绘。悬停则在非CanvasPanMode逐样本请求绘制。
- 所以若 RTS Down 没进入 active stroke，无论是上游不再callback、CallbackGate关闭、decoder失败或producer拒绝，UI WM_POINTER Down仍可画出一次光标；持续contact mailbox更新没有活动帧消费，光标固定Down点；Up/重新hover又触发重绘。

这与用户现象完全相容，但只能定位“可视光标路径与真实接触 producer 分离”，不能证明它属于上述哪一种RTS失败分支。尤其不能继续使用“hover正常 ⇒ RTS callback正常”的推理。

### 6. 离开移动模式不会撤销 DM 对同一 HWND 的系统对象寿命

`SetTouchpadNavigationEnabled(false):Draw3.WindowControl.cpp:842–851` → bridge.Cancel → owner Update/Apply(:421–435) 会 Stop/Disable/Reset viewport。

该流程不会 RemoveEventHandler、Abandon viewport、Deactivate manager 或释放 COM。这些操作仅在 owner Shutdown(:466–483)，由 Host Stop(:2572)调用或初始化失败清理调用。

因此成功创建 DM 后切回画笔仍共存同一个 DM manager/viewport HWND 注册；如果故障根源在其注册/系统采集模式，退出Pan不会自然恢复。该寿命事实支持主会话继续调查系统边界；它不证明 Disable必然不能让RTS恢复。

首次初始化失败时会走 Shutdown(:403–405)，逐步 Remove/Abandon/Deactivate；失败阶段未单列，只输出聚合 HRESULT。不能凭单条 E_ACCESSDENIED 判断是 CreateViewport、SetOptions、AddEventHandler、Activate还是Enable失败。若清理也失败，NeedsCleanup保留holder，当前Owner不再重试初始化；只有成功清理后后续开启才创建新Owner。这个路径同样没有调用stylus.Shutdown。

### 7. 现有诊断如何区分，不改代码的边界

- 默认 CanvasPan 的 rts-batch 只在成功获得decoder并完成PublishMove后且非Pen才写（RealtimeStylus.cpp:1454–1457）。没有它不能证明 Packets callback没进入。
- 既有 Cursor 诊断可显示 Down arrival/no-decoder/published/publish-failed，以及每秒一次的 Packets state-busy/no-binding-decoder/decode-failed/published（:1188、:1600–1623）。若这些在DM后仍出现，能排除“完全无callback”。
- CallbackGate lease 在上述普通诊断之前；若门被关闭，会无diagnostic早退。应用实际Close唯一调用点可由shutdown/stop调用栈分辨。
- Enabled/Disabled/Error 目前只在 DRAW3_RTS_DIAGNOSTICS 编译宏内记录事件，普通Cursor/CanvasPan gate不足以直接证明这些生命周期通知是否发生。
- 不启动GUI、不改产品码时，最有价值的动态证据为现有运行的线程栈/原生日志/系统注册边界；本研究未自行附加或暂停产品。主会话负责相关系统二进制/动态调查，避免重复。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp`：同步插件、decoder、COM startup/shutdown、cursor与接触发布。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RtsStartup.h`：单调CallbackGate、单向ContactPublicationGate与fallback。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：唯一producer选择、运行设置泵、StopInputProducer。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`：DM owner、WM_POINTER光标兜底、native退出与真实退休差别。
- `Inkeys/Inkeys/Window/Window.cpp`：Drawpad overlay owner STA初始化。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:6550–6564`：Down dismiss回调只检查状态并PostMessage，无同步等待DM/RTS。

## External References

- [Microsoft: RTS threading](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis)：同步回调通常在RTS高优先级线程；UI async不同。
- [Microsoft: AddStylusSyncPlugin](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-addstylussyncplugin)：同步插件必须聚合FTM、不是STA对象。
- [Microsoft: RealTimeStylusEnabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-realtimestylusenabled)：对象启用或插件加入时通知。
- [Microsoft: RealTimeStylusDisabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-realtimestylusdisabled)：对象禁用或插件移除时通知。
- [Microsoft: GetPacketDescriptionData](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-getpacketdescriptiondata)：官方callback内COM查询示例。

## Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`
- `.trellis/spec/native-desktop/canvas-pan.md`
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`

## Caveats / Not Found

- 本轮只写本研究文件，未修改代码/测试/spec/既有报告，未进行Git操作、构建、GUI或产品进程附加。
- 未找到DM模式切换直接关闭RTS callback/publication的路径；不等同于Windows内部采集器一定未被DM影响。
- 当前还没有故障后RTS Disabled/Error/state-busy/no-decoder的正证据或RTS线程栈；这些分支不能因默认日志缺席被一并排除。
- 外部资料只支持线程/通知/FTM合同，没有宣称DM和RTS同HWND必然冲突。该系统边界由主会话独立核对。

## 8. 追加：公开 HWND 注册/消息递送合同的窄查证

### 明文存在的合同

- [IRealTimeStylus::put_HWND](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_hwnd) 用于选择采集窗口，child/cascaded RTS 不允许设置该属性；公开页没有规定窗口必须不支持WM_POINTER或DM。
- [IRealTimeStylus::put_Enabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_enabled) 明确：同一HWND上多个 enabled RTS 的输入矩形重叠会出现不确定行为；不启用的对象没有事件。该条针对多个RTS对象，不能直接改写成“RTS与DM同HWND明文禁止”。
- [IRealTimeStylus::SetAllTabletsMode](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-setalltabletsmode) 包括fUseMouseForInput；对象Enabled后再调用会失败或旧系统无效。当前产品先SetAllTabletsMode、后put_Enabled，符合顺序，没有运行中因Pan重新切该模式。
- [InkCollector class](https://learn.microsoft.com/en-us/windows/win32/tablet/inkcollector-class) 描述多个采集器同HWND/重叠区域的限制，以及唯一tablet例外；该对象与当前RTS不是同一个API，不能用它证明DM注册冲突。
- [RegisterPointerInputTarget](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerpointerinputtarget) 是全桌面特定类型输入重定向，要求UIAccess且calling thread拥有窗口。它与主会话正在核查的内部DelegateInput并非同一公开API，不能拿前者的E_ACCESSDENIED合同推定后者所有失败原因。

### DM 文档只保证返回 UI 消息，未保证复制给 RTS/WISP

[Processing input with DirectManipulation](https://learn.microsoft.com/en-us/windows/win32/directmanipulation/directmanipulation-processing-input-with-directmanipulation) 具体描述delegate先收到消息、询问UI/IHT是否SetContact；未SetContact时将消息交回UI，识别后可能只交Down/HitTest/CaptureChanged。

这份公开合同仅针对窗口消息的流向。它没有承诺即使DM已建立delegate注册，原RTS/WISP同步插件也继续收到独立callback。上一轮把“INPUT允许UI收到消息、拒绝非PTP SetContact”外推成“RTS仍收到Packets”没有此primary文档支持。

### DefWindowProc：有明确promotion规则，未查到RTS专用转发保证

[WM_POINTERUPDATE](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerupdate) 明确未处理应交DefWindowProc；它可将pointer序列识别为WM_GESTURE或转换成mouse。部分消费、其余交默认处理可能产生不确定行为。该页没有说明DefWindowProc会把pointer重新送入RTS。

当前产品普通Pointer Down/Update/Up/Enter分支在 `Draw3.WindowControl.cpp:2517` break，最后走默认DefWindowProc；并不是这些主生命周期消息全部return 0。因此“新增实现吞了Pointer Move的default处理”不是已找到的确定应用缺陷。DM_POINTERHITTEST整类return 0是另一个消息族。

未查到Microsoft primary文档明文规定“RTS窗口不得注册WM_POINTER/DirectManipulation”，也未查到必须把某条WISP私有SendMessage回送DefWindowProc才能继续RTS采集的具体合同。不得把公开文档未写视为兼容保证；系统内部DelegateInput/UndelegateInput与WISP采集器关系需要主会话本机二进制证据或真实边界观测。

## 9. 追加：独立 coexist-probe 只读审查

已阅读 `Build/Investigations/dm-rts-20261112/coexist-probe.cpp` 和八份 `*.stdout.log`，未重新运行、未修改探针或产品。主会话报告系统DLL版本为10.0.26100.9818；该版本信息由主会话记录，本研究没有重复二进制检查。

### 探针有效性

没有发现足以让该矩阵整体成为明显假阳性的实现错误。RTS plugin使用WRL FtmBase；回调只原子计数，不依赖产品gate/decoder/渲染。每个case创建私有隐藏HWND，COM初始化成功，RTS/DM各方法报告直接HRESULT；日志中失败前的Create/config/options/content/handler均成功。

- baseline：RTS启用、禁用及Enabled/Disabled回调成功。
- same-create-only：创建/配置同HWND的DM但不Activate，RTS仍可正常禁用；可将差异收敛到Activate阶段而非COM创建/flags本身。
- same：先RTS启用，DM.Activate返回80070005；最终RTS.Disable返回80070057，get_Enabled仍true、5 contexts、未收到Disabled。
- dm-first：DM启用成功，反序同HWND RTS.Enable返回80070005；故不能把正序失败仅当作RTS startup本身原有问题。
- separate：同进程、同线程换成两个HWND，两套启用和RTS禁用成功；支持窗口级关联关系而非全进程绝对不兼容。
- dm-first-retired：同HWND先完整退休DM，再RTS启用/禁用成功；说明并非用过DM就永远损坏该HWND。
- same-retry：首次DM.Activate失败，第二次立即成功，但RTS get_Enabled仍true且最终Disable异常。启用状态读回不能独立作为采集器实际健康证明。
- **same-disable-early**：首次RTS.Disable在任何探针DM清理之前就80070057，证明异常不只由探针显式Remove/Abandon/Deactivate引入；但其DM清理后第二次RTS.Disable成功并收到Disabled，不能宣称故障必定不可恢复或只能重启。

### 与产品差异和剩余限制

1. 探针把HWND、RTS初始化和DM调用放在同一MTA线程；产品RTS在startup caller MTA、DM在HWND owner STA。不同case同MTA内部对照仍有效，但没有完全复现产品跨线程/apartment布局，不应称逐行产品完整复现。
2. 产品有SetDesiredPacketDescription；探针遗漏。baseline和create-only正常、失败点在独立DM.Activate，降低遗漏造成通用startup失败的可能，但仍未验证指定packet布局的完整产品形态。
3. separate时只给RTS window设置MICROSOFT_TABLETPENSERVICE_PROPERTY（探针:102），第二DM HWND只共享同一WM_TABLET_QUERYSYSTEMGESTURESTATUS回复。这是对照的属性不对称，不能把separate描述成除HWND身份外所有窗口属性完全相同；同HWND正反序和create-only结果仍独立成立。
4. 探针无输入注入/真实contact，所有contact计数均0。它能直接证明生命周期/注册级API冲突与状态不一致，不能直接证明某次Packets断流、拖动恢复或物理设备体验。
5. snapshot会Pump并调用get_Enabled/GetAllTabletContextIds；这些操作各case共用，没有发现明显使失败只在同HWND Activate发生的假象；但不是产品精确时序。
6. case返回值只覆盖start结果，不把所有cleanup HRESULT纳入outcome，所以不能仅以进程退出码代表cleanup通过；必须引用每阶段实际日志。
7. 因失败后Activated flag=false，探针不会显式调用manager.Deactivate；产品同样只有成功Activate才标retirement.managerActivated。主会话已静态确认系统Activate失败内部会Deactivate→UndelegateInput；该事实来自独立二进制研究，本审查不再推断不同cleanup机制。

### 审慎结论

现有新证据已经超出“文档候选”：在本机系统/该探针配置下，RTS与DM在同一HWND争用Activate/Enable注册边界可确定复现，且失败可让RTS读回Enabled却不能正常Disable。它能够解释为何上轮只修viewport options/SetContact/下游状态未触及真正注册边界。

尚不能声称“全部Windows版本一律禁止RTS/DM共存”、“Disabled回调已在用户现场发生”、“该探针实测了RTS Packets停止”，或“换HWND已构成产品修复方案并通过设备验收”。根因机制应结合主会话的DelegateInput/UndelegateInput静态调用链和用户时间线表述，物理输入断流仍属于与机制一致的现场推断。

### 追加控制修正：第二次Disable成功不依赖DM清理

已只读核对 `same-disable-twice.stdout.log`：在任何探针显式DM cleanup之前，连续两次RTS.Disable分别为80070057和S_OK，第二次收到Disabled回调。因此§9的same-disable-early只能说明首次异常早于cleanup；其后恢复不能归因于DM cleanup。更准确结论是第一次Disable会失败，但紧接第二次可完成内部退休，不能把它描述为永久坏死状态。

主会话补充系统静态解析：Activate失败路径的vtable+20表项对应CManagerImpl::Deactivate，进而_ClearWindow→UndelegateInput(HWND,2)。这是本机二进制静态调用链；“该回滚可能撤销先前RTS建立的委托”应标为与探针矩阵/现场时序一致的强推断，尚未动态hook观察原RTS的具体委托被撤销。
