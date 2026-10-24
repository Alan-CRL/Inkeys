# Animation target precheck

- Query: 连续字体修正系数方案的调度、目标交接和作用域预审。
- Scope: internal；只读预审，产品实现仍在进行，本文件不是最终源码审查结论。
- Date: 2026-10-24

## 已确认设计

以稳定原始物理目标T计算Q=floor(T)、cTarget=Q/T，原raw字号/几何轨迹乘连续c；开启cTarget=1。原size.val/tar、几何、共享帧时钟保持原语义。MeasureText保留原上界；设置字体不改。不再使用逐帧floor或动画结束后补floor。

已读最新prd.md、design.md及font-animation-targets.md、font-animation-scales.md；旧逐帧floor code-spec由此次修订覆盖。以下检查点均有现存调用路径依据，已告知主会话和implement；不将尚未完成的接入误记为最终缺陷。

## 具体接入检查点

### 1. 先决定是否绘制、后执行Word：c尾段必须进入需求

依据：Bar.RenderLoop.cpp的RenderFrame依次执行AdvanceAnimationsAndDeriveLayout、PrepareLightingAndDemand、CalculateDirtyAndDrawPresent；后者先检查presentDecision.ShouldPresent，才到Word。PrepareLightingAndDemand的hasActiveRendering当前只包含原动画、damage和retry等。源动画结束而c仍在途时，单在Word里推进/汇总c无法保证下一帧仍进入绘制。

最小接入：在新BeginFontFrame清本帧font active之前保留上帧活跃需求，并在本帧PrepareLightingAndDemand之前将其纳入damage/visual demand；当前帧绘制新发现c active时也须让owner继续有限帧。或者在原Advance阶段推进/聚合，保证其目标元数据已就绪。不能只在绘制末尾记录一个随后帧首清掉的bool。

分页同样要接两处：Bar.Scene.cpp的Advance返回active；PageControl.cpp::PresentScene在调用scene.Render之前已用scene.AnimationActive设continueRendering并执行无damage早退。只在Render后的Word更新renderer active，仍可能提前idle。

### 2. 绘制cull可能早于Word，不能依靠未绘制文字推进

依据：Bar.Button.cpp::DrawBarButtonVisual和DrawBarFixedButtonVisual在Word前执行ComputeBarButtonVisualBounds/ShouldDrawFrameBounds；Word自身也有enable、opacity、原始字号、layout及dirty clip早退。

最小接入：上一帧仍有c active时应让相关文字范围或本客户端保守全脏，确保可见文字不会因原几何未变而被跳过。若在统一Advance中推进，则在c changed时加入原文字包络damage。隐藏/透明/原始大小为0的文字不应独自维持空转；Q=0但raw>0的可见文字在开关恢复时仍要能推进c由0返回1，不能因本帧结果为0而先退出、漏报active。

### 3. PageControl同now的Advance/SetBounds顺序

依据：PageControl.cpp::ConfigureSurface原路径是scene.Advance(now)后ApplyCurrentSceneBounds，RenderSurface/scene.Render还会以相同now再次Advance；Scene原实现相同frameTime立即返回。AnimatedBounds包含values[4]当前scale及独立targetScale，SetBounds得到的是当前插值，不等于终点。

最小接入：ApplySceneBounds在相同bounds短路之前交接targetScale，且文字frame目标冻结必须发生在该值更新之后或由SetFontTargetScale立即更新当前帧元数据；不能让第一次Advance冻结旧targetZoom后被第二次同now早退永远保留。验证一次无动画bounds跳变后不依赖额外外部唤醒，也能收敛正确字号。

font-animation-scales.md前部表格/源码地图正确指出targetScale；其7.1中“Scene.SetBounds的scale当前无独立动画”只适用于Scene自身持值，不能当成PageControl外层没有倍率插值的结论。预审期间implement已在ApplySceneBounds加入显式SetFontTargetScale，方向正确，最终仍需检查同帧时序。

### 4. 目标作用域必须与DC恢复点成对

依据：主栏SetBaseTransform/SetBodyTransform/SetGripTransform/SetRigidTransform切换不同Y倍率；通用ButtonDrawTransform另加press；手工ThicknessButtonRender及freeLineRow直接套按压矩阵；DrawThicknessPopup临时把字号恢复常量后套popupGeometryScale；Scene.SetPose把菜单姿态乘进frameZoom。

最小接入：在这些明确拥有者旁同步提交各自target值，退出恢复原文字ratio/targetZoom/duration，与矩阵恢复同一作用域。父scope乘子scope，不能由子覆盖父；menu/theme须Compose父子目标pose。Eraser/Recall预缩放字号的nominalTarget已含目标pose时不能再乘一次相同目标pose。纯X果冻不参与字号高度；细刻度不额外乘只影响位置/线长的panelAnimationScale。

### 5. 每word/独立样式有界状态且每帧只推进一次

依据：细刻度同一帧循环多个数字；Scene.Render会再次Advance；同一个Word也可能被不同pass重复调用。不同直接文字具有不同目标，dock与FPS不能共享一个correction状态。

最小接入：word自带c；dock用既有bottomDockIndicatorWord；FPS单独固定c；细刻度所有数字共用一个固定样式c即可。以owner共享frameTime/serial保护Advance，重复目标SetTar保持幂等；测量和格式预热不推进。相同serial仍可更改目标但dt不重复消费。不要为每个tick或word地址创建增长map。

### 6. 0目标、零dt和重定向

依据：BarUiAdvanceAnimation已在有效非零duration下对dt<=0返回active并保留当前值，无需另建零dt时钟或视作结束。BarUiValueClass.SetTar从当前val重定向，同target不重启。

最小接入：T=0显式定义有限c（例如保留当前c），不要计算0/0；Q=0、T>0可连续走向c=0。只有初次或明确forceReplace/禁用动画可直接落目标。政策切换、格式缓存清理、重建D2D资源不能Reset c或ForceNext，从而在首帧突然补取整。重定向前后dt=0应具有相同raw*c输出。

### 7. 呈现失败沿原动画语义，不扩展rollback

依据：主栏ULW/EndDraw失败时保留dirty重试，原raw动画值已推进；成功后才消费damage/发布命中。新c应遵循同样候选动画语义。

主会话已明确不引入新的呈现rollback架构：失败/资源重建不重置c，不把未成功提交的动画值称为“已显示值”。文案、注释和断言使用“当前动画值”；需要提及已上屏值时必须有真实成功帧证据。回归应验证失败重试和中途新目标不会触发额外Reset/ForceNext/结束floor，不将现有raw动画跨失败的时间推进冒充新机制缺陷。

## 有定位价值的回归

- 原size或press先结束、c仍active：主栏和分页仍继续帧，最后一次变化有真实重画，最终恢复idle。
- 只有字体政策变化、没有几何damage：可见文字持续到新c目标；不依靠其他颜色/布局动画带帧。
- 同frameTime多次Advance/Word、细刻度多个tick：c仅推进一次。
- 一次SetBounds当前/目标改变并同now Render：使用新targetScale，不等下一次无关事件。
- Q=0的可见文字从关闭切回开启：不会因0px早退永久消失；T=0退场安全、无NaN。
- 嵌套body+press、Scene父菜单+主题、Eraser/Recall预缩放、tooltip临时字号、dock/FPS/细刻度各自端点与中间小数轨迹。
- p接近1与结束/首idle的差随dt趋零；p中途反向提交dt=0不跳；失败重试不重置c。

## 状态

只写本research文件。未修改产品/测试/spec，未build、GUI、Computer Use或耗时测试；最终代码正确性及实际执行结果待implement完成后的完整审查。
