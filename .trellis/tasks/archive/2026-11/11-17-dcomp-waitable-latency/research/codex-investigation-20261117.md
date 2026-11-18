## 事实（file:line）

已核对7047db94并先读指定文档；全程只读，未运行测试。
下文除另注明，文件均为 `Inkeys/Inkeys/Drawing/Draw3/Draw3.<名称>.cpp`；C=DrawingController，P=TransparentPresentation，R=Renderer；`.cppm/.h`另标。

1. **Present路径**：生产Present1仅P:1161，经C:15345→P:1404。三个PresentFrame调用：C:15432清屏、15466全画布、23338主循环。Host:2338在绘制线程、Run前清屏；Run在2391，线程创建2429。PresentFullCanvas仅由Resize可选分支调用（C:15502），循环传false（21540），目前无实际调用。恢复/工作区/交接归主循环呈现（C:21420、21501、23405；P:1427）。均无句柄等待；公开清屏/全画布API可绕过循环。

2. **阶段顺序**：C:21347入口→输出切换21362→计时21381→设备恢复21408→合成状态刷新21529→Resize21540→导航优先Drain21624→tile/动画→脏区21930→命令/调试21987、22020→idle判定22054→输入批次/快照22152、22159、22185→几何→合成23255→Present23338→统计23424→帧末等待23508/23534。截止等待在上一轮尾部，首轮无；Down/Up代次使其提前返回（ContactInput:1329、1344）。idle的WaitDequeue会直接消费并处理输入，再continue（C:22125）。

3. **不走Present1**：空脏区、光栅/合成/激光失败（C:23227、23281、23298）；维护或idle/Hold的continue（22030、22050、22101、22129）；恢复/刷新失败早退（21427、21448、21536、21597）；退出（21893）。SelectionUlw仍渲染，但只做辅助ULW（P:1409）；主ULW亦不Present1（1416）。裁剪后空矩形直接S_OK，未调用Present1（1157）。隐藏/最小化/输入暂停没有独立的循环禁呈现分支；WindowControl:2803仅忽略最小化Resize。

4. **排除信号**：可见性诊断（Host:150）、show/hide更新（WindowControl:2771、2786）；输出target/revision与交接屏障（P:1273、817）；RecoveryPending/设备移除（P:1528、903）；pendingWorkspaceReady、视口恢复（C:15704、23395）；Host工作区交接/加载/挂起门禁（Host:837）。无统一“合成健康”证明：DComp的RefreshAfterCompositionChanged直接成功（P:1514），DwmIsCompositionEnabled现用于诊断（Diagnostics:936）。

5. **生命周期**：P:972创建waitable，997设latency=1，1029失败退普通FLIP；该尝试目前也覆盖ULW。P:1073统一初始化；921释放呈现器/Renderer/链；1172关闭；1305恢复设备、重试当前模式或降级。DComp退休含Commit/WaitForCommitCompletion（P:666）。R:595的ResizeBuffers保留原BufferCount/Format/Flags；P:1383不重建DComp链。设备级latency=1在GraphicsInitialization:156。

6. **帧率**：target_fps取自启动配置（Host:2274），有效档30/60/120/240（StrokeGeometry:465）；非法profile回默认120（InkPrediction:203）。Host.h:283明确仅启动选择，无运行期帧率setter（DrawingController.cppm:203）。未见DwmGetCompositionTimingInfo或现成刷新率字段；`Inkeys/Inkeys/Display/Display.cpp:694`只取显示方向。

7. **指标/面板**：C:15350的lastPresentDurationMs_、15363的presentWallMs；RuntimeMetrics.cppm:85有wallMs/呈现尝试与成功，RuntimeMetrics:711记录活动帧，1019输出frameWall/presentWall分布，1106导出逐帧。CanvasDebug:634结算输入→Present返回，660仅计成功画布帧；C:23459排除纯调试刷新。已有呈现模式/链描述/waitable信息（P:874；Diagnostics:395），经Host:436进入设置“画布信息”（`Inkeys/Inkeys/UI/Setting/Setting.cpp:4937`）。RendererDebugOverlay:299负责纹理/标记绘制，无呈现调度。

8. **测试**：HiddenWindowTest:4708/4714测自动/DComp启动；2858辅助资源测试强制ULW（2902），2916等直调Present不走DComp。R:813生命周期自测含composition链和Resize（849、959），未Present；HiddenWindowTest:4348光栅失败探针亦使用无窗链。`InkeysHeadlessTests/draw3_bridge_tests.cpp:738`测交接屏障，`draw3_contact_tests.cpp:2126`测截止唤醒，`window_tests.cpp:2165`测最小化隐藏/恢复；后者不验证DXGI。

## 推断

- **首选候选C:21598**：恢复/Resize已完成，首次导航输入观察前；比22152覆盖完整。若等待须覆盖恢复中的GPU操作，则在21381附近预检，并在换链后复核代次。idle的22125须拆开“唤醒”和“消费”，才能严格满足先等后消费；下一轮补等已晚。
- 令牌归具体交换链；空帧、光栅失败、SelectionUlw、idle保持，只有实际成功Present1消费。ClearCanvas/PresentFullCanvas需同一准备入口，不能只改Run。
- 句柄宜在P:997成功后获取；空句柄回退普通链；释放/失败回滚在swapChain.Reset前关闭并清令牌，Resize保持同链所有权。熔断状态应跨ReleaseAttempt保留，仅新会话复位；最小入口是给TryInitialize/CreateSwapChain加禁waitable选项，复用恢复后的CPU重放，避免直接换链丢L2。
- 等待耗时/结果挂逐帧RuntimeMetrics，超时/熔断挂Session及呈现器；不要混进presentWallMs或Present失败数。FrameParams/PerformanceSnapshot可扩展等待显示，设备信息页显示熔断原因。

## 待实测

- 可见DComp：首次令牌、空帧持有、超时后Down/Up、辅助输出往返、Resize/设备恢复及句柄泄漏；隐藏窗口测试不能证明可见DWM节奏。
- 扩展交接/输入测试验证“超时不消费令牌、不发布ready”；扩展Renderer生命周期测Flags与句柄代次。
- 对比各帧率下等待耗时、超时、presentWall、输入延迟及p95/p99帧时间；“合成正常”的可靠判据仍待确认。

## 风险

- **令牌不能按外层Present布尔值消费**：P:1426的Present1成功后，1432的DwmFlush仍可能失败；反之P:1157又可未Present而返回成功。必须区分实际调用与原始结果。
- C:21930每轮重置脏区，没有跨帧累计；超时只跳Present会漏刷新，须保留脏区或整帧重绘请求。若“不提交GPU”要求零GPU命令，仅在23338加门禁不足，恢复、tile、几何已发命令。
- 交接屏障和workspace ready依赖成功呈现，永久排除其超时可能永不熔断。首帧超时还会被Host:2346判启动失败。等待计入workMs会影响固定预算及CanvasDebug:754的无限制FPS/负载，需明确统计口径。