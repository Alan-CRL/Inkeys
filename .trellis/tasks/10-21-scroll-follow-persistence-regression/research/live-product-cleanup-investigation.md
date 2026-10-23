# Product live 失败退出 AV：独立只读清理调查（2026-10-22）

本调查只读日志和源码，不改source、不build、不运行CLI/GUI、不SendInput/截图，不派代理。core负责WER/dump和串行修复，另一研究者负责Initializing未继续推进；本报告只核清理可达性、资源寿命和证据边界。

## 实际观测与范围

- 原始外层 `Build/ScrollFollow/live-product-click-unlocked-test.json`：PID72368，exit **-1073741819 / 0xC0000005**，elapsed **12678ms**，stdout7601B、stderr1487B。
- 正确私有目录是首行所写 **`Build/ScrollFollow/product-live-72368-213134453/result.log`**；先前派发中的212919656不对应此次日志。
- source99/99和逐像素ROI、OS自建source激活、own LeftUp接受均成功。StartProduct图形/首帧成功，DComp、主窗pen-ready，actual_ack1、visible=(1,0)。
- 主线程64072、Window owner24252、绘制线程61864。输入日志显示RTS的COM初始化 `0x80010106`，之后采用Win32Mouse/input_mode3。
- Toggle后最后业务事件只有initializing/session1。约8秒等待失败时快照仍Initializing、Host Follow phase2/Active，但 **Capture session0、attempts/accepted0、publishing/occupied/inFlight0**，projection0。没有进入正式采样；不能把这次失败称作PW黑帧或Mag未更新。
- 最后两行应用事实为timeout FAIL及business-tracking快照；没有分段cleanup日志，也没有最终cleanup行。因此知道进程随后异常终止，**无法仅凭日志确定它死在RequestStop、Shutdown、state join、StopProduct、保存还是窗口销毁哪一步**。
- catch里的error1400来自环境GetLastError，未绑定具体失败API，不能以此定位AV。
- core转述WER只有ntdll/c0000005/+0x2CB00，只有Report.wer、没有dump；cdb/windbg未安装。checker没有映射这个偏移到函数或获得异常线程/指令栈，不能把静态路径写成此次AV唯一根因。

## 最强确定代码危险路径：清理也会刷新未初始化Bar文字

此处不是发现Fixture f提前释放，而是普通产品刷新入口没有使用已有readiness合同。独立核对与另一研究者一致，并补齐了**清理再入**：

1. Business `Service::Change` 先写visibleState/session、打印事件，再调用RefreshUi。
2. 原RefreshUi普通绑定裸调用 `::barUISet.UpdateRendering()`，默认updateState=true。测试TestPorts会绕过，所以原纯Control矩阵不能覆盖真实绑定问题。
3. Bar.Main.cpp285后的UpdateRendering虽然按钮StateUpdate缺preset会早退，随后在真实工具IdtPen时仍无条件调用ThicknessDisplayUpdate。
4. Bar.State.cpp68–73：`barUISet.wordMap[DrawAttributeBar_ThicknessDisplay]->content.SetTar(...)`，没有空检查。wordMap是shared_ptr map；本早退夹具没有InitializeUI，key不存在时取到空shared_ptr。
5. Bar.UI.cppm60的Content::SetTar首先对成员tarmt取unique_lock。无效Content对象可把无效成员地址带到同步原语内部；因此WER写ntdll并不能独自排除应用对象错误，但**不能由此宣称0x2CB00就是某个具体锁函数**。

除初始化外，下列退出路径也回到相同危险入口：

- RequestStop → Service::Tick stop分支 → Change(Stopping) → RefreshUi。
- RequestShutdown → shutdownRequested → Service::Tick shutdown分支 → Change(Off) → RefreshUi。

所以只保护Initializing或在夹具补一个假wordMap对象都不足。应统一让RefreshUi使用既有ready门，同时保护普通早启动和退出状态变更。代码可达的空对象访问已明确；**本次AV到底在哪个线程/哪一次调用发生，仍待更细日志/异常栈或修复后对照验证**。

Tick本身由Service自己的16ms worker驱动，不属于本调查提出的新驱动方案；详细初始化研究见 `live-product-initializing-investigation.md`。本报告不建议增加另一个Tick线程、加Mag等待或写假ACK。

## 其它清理与callback寿命审查

当前夹具异常分支在catch记录事实后，先归还自己成功注入的LeftDown；安全层时提前BeginShutdown原子关闭旧显示，正常失败先RequestStop并有限等待Off。随后RequestShutdown、offSignal、StateMonitoring join、StopProduct、原Mag coordinator stop及worker join、WindowService owner shutdown/join，最后才Destroy source、DIB reset、Display/COM、logger/globalPath/log。

- **Fixture f/startupContext**：f是函数栈对象，startupMilestone只在Host启动阶段使用，StopProduct和所有相关owners join前仍存活；没有找到提前析构或把局部临时对象地址传入异步callback的确定路径。
- **Style callback**：保存的是进程单例service地址，service直到Host Stop之后才停止/销毁窗口。正常失败security0没有先拆service再让Host回调的顺序倒置。
- **Runtime callback**：penWidthContextChanged的void*参数不解引用f，调用原ApplyPenWidthContext/SyncDraw3State。EndDrawingActivity会核可选callback，fixture没有登记普通Main的drawingActivityChanged/acceptedStrokeBegan，未发现这两项访问未建UI的路径。
- **Logger/log**：null logger和f.log到state/Host/Mag/Window joins之后才恢复/关闭；已有证据不足支持logger已释放的UAF结论。Business事件用原DiagnosticOutput，Main的Session仍包围整个专测调用。
- **Source/DIB**：只在主线程SourceProc使用，f到DestroyWindow后仍有效；跨线程只通过Log的mutex写入日志，失败/returned等标志为atomic。texture在owners结束后释放。未发现这条链的确定借用越界。
- **StateMonitoring**：退出条件offSignal，迭代重查；正常只运行Desktop分支，没有设置whiteboardDesired或PPT事实。不调用正常Main的UI3线程join、Setting::Shutdown、RenderPipeline::Shutdown；夹具也没有先初始化这些再拆掉。实际Bar刷新危险来自上面的共享入口，不能笼统归咎StateMonitoring全部需要正常Main。
- **Host::Stop/保存**：原路径先关contact准入、升级Follow Shutdown，队列最终PrepareExitAutoSave，再停止真实输入producer，等Controller提交最终保存屏障，AutoSave workers drain，最后绘制owner退出并销毁Controller/GPU和detach。private autoSaveRoot和f仍在。代码包含阻塞等待，若卡住应由专用90swatchdog报告失败，不能仅凭这些等待解释本次c0000005。
- **Win32Mouse停止**：原WindowController把Stop消息Post到仍运行的owner并确认，Product WndProc在ProductStopping期间仍允许该特定stop消息。不因fixture用原Win32Mouse就直接缺失owner生命期。
- **RTS早失败清理**：RPC_E_CHANGED_MODE时coordinator/sink清空，comInitialized没有设true；Shutdown对impl/plugin/stylus等有判空并只在comInitialized时CoUninitialize。本次没有找到“COM初始化失败却重复CoUninitialize”的确定路径。
- **Mag**：正式Capture尚未占用；原Mag worker在WindowService Mag销毁/Uninitialize之前join。没有证据支持此处PW借用DIB已提前释放。
- **Business worker**：公开RequestShutdown让原worker单调退出；夹具没有新增join API，原静态Service/jthread仍由原析构负责。不把Snapshot Off叫作线程join证明，但它也不借用f.source/texture/startupContext，未发现该顺序直接造成f的UAF。

上述是已检查范围内没有确定UAF，**不是所有并发路径已证明安全**。本次没有dump，不能排除另一路错误或系统调用所收到的无效应用指针。

## 已授权两项最小修复的快速只读复核

主会话明确授权core修复并串行构建 `product-live-ui-ready-mta`，checker没有改source/build。已核实际新diff和hash：

1. **Business.cpp SHA256 `ffe8124961a426d5d505d8c09219a7a2e61dbb5fcd8ea3e14e964299d87a77dc`**：相对本轮前代码只把RefreshUi裸UpdateRendering改为 `Inkeys::UI::Bar::RefreshToolState()`，加中文寿命注释。Bar模块export :Main，Main.cppm1230已导出该函数。ready原子初false，RefreshToolState调用UpdateRendering(false)仍通知/唤醒；真实UI创建完毕后设true，继续原完整按钮/粗细更新。失败/退出重设false，不fake ready、不手建wordMap、不改ACK。
2. **inl SHA256 `25725365c4da9e325ca11b906458d31dbb884b9d9b7f0e100f4d191ffa8bce93`**：只把fixture主线程STA改为与正常Main2742一致的MTA。将该字符串逆换后文件hash精确恢复35c690...，没有其它输入/安全/清理差异。原WindowService STA线程/Mag设计未改。

本次Win32Mouse fallback是夹具先STA与RTS要求MTA冲突导致，不能写成硬件不支持。MTA修正后RTS能力及真实鼠标画笔仍须重新实测。

scoped `git diff --check` exit0。两项都具备明确原代码生命周期依据，**完整ARM64构建和必要纯CLI通过后，可按已有严格门进行有限GUI重试**；不因没有精确dump而禁止这种已证明必要的最小修复，也不把修复合理性等同AV根因已证或问题已消失。

## 若仍失败：最小诊断点

无需通过扩架构或再建假对象定位。若新的实际运行仍异常，建议只加本夹具自身同步log的成对before/after检查点，包含当前TID，不使用尚未就绪的Bar/logger资源：

1. 归还自身鼠标、RequestStop开始/返回和Off等待结果。
2. RequestShutdown开始/返回，offSignal已发布。
3. StateMonitoring join开始/返回（以及wrapper实际返回TID）。
4. StopProduct开始/返回；必要时细分CloseInput/producer stop/final barrier/保存drain/绘制join。
5. Mag stop/join，WindowService stop/join，source/Display/COM/logger关闭。

这些阶段点用于确定**哪个函数尚未返回**，不能单凭最后一个点认定其内部某条指令。仍需可取得的异常栈/线程dump才能给精确AV函数；WER module+offset和ambient LastError不足。core负责可用工具取证，checker未安装调试器、修改WER系统配置或再次运行程序。

## 验证与证据保存

- Lint/static：本调查只读调用关系和两项新diff，scoped diff检查pass。
- TypeCheck/build：checker未运行；core正在上述串行完整ARM64构建，结果另行记录，不从源码hash推断build成功。
- Tests/GUI：checker未执行；仅读取PID72368失败原日志，不将其它通过用例套用。

| 原始记录 | SHA256 |
|---|---|
| live-product-click-unlocked-test.log | 00cc58b82563b6bf3ae27b3f0c7b23cf3ad0caa48cfd8f1fbbbf6e28f3cdde8b |
| live-product-click-unlocked-test-stderr.log | b7355254c69dc1e4adb0dd5ad5eca7023226af0115cc0ed56c31694c99c775dd |
| live-product-click-unlocked-test.json | 9f4881ca07afa2b5c6817532f0e534e818db5126f54c0db4456b85e6b017d2b9 |

没有保存/读取未知个人窗口内容。没有为此次AV分配未经保存的失败EXE hash：当前磁盘EXE可能已被新build替换，应以core该次launcher/产物记录关联，不能把修复后hash冒称崩溃binary。

结论保持：存在清理也可达的确定早期Bar无效引用，已最小修复并静态复核；本次精确AV栈和实际修复后结果仍未证。父任务和完整功能继续in_progress。
