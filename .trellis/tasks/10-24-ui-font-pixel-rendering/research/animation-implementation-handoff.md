# Animation implementation handoff

日期：2026-10-24。本轮承接首轮提交，未commit/push、未结束或归档任务，未启动GUI。

## 实施结果

- `Bar.Animation.cppm` 新增每Word/样式有界的 `BarUiFontCorrectionClass`，复用 `BarUiValueClass`。真实目标变化时调用生产 `ResolveBarFontCorrectionTarget(T, allow)` 求 `floor(T)/T`，从当前系数重定向；同frameTime只推进一次。首次、禁动画或明确forceReplace直接落目标；T=0退场保留当前系数并使下一次有效目标重算，0<T<1仍可收敛到0。
- `Bar.UI.cppm/.cpp` 的Word保存mutable渲染修正状态及独立名义目标覆盖值，原size.val/tar/middle/曲线不变；Initialization重置状态，公共Word推进仅在forceReplace时要求下次直接落目标。
- `Bar.Rendering.cpp/.cppm` 用 `BeginFontFrame` 复用owner帧时间/dt，解析最终目标zoom及绝对目标Y向量，实际字号只乘连续cCurrent。没有动画中间值floor或结束补floor。默认静态入口仍能解析实际DC；主栏和Scene生产入口明确提供目标。
- 目标Y向量使用double显式组合：不再用当前FLOAT矩阵乘double比值去抵消按压/弹出，以免释放目标13px算成12.999999...后被误截断；不使用epsilon/round。
- 公共/固定按钮把press.tar交给栈上 `FontTargetScope`，手工属性按压、freeLineRow同样接入。作用域只改变字体目标，不改几何矩阵。
- 主栏BeginFontFrame目标zoom来自displayUiBaseScale.tar×configZoom；body/grip按直接抓手静止目标或恢复Y=1，base/rigid目标Y=1。上一帧字体active在清本帧汇总前进入真实damage/retry，使c尾段不会被idle吞掉。
- `BarAttributePanelMotion::TargetGeometry`、`BarAttributePanelFrame::targetPose` 提供并组合父子目标；Scene菜单/主题向字体传当前/目标pose。Eraser/Recall派生SetDirect字号显式保存nominal×targetPose，避免把每帧当前pose当作新目标。
- PageControl在bounds缓存早退前交接AnimatedBounds.targetScale；SetFontTargetScale立即同步renderer，SetPose在实际Render时再组合最新目标，兼容同now的Advance→SetBounds→Render顺序。Scene保留fontPending进本帧active，PresentScene绘制后再次合并新启动的字体active，接回真实continueRendering。
- Tooltip传源annotation/overflow Progress.tar；数值预览popup传其源progress目标；dock复用既有word状态且排除内容中点的临时缩放。细刻度一个固定样式c，FPS独立c且明确nominalTarget=12.0，避免从浮点当前字号反推常量。细刻度缓存仍包含实际本帧格式字号。
- 三语说明改为静止字号向下取整、动画连续；标题、配置key/default及设置字体渲染保持不变。MeasureText仍用未截断布局上界。

## 本轮回归

- Headless：生产目标helper与生产c类，原size真实13→15动画、中间小数、结尾到idle无补floor、零dt中途重定向、重复目标、同帧重复采样、原始middle/tar保留、T=0安全、禁动画和forceReplace。
- 既有无窗口字体离屏：调整内容缩放像素对照预期；新增真实公共Button/D2D按压与释放、0.95等FLOAT矩阵下13px释放目标恒定、同帧双Draw只推进一次、按住静止12px/释放13px端点、内容中点/Back过冲不重定向c。
- PageControl既有RunOffscreenTests：真实无HWND回调，只有字体政策改变也返回Continue、提交多帧后归Idle；覆盖新动画的真实调度返回链。

## 已执行检查

- i18n sync后按新基准恢复已提供的英文/繁中说明，随后check：两语均437/437通过。
- git diff --check：通过。
- 20个受改产品/测试/资源文件与checkout过滤后的HEAD对照BOM与换行：全部保持原状态。
- 静态搜索确认旧逐帧解析helper已移除，目标helper由生产c类直接调用；没有无界map、全窗animating豁免或设置字体修改。

## 主会话验证

产品源码已稳定。按约定本代理未运行MSBuild或耗时测试；请运行完整ARM64 Debug solution、新Headless --no-window和Inkeys.exe --bar-eraser-offscreen-test。若发现真实源码错误请交回本代理；真实设备观感仍未做GUI验证。

## Trellis检查局部自修

- direct-hold字体目标改为生产/Headless共用的ResolveBarBottomDockFontTargetScale：消费实际mapping.rigidOverlayTranslationYDip（Resolve保存的已解析抓手偏移），capture-bottom目标仍0，释放目标1；不再消费未重基准的frame输入offset。新增4个倍率的5→6DIP反例及capture/释放回归。
- PageControl字体续帧测试先断言baselineIdle，再改变政策，避免原动画遮蔽字体尾段。
- helper首次字段误名经C2039反馈后已核对真实结构并修正，未改ABI；请使用最终源码复编验证。
