# Research: 页码环形等待指示器的复用与调度边界

- Query: 如何将页码角标三点替换为延迟1000ms的小环，复用现有主题/动效/调度且不改变业务、输入与ACK？
- Scope: internal；只读源码/规范，规划阶段，仅写本研究文件，无产品/测试/spec修改，无构建/GUI/git操作，无派发。
- Date: 2026-11-01

## Findings

### 1. 现有事实与最小行为归属

`Bar.Scene.cpp:712–729`现有三点由Page id3的`options.content`绘制，触发为`navigationWaitingPrevious || navigationWaitingNext`，中心近似(right-15,top+7) DIP、radius1.6 DIP。`Bar.Scene.cpp:410–411`将等待变化计入Page损伤；方向输入仍可保留，仅替换视觉。`Bar.Scene.cppm:41–55`的BarPageSurfaceContent目前没有show session/等待episode字段，不能只凭两个bool辨认跨场旧等待。

`PageControl.cpp::BuildPageContent`约920–979提供PPT命令/页面准备/Recovering的视觉源，白板分支隔离。普通数字继续Numeric，稳定视觉override不改变currentPage/ACK/EndShow。该指示器应只消费已有等待事实，不把动画状态作为ready/input条件，也不改变故障或Retry动作。

### 2. 已有1000ms截止唤醒通路：优先复用InputWorker，不必改Scheduler

已确认RenderPipeline公开Scheduler接口只有Request/RequestControl/PostControl，没有RequestAt、通用定时客户端deadline或延迟任务（`RenderPipeline.cppm:295–329`）。`RenderPipeline.cpp:706–735`在pending为空时仅根据异常诊断期限调整等待，否则INFINITE；其nextDeadline是16.67ms帧节拍，不是业务deadline。不能直接声称已有Scheduler延迟唤醒API。

但仓库已经有更窄的通路：

- `Bar.InteractionWorker.h:205–214`定义SurfaceInputTick返回`steady_clock::time_point`，并提供`WakeInteractionWorker()`。
- `Bar.Interaction.cpp:1484–1513`每轮遍历所有active注册surface并调用tick，不要求当前有手势；`:1552–1554`取各tick与主续接最早deadline；`:1911–1912`用现有mailbox.WaitUntil等待，`:1868`的Wake递增/通知现有mailbox。
- `PageControl.cpp:3000–3021`已有PageControlInputTick，且`:5781`已注册到每个窗口生命周期。当前tick在长按不启用/没有press时早退max，因此新视觉deadline必须在这些早退之前独立处理，最终返回min(原长按deadline,视觉deadline)，不能被原早退吞掉。

建议实现轮廓（尚非已批准代码）：renderer/Scene只拥有环状态和绝对截止时间；PageControl在稳定per-surface原子数值槽发布/取消deadline并调用WakeInteractionWorker；InputWorker tick在既有inputLifecycle/generation验证后只读取该纯值，到期CAS消费当前deadline并RequestSurface(index)，不接触Scene/D2D或持render锁。render重新读取最新等待/会话状态后决定是否出现；过期wake只能多唤醒一次，不能携带“显示=true”命令。

deadline数组宜与可赋值替换的SurfaceState分离，避免atomic成员使现有`surface=SurfaceState{}`失效或生命周期内读到已替换对象。Acquire/Release/Unregister应清截止值，仍依原DrainInteractionSurfaceCallbacks的真实排空，不靠标志替代join。新等待替换deadline时，tick必须CAS对比旧值，不能把新deadline无条件清零。即使cancel与到期竞争，也只RequestSurface，最新状态复核阻止复活。

这条通路可使渲染器在1秒延迟期真正Idle，无须新线程、Scheduler API或Win32 timer；应以确定性worker测试确认到期前无额外render callback、到期一次、取消/新生命周期无迟到显示。

### 3. WM_TIMER和Continue备选的事实与取舍

PageControl已有`PointerTimer=0x353`（`:131`），由窗口owner WndProc在原输入路径SetTimer/处理WM_TIMER/KillTimer（`:3150–3163,3193,3356`）。它只在接触活动期间按50ms维护桌面/输入drain，不是无手势加载timer。Window::Service公开接口（`Window.cppm:308–418`）也没有通用owner定时任务API。不能直接在renderer乱用SetTimer/复用PointerTimer编号；若另做Win32方案，至少要私有消息投递owner、owner设置一次timer并在取消/销毁清退、HWND generation验证，范围和风险均大于现成InputWorker deadline，因此不推荐。

若简单让delay也返回FrameResult::Continue，确可约60Hz Tick而不Draw（`RenderPipeline.cpp:54,748`为16,666,667ns节拍）；四个可见/启用surface一秒内可有约240次回调。它不等于真正Idle，且会影响共享批次/诊断活动与idle时钟。一般不是逻辑自旋，但为1秒隐藏期制造持续回调没有必要；现有deadline通路更符合最小调度复用。

### 4. 单调时间、动画时钟与生命周期

FrameContext.frameTime由同一串行scheduler批次提供（`RenderPipeline.cppm:61–66`）；Scene.Advance会同frameTime去重（`Bar.Scene.cpp:605–620`）。FrameAnimationClock.Tick在idle恢复首帧返回0，普通dt截到50ms（`Bar.FramePacing.cppm:89–141`）。因此1000ms门槛必须用steady_clock绝对`now >= waitingStart+1000ms`，不能累加动画dt或按帧计数，也不能乘全局动画speed。入场到期首帧dt=0时必须仍保持motion active，确保后续帧推进，而非0透明就误Idle。

建议状态：Idle → Delaying(deadline) → Entering → Spinning → Exiting → Idle。它是纯视觉状态，不改变已有loading源。重复true、方向变化、同数字/样式publication不重启deadline；只有连续等待false后新true才新计1秒。换show session/隐藏/进入白板/Release退休旧episode与deadline，不能沿旧可见状态复活。当前BarPageSurfaceContent缺少session/eligibility，需由PageControl给Scene最小纯值会话/可见资格或等价明确setter，不能仅靠等待bool。

设备资源释放通常不等于新等待：`Bar.Scene.cpp:836–840` ReleaseDeviceResources保留Impl/动画时钟，仅释放renderer与图标；`:830–834` Reset则重建整个Impl。可见期间device重建应保留绝对等待起点、运动相位，重建缓存资源；真实隐藏/换场才重计。

若出场未完又来新等待，严格“新等待重计1000ms”与“立刻反向入场”是不同产品行为。按主会话拟定，false退休旧episode、新true重计；旧出场可以从当前姿态平滑到0，但新等待不得绕过1秒门槛直接入场，具体交接应在最终设计中写明。无需额外最短显示时间或自造去抖TTL。

### 5. 主题色、几何和描边资源

已确认指定主题角色：`Bar.Theme.cppm:22–30`的SurfaceFrame与TextPrimary。`Bar.Scene.cpp:196–197` divider/dragHandle使用SurfaceFrame，divider opacity0.30；`:626–633`主题更新也用同border token。TextPrimary在深色主题为白色、浅色主题为RGB(27,27,27)，SurfaceFrame浅色基色为黑、深色灰（`Bar.Theme.cppm:60–88`）；应跟随token与现有alpha，而不是硬编码白/灰，否则浅色主题失配。

现有`BarUIRendering::DrawProgressRing`（`Bar.Rendering.cpp:1432–1478`，声明cppm:343）可参考track/arc绘法，但它每帧CreatePathGeometry/Open sink、单一strokeWidth，DrawGeometry不传round stroke，不能直接满足“粗圆头前景+细轨道+无热帧分配”。

`GetRoundStrokeStyle`（`Bar.Rendering.cpp:1561–1584`）已缓存ROUND start/end/dash cap及ROUND join；`DiscardDeviceResources`在`:382,395–396`释放并清失败latch。可复用其资源生命周期，新增或扩展窄环绘制helper：完整轨道用DrawEllipse；固定角度单位圆弧几何只创建一次，逐帧仅旋转/scale变换，保存并恢复原D2D transform，foreground使用缓存round stroke。不要在每帧重建path/brush，不引入WinUI或新渲染系统。若资源失败，不得仅因装饰环不可画就打开/关闭原ready门或改变错误状态。

尺寸可行建议而非既定产品决定：旧角落中心(right-15,top+7)，PageTwoTwo主槽上沿top+11；8DIP外径、轨道1、前景1.8–2、约250°固定弧、一圈约1秒符合主会话拟定的小环方向。注意外径8在中心y7时下沿恰到11，需考虑AA和缩放包络；可微调外径/角落内center以留余量，不能把EaseOutBack超调后的最大半径漏掉。选择.75→1的单调非线性而不超调可避免越过数字槽。必须用9999/999、三倍率实际BGRA/几何包络验证，不以肉眼想象声称不遮字。

### 6. 显隐曲线、全局动画与渲染需求

`Bar.Animation.cppm:29–46,95–122`提供EaseIn/OutSine、Cubic、Back等曲线。现按钮文字内容用退出EaseInCubic缩放/EaseInSine透明度、进入EaseOutBack/EaseOutSine（`Bar.UI.cpp:901–911`）；`Bar.EraserAttributeMotion.cppm:51–90`已有可打断双时间线geometry/opacity和同帧dt=0保护，可参考其从当前值Retarget/LIFO样式，不要重建按钮内容timeline以实现角标。

主会话拟定180ms/140ms显隐、scale .75→1且不超调；可用公共EaseOutCubic/OutSine与相应In曲线，避免小角标轻微回弹出界。1000ms等待门槛独立于动画开关和speed；显隐运动可按现BarUiAnimationEnabled/SpeedRate合同推进。全局禁动画时现通用advance会直接提交target；一致建议是显隐即时、前景静态弧不旋转，仍保留1000ms门槛，不新增设置。循环spinner没有现成仓库语义可替用户决定，需在最终计划审批中明确这一行为，而非把建议伪称既有需求。

Scene.AnimationActive当前包括所有视觉motion和renderer字形动画（`:958`）；PageControl真正inputLocked在`:1260–1261`用bounds.active/Geometry.layoutAnimating及workspace switching，**不是**所有AnimationActive，所以spinner作为visual active不应直接锁输入，必须保持这一分离。

但`:2492–2504`把scene.AnimationActive并入transitionDeadlineActive，再用于窗口退场可见性；若隐藏后仍将“原等待=true”的旋转无限保留，window可能无法退场。必须在隐藏/白板/会话退休时停旋转/清deadline，不依旋转活跃留窗口。进场/旋转/退场期间仅损伤角标Page区域，不重开布局、不要导致数字/字体动画；最后清除帧成功后仍应收敛到Idle。原no-damage复用和nativeFrames.keepAnimating（`:2430–2445`）也要考虑实际角标运动，不能重复复用静态旧像素把spinner冻结。

## Files Found

- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp(m)` — 三点绘制、Scene时钟/损伤/活跃与资源边界。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp(m)` — 等待源投影、visible/session/控件生命周期、native帧与既有输入tick注册。
- `Inkeys/Inkeys/UI/Bar/Bar.InteractionWorker.h`、`Bar.Interaction.cpp` — 可复用的steady-clock deadline/现有worker唤醒，无须新线程。
- `Inkeys/Inkeys/UI/Bar/Bar.FramePacing.cppm` — idle首dt0与50ms clamp。
- `Inkeys/Inkeys/UI/Bar/Bar.Animation.cppm`、`Bar.UI.cpp`、`Bar.EraserAttributeMotion.cppm` — 公共曲线、全局动画合同与可打断双通道运动。
- `Inkeys/Inkeys/UI/Bar/Bar.Theme.cppm` — SurfaceFrame/TextPrimary角色色。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp(m)` — 现有环绘法参考、缓存圆角stroke/设备资源释放。
- `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp(m)` — 回调批次/Idle/Continue节拍；缺少公开业务RequestAt。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md` — 主栏/分页共享组件、数字/状态策略、资源与真实呈现、输入与布局分离。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md` — fixed numeric hot path、真正Idle、无render I/O与真实join后封口。
- `.trellis/spec/ppt-interop/native-session-ui3.md` — Page角标、稳定显示数字与control/ACK分离、Pending/Recovering/错误保留。
- `.trellis/spec/guides/code-reuse-thinking-guide.md` — 仅收窄具体内容策略，不用surface级开关关掉其它动画。

## Validation Recommendations

1. 纯状态/假时钟：999ms无环、1000ms开始入场、短等待结束后无晚闪、重复true/换方向/同值目标不重计；false后新等待重计，入场取消和出场重入都连续。
2. 真实worker deadline：无手势也准时RequestSurface，到期前renderer无强制Continue/Draw；取消/替换/CAS竞争/Unregister+新generation不会复活旧请求，原长按deadline不被破坏。
3. 运动：同frameTime不二次推进；idle后dt0仍继续显隐；global speed不改变1000ms门槛；禁动画提交目标且静态，收敛后无持续callback。
4. 实际Scene/D2D：旧角落内track/粗圆角arc、三倍率/深浅主题/9999与999无遮挡，部分重画与全量一致、退场清除无残影，旋转帧确实不同；glyph/Arrow/Add动画和ready状态不受影响。
5. 资源：热帧path/brush/stroke复用，device epoch重建/Release/Reset无失效复用或保活；故障不把装饰失败升级为业务状态变化。
6. 控制隔离：spinner pending/spinning/exiting不改变current/target/pageAckEpoch、稳定数字、输入locked/ACK、Retry/EndShow；隐藏/白板不会被循环motion拖住。
7. 按仓库规则完整InkeysRepo.sln Debug|ARM64、已有定向/真实离屏与适用worker测试；用户负责最终桌面视觉，没有GUI验证的部分单列。

## External References

无外部网页或SDK/版本推断；全部为当前仓库实证。不引入WinUI依赖，不把“WinUI3风格”扩写成特定系统控件参数保证。

## Caveats / Not Found

本研究没有实现、构建或实机GUI结果。1000ms起点、短空隙/新等待与出场复入、全局禁动画时循环表现、最终尺寸/时长为需要计划明确的UX语义；上文已区分当前事实与主会话拟定方案。InputWorker复用是源码可行的最窄路径，但必须实际测试其deadline合并、生命周期及锁顺序；不能把此研究等同已验证实现。

## Final Planning Cross-check

已只读核对当前prd/design/implement，InputWorker deadline复用、Scene可见循环/隐藏退休、绝对1000ms与动画dt分离、缓存固定D2D弧均与现API相符，无技术阻断。8DIP、track1/arc2、250度/1秒、180/140ms及.75→1无超调为提交用户确认的建议，不是当前实现或已批准规范。建议明确资源降级：真实device lost沿既有路径；单个装饰环资源失败不升级CanvasRecovery、不改变ready或让数字永久无法提交，可仅跳过环并在资源恢复后重建。其余需按既定包络/生命周期测试验真，本次未实施或构建。
