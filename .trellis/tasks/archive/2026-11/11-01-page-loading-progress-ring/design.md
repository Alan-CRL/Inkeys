# 环形加载指示器设计（已批准）

## 决策与最小边界

本任务只替换PPT页码角标的显示，不修改等待事实源、COM、数字缓存、错误/Retry或任何ready/input语义。现有方向等待OR页面准备/Recovering统一为连续loading。重复true/方向更新不重新计时；false结束本段，下一段重新计500ms；换session/白板/隐藏/注销退休旧状态，不跨不明确的等待空档拼接。

### 外观

每个Page id3保留一个右上角角标，以现空白区域锚定。最大外径约12DIP，具体包络保持在主/次数字上方；按原UI scale适配四surface，最大9999/999不遮字。整圆track使用SurfaceFrame（边框/分割线角色色），线宽约0.6DIP；前景TextPrimary，线宽约1.2DIP、圆头，固定约250度弧顺时针转动，默认一圈约1000ms。它是不确定加载反馈，不显示百分比，不改页码/按钮布局。

### 时序与动效

建议状态为Hidden→Delaying→Entering→Spinning→Leaving→Hidden。500ms门槛使用FrameContext.frameTime对应steady_clock绝对时间，不累计被clamp或idle归零的动画dt、不受全局动画速度影响。499ms不显示；达到门槛且当前loading仍true才开始入场。门槛前结束取消deadline，不迟到闪现。

入场建议180ms opacity0→1、scale0.75→1，复用公共EaseOutCubic；退场建议140ms opacity当前值→0、scale当前值→0.75，复用EaseInCubic。最大scale为1无超调，避免挤入文字。所有重定向从当前值续接，不强制最短可见时长；短间隔新loading重新计0.5s，原退场继续。全局关闭动画时按现有规则即时显隐、固定静态弧，不旋转；0.5s门槛始终保持。

### 调度

延迟期不靠Continue持续空帧，也不新增线程/Scheduler API/Win32 timer。优先复用Bar.InteractionWorker的SurfaceInputTick deadline与WakeInteractionWorker：render owner为稳定per-surface原子槽发布/取消deadline，input tick在长按早退前合并该独立截止时间，到期只RequestSurface，render再检查最新loading/session。worker不访问Scene/D2D；注册/注销与原生命周期一致，CAS消费当前deadline，取消/替换后的过期wake最多触发一次无害状态复核，不显示旧指示器。

进入/旋转/退场只为当前可见PPT surface请求实际续帧和角标损伤。不能把循环旋转无条件塞入用于ShouldKeepWindowVisible的Scene.AnimationActive；隐藏目标直接退休并由父窗口原动画处理退场，防止旧窗口常驻。输入锁继续只由原layout/业务门决定。退场末帧清理角标像素后恢复Idle，无常驻回调。

### 绘制与资源

复用现D2D renderer/theme brush，缓存单位前景弧几何与round-cap stroke style，track DrawEllipse；每帧仅旋转变换和现有opacity/scale，不反复CreatePathGeometry/Open sink或分配，不引入WinUI运行时。确保transform/brush状态在绘制后恢复；真实设备丢失仍沿既有DeviceLost路径，epoch/target重建及ReleaseDeviceResources配平。单个装饰环几何/brush失败仅降级不画环并记录原有诊断，不升级CanvasRecovery、不阻塞数字/ACK；失败按epoch/新等待重置的有界策略重试，不逐帧重新分配。诊断不得把环伪记成三个dots，必要仅记录真实marker种类/事实，不扩通用遥测。

## 预期文件

- Bar.Scene.cpp(m)：角标视觉状态、Tick/帧需求、几何/样式资源及绘制。
- 必要窄Bar.PageLoadingIndicator内部helper：单调计时和可打断相位，生产/headless共用，不做第二套动画曲线。
- PageControl.cpp：同frameTime、可见/session生命周期、原子deadline→InputWorker合并、角标损伤/退场调度，既有实际像素fixture。
- 必要BarPageSurfaceContent字段携带视觉session用于退休，不改COM/业务协议。
- Bar.Main.cpp仅在全局动画enable/speed变更时复用PptPageMask Request唤醒静态环；原UpdateRendering仅唤醒Bar，这一接线避免静态Idle后无法恢复旋转。
- Bar.Interaction.cpp仅既有RunSharedInteractionWorkerTests及helper include，验证无Pointer Down的实际deadline唤醒，不改worker算法/API。
- 既有headless测试与必要spec同步。若无需改Ppt.cpp投影则保留；不改Bar.InteractionWorker公共接口或其其它消费者，除非研究证明原接缝不能满足。

## 风险与兼容

单调门槛与动画dt分离；新等待/同值heartbeat/方向更新不混淆；定时wake取消与注销不可访问已释放Scene；无限旋转不保留隐藏窗口。资源丢失不能重置业务等待或输入状态，也不能触发旧会话环。样式按主题角色色而非硬编码white/gray，浅色主题跟随文字色。短loading无任何spinner像素。实际桌面视觉与Office性能仍单列，不把本任务当性能修复。

## 2026-11-01 人工反馈调参（用户明确批准）

可见环半径约原1.5倍（最大外径8→约12DIP），轨道1→约0.6DIP、前景2→约1.2DIP；因此中心线半径建议5.4DIP，圆头/250度/旋转周期/显隐曲线仍保持。显示门槛1000→500ms，新等待重新计时与取消语义不变。优先保留原角标锚点，若实际像素证实遮字/裁切可在同一右上角空白区域最小调整，不能移动数字或改字号/布局。新大包络/AA损伤与最大位数必须同步实际验证。
