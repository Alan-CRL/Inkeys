# 本轮技术设计
- 目标是消除设置渲染时执行 SyncDraw3State 导致的跨线程等待循环，恢复下拉选择和独立信息说明卡片；全部自动启动判定及三语文案保留。
- 已确认链：RenderSettingFrame 持 settingImguiMutex 调用会话，选项调用 SyncDraw3State -> ApplySettingOwnerDesiredState -> Window::SetSettingOwnedByDrawpad -> Submit 的 future.get；设置 HWND WndProc 同时需要 settingImguiMutex，存在反向等待循环。
- 复用已有 Setting FIFO 业务调度，把需要同步等待 HWND 的绘图状态应用从 ImGui 临界区移出；实施前确认所有相关等待及状态/保存时序，不扩展到不相关架构。
- 下拉框复用 Widgets::combo，实时取 PaintDevice；说明复用现有 PPT 提示卡片的图标、边框、内边距，自适应三语文字高度。
- 回归验证必须覆盖跨线程调度边界；之前仅替换 SyncDraw3State 为计数器的回放不足。禁止 GUI/Computer Use，不能声称已做现场GUI复现。

## 最终实现
- 全局 SyncDraw3State 入口增加 Setting::DeferDraw3SyncIfRendering guard；设置会话的两个 Resume 位置使用可嵌套/异常恢复的 thread_local RAII标记。guard将Sync投递到既有业务FIFO，业务worker执行原Sync。非render调用维持同步。
- 队列停止拒绝新任务时也不得锁内回退；接受的任务按既有Stop排空。配置写盘仍由生产者冻结快照，FIFO顺序保持。
- 审查发现延后worker与设置写入并发访问模式字段，最终使用既有可复制IdtAtomic<int>，Sync单次load，JSON显式load；不改配置键/数值/缺省。

## 当前阶段设计（已审批）
- 共享UI/StartupPlacement.h，namespace Inkeys::UI，enum class StartupPlacement {CenteredBottomBar=0,BottomLeftFloating=1,BottomRightFloating=2}；NormalizeStartupPlacement(int)；共享位置resolver按同一pixelScale和可见描边包络计算RECT，corners24DIP。由正式Bar implementer先创建并通告接口。
- 配置UI.Bar.StartupPlacement:IdtAtomic<int>=0。Mini冻结后传Preview StartOptions.startupPlacement与Bar::Initialization(StartupPlacement)。Initialization保存一次性选择供首个完整layout使用。
- 首帧布局分离startup placement与whiteboard/PPT dock request；corner直接初始化所有折叠状态而非播放收起。保留后续方向/吸附；fit/尺寸clamp不能造成Preview/formal DPI×zoom重复换算。
- Preview中心宽用现有展开缓存，corner用main button80 nominal DIP；正式首帧缓存总是展开目标layout名义宽度。Preview关闭也不能丢配置。
- Preview实现复用原reducer/owner/render/failure机制；无状态superellipse路径抽共享header，Bar原缓存保留。Ring32/3圆头，gray/white0.24轨道，蓝/红用原brush；仅用D2D/ULW。
- UI新增预设页/三语说明，仅下次生效，既有QueueConfigWrite冻结；旧Preset schema/记忆不恢复。
- 配置/几何/进度tests复用startup_preview_state_tests；增加真实离屏D2D/WARP像素检查，不创建HWND。不能以仅状态测试声称视觉验收。

## 当前阶段实现细节
- 预览名义宽度：角落80 DIP；居中按展开目标宽度缓存（缺省470 DIP）。描边外包络另外加1 DIP，因此共享resolver收到角落81×81、默认居中471×81 DIP；整数窗口中保持主体精确80×pixelScale，不从舍入窗口反算主体。
- Mini与正式Bar查询同一主显示器的有效DPI；Display::QueryEffectiveDpi只复用现有动态查询及GDI回退，不触发EDID/全显示拓扑枚举。
- 环形与外形绘制原语位于StartupPreview.Drawing.h；Superellipse.Geometry.h只共享无状态路径构造。保留各自mask/device资源所有权。环形HRESULT失败回到已有重试链，不能提交sticky-visible或failed-visible状态。
- 三种启动状态、所有折叠子控件透明度以生产赋值提取回放验证；新预设页面和完整侧栏以实际ImGui块提取回放。回放不创建HWND、不验证真实ULW首帧，也不等于嵌入字体的GUI视觉验收。
- 完整侧栏实际回放证实原36 DIP导航行可容纳新增预设项：Experimental446..482、分割线486、Community490..526；无需改变原导航尺寸。

## 2026-10-14 进度形态与反光修正
- StartupPreview.Drawing.h统一选择居中旧条形进度与角落现有环形进度，生产与离屏fixture调用同一选择，避免测试仍无条件绘制环形。
- ResolveStartupPreviewShimmerGradient固定X/Y绝对跨度相等，保留原向量长度；原宽高自适应向量的角度随缩放改变，特别是corner 2x时高光带约26°，是本轮纠正原因。
- 修正仅覆盖Preview进度原语/选择、纯渐变方向和相应像素/几何回归；保留此前未提交的安全重试测试入口，不改主栏位置、配置、UI设置或交接状态机。
