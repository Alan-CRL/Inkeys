# 开屏与原生框架规范摘录

保留ShowSerial开屏、字体/设备资源恢复、输入取消屏障、成功Present提交缓存及Win7原生框架合同；本任务只在明确需要的位置接入标题栏可见性和新页面动效。

## .trellis/spec/native-desktop/rendering-and-ui.md:2314-2331

- 手写字标来自网站 NewHome3 曲线与弧长彩虹色，完整包含连笔和 y 下伸。本地一次缓存曲线轮廓与归一化位置；由原 ImDrawList 管线提交透明抗锯齿网格。真端头用定向半圆共用笔画截面顶点，该端头接合的 AA 仅在外轮廓；首尾重合的回折笔画统一接合宽度和截面方向，沿短弧长平滑过渡并避免重复端头，不能把完整圆盘的透明外环叠到笔画内部。禁止逐帧采样贝塞尔或新增 shader/设备/线程/定时器/额外帧请求；开屏允许同设备单份临时页面纹理，仅通过GPU copy复用真实页面像素。
- 横幅局部浅蓝/薄荷/淡紫与深色对应色不改变全局控件。700 DIP 宽窄分界，左右 28/20、顶部 56/40、底部 28 DIP；字标宽为可用内宽 80%、上限 520 DIP，预留变换包络、保持 8 DIP 圆角及完整下伸。
- 字标下 32 DIP 的标语统一 14 DIP 正文、字重与颜色。用同一真实字体测量两个语义段及本地化 HeroJoin，能容纳全文时一行，不能时优先语义两行并允许段内自然换行；中文连接符为空，英文为空格，不缩字号。分割线与开源标题/说明/按钮全居中，标题 16/22 DIP，说明 12 DIP；按钮等宽 36 DIP 高、最小 156 DIP，放不下时上下排列。版权字号保持，横幅后 16 DIP、段间 4/2 DIP。
- 版权和代码托管说明是三语言真实文本；完整许可证名以及 GitHub/GitCode 分别具备独立链接语义。换行片段需要稳定 ID 和完整可点击覆盖；只有相应名称是链接，前后说明保持普通文字，不烘焙到图片。
- HomeMotionState 由常驻设置会话持有，仅 ShowSerial 的真实隐藏→显示触发入场；内部导航返回主页不重播。时序统一由HomeEntryTiming定义：速率1时先稳定240ms、六笔并行写出1060ms、完整字标原地停留600ms，再同时启动背景渐退与540ms三次Bezier空间曲线归位、220ms小幅落定，共2.66s逻辑动画时间；有限准备帧另计且不消耗书写时间。书写/停留期间客户内容区覆盖层alpha=1，标题栏排除在外；前景只绘制一份字标，横幅字标保留布局但不提交。全局动画禁用/离页/实际输入直接稳定终态，取消帧仍阻断输入，不能点穿不可见操作。重复Show/激活/主页点击、resize、主题/字体/设备重建、滚动与鼠标靠近均不重播；缺省空上下文静态绘制。旧反光/水波不再播放。delta沿用设置时钟重基准、总开关与速率，不新增计时器。
- 动态atlas的Build不代表真实字号/语言字形已预热。生产会话先在不透明覆盖下生成完整页与导航，两个不同全帧backend+Present成功且字体事务就绪后才推进书写；准备尝试有六帧预算，失败回滚该帧时间并沿原HRESULT Retry/DeviceLost路径收束，不能形成永久加载遮罩。完全不透明且值键稳定时暂停底层页/导航生成；当前帧DrawData只保留root标题栏clip、titlebar与foreground，Textures队列仍交生产DX11后端。书写完成（WritingEnd）后仍保持完全不透明，至少两个成功完整布局帧且当帧layoutReady后，先提交不含foreground的真实主页/导航，再CopyResource到同设备单份临时纹理，最后绘制唯一遮罩/字标。捕获成功与字体事务就绪后才发布revealReady。Hold后段、渐退和归位暂停正文/导航CPU生成，Clear后CopySubresourceRegion原样回写客户内容，再提交标题栏/foreground；不能用AddImage再次混合预乘RGBA。终态前在缓存遮挡下恢复至少两个真实child布局帧且layoutReady，再恢复正常整页并释放缓存。不得跨帧保存裸DrawList、ImFontBaked或旧字体UV。
- 主页Pending/Playing完整准备、截图及终态恢复帧向外层BeginScrollChild传settleScrollbarLayout=true，native在Begin前使用完整8 DIP轨道/2 DIPpadding/三态alpha，Finish依据实际ScrollbarY写回value/target/start/progress的0或1。只沿当帧父子窗口标记继承给嵌套正文，不影响其他窗口和后续普通帧；无溢出不保留退出装饰或空白。缺席后的160ms条宽补间会改变WorkRect和hero目标，不能把过渡宽度截进缓存。默认false保持正常滚动条显隐动效，不用AlwaysVerticalScrollbar或额外等待帧掩盖。NoInputs仍只按导航阻断/取消首帧决定，不能为了视觉一致解除输入屏障。
- CompleteHomeEntryLayout仅递归当前真正提交的正文子树，在outer EndChild前按vendored CalcWindowContentSizes的collapsed/hidden保留、ContentSizeExplicit及ImTrunc64规则检查当前ContentSize与ContentSizeIdeal是否消费本帧CursorMax/IdealMax测量；不能把字体准备或固定两帧等同于nested AutoResize尺寸稳定。Begin每帧清layoutReady，失败/key/cache失效撤销；实际GPU捕获和revealReady发布均检查该帧ready。恢复同样至少两次成功提交且ready；capture/restore各自六次不收敛则收束动画，不能永久遮挡或靠猜测增加固定暖帧常量。准备期间不消费书写/归位时间。
- 活动主页入场且Solid backdrop、style.Alpha和WindowBg.w精确为1、RGB有限且在0..1时，可以SetNextWindowBgAlpha(0)省去已由同色不透明Clear建立的根背景填充。根Begin前读取同份backdrop/WindowBg快照供门禁和最终Clear；保留原flags、外框、caption、child与输入。不能使用会同时删除外框的NoBackground；Acrylic、部分alpha或静态终态保持旧路径。必须在真实同色Solid clear下对浅深八阶段full/suppressed精确RGBA及title/border比对，并单独报告透明/部分alpha回退与配对性能，不能将Solid收益推广至Acrylic。
- HomeEntryFrameKey涵盖客户/内容矩形、FramebufferScale、DIP倍率、字体/atlas身份与序号、语言、主题、设备epoch、atlas UniqueID及尺寸、backdrop透明模式和style.Alpha/WindowBg RGBA五个材质值；任一变化撤销缓存并强制完整页准备并重验实际可见包络，同次Show不重播。弹层/真实输入取消时使用完整页并保留取消帧屏障，普通Tab/点击/滚动不因快路径失效。隐藏/最小化/遮挡继续复用既有调度和时钟重基准，临时页面纹理仅由设置共享渲染线程持有；Hide、尺寸/字体/epoch切换、输入取消、终态和失败均撤销，清理须能从RenderSettingFrame同步执行，不能依赖隐藏协程恢复。无新线程、timer或额外帧请求。
- CPU侧仍逐帧NewFrame、布局、输入、tooltip/menu延迟、caret、下载文字轮询与动画时钟；最终DrawData精确等于上次成功呈现时，可在Clear之前跳过cache copy/backend/Present，但必须返回Continue。不能从连续两帧相同推导整个设置客户端Idle；共享FrameInterval和Present策略保持。准备/捕获/恢复由RequiresHomeEntrySubmission强制实际提交，skip不调用CompleteHomeEntryFrame或虚增暖帧成功。
- FrameSubmissionCache单帧存储预算1MiB，只存顶点/索引和命令实际渲染字段的值；ImDrawVert无padding须static_assert，命令逐字段比较ClipRect/GetTexID/VtxOffset/IdxOffset/ElemCount，不比较命令padding，不靠hash。FrameSubmissionKey含HomeEntryFrameKey、clear RGBA、Show/Hide/主题serial、target RTV和页面cache revision。只有backend成功且真实Present==S_OK后ConfirmPresented；热duplicate只memcmp，不复制/扩容，超预算或分配失败回退提交。callback、非OK纹理事务、输入、旧backend失败和所有入场准备事务强制提交；device removed沿旧恢复路径。旧backend Map错误不能在RenderDrawData重置LastResult之前反复提前退出。失败/occluded/Hide/resize/fonts/主题/epoch/teardown撤销基准。
- 已有页面cache在覆盖opacity与style alpha精确1、客户矩形经DisplayPos/FramebufferScale映射后四边全为整数像素时可省copy；部分透明、分数边界或缺失DrawData仍copy，截图保留待渐退。每个实际提交仍正常ClearRenderTargetView，不依赖discard backbuffer内容。省Clear的完整覆盖实验无稳定收益，门禁只保留在离屏测试局部，不加入生产API或提交键。
- 完整归位后仅字标参与4°/6 DIP、1200 DIP透视与约160ms跟随；用未变换布局框和框外120 DIP衰减计算目标。只接受真实鼠标；触摸、笔、按住/拖动、弹层和失焦回正。字标包络完全离屏时不推进姿态或提交字标网格，滚回不制造新Show事件。原彩虹与透明AA保持，文字、按钮和命中矩形不参与运动；一次性按钮装饰仅在hover上升沿触发，不循环。
- 原曲线轮廓、六组笔画截面/书写时间/索引区块在首次Build缓存；书写按每笔完整前缀选择索引，活动定向半圆共用截面，禁止任意截断全局索引或提前显示终端帽。写完立即复用原完整网格，最终RGBA与静态一致。视差缓存仍以姿态/宽度/DPI/主题为键，入场时钟不进入该键；热路径无重新采样/几何扩容。临时笔头为有界额外顶点，覆盖层与单份字标只增加一个前景批次；不新增Shader、设备、线程或额外渲染请求；页面截图独立于字标网格缓存，只允许一张同设备copy-only纹理。
- 首次有效实际布局决定本次Show是否可播放：完整横幅必须在主内容clip内，中央起点、目标字标和整条曲线包络在客户内容区内保留安全边距。按真实三语言/font/DPI/用户倍率/导航宽度判定，不用固定分辨率代替；首帧AutoResize内层clip尚为空时使用有效外层viewport及实际测量，不能先闪正常页面再覆盖。门禁按viewport/contentClip/hero/brand/DPI缓存，稳定帧不重复计算路径包络；布局变化重验，变小不安全即取消，本次放大不能补播。
- HideSerial由窗口状态同步持有，渲染线程在下一次可渲染帧NewFrame后、导航/拖动/布局前消费，共享复位函数回Home并清弹层/焦点/内容reveal；BeginScrollChild(resetToTop)在Begin和旧拖动目标消费前清Scroll、ScrollTarget及拖动storage。隐藏期间不强行Resume无RTV协程；即使Hide→Show间没有渲染帧，重开首次布局仍在主页最顶端。最小化/遮挡不递增Hide或Show，恢复delta重基准。弹层关闭只在栈非空时执行；从首层弹层保存的导航窗回溯到设置root，清旧NavLastIds/child记忆后FocusWindow(root)+NavInitWindow(root,true)。不能仅FocusWindow(nullptr)：无缺帧重开根窗不Appearing时Tab会失去入口。输入取消还需检查IsKeyDown，不能漏掉Show前已按住的导航键。
- RenderHome 仅返回动作；许可证与 GitCode 和其他外链一样由 Setting.cpp 现有异步 Shell 队列处理，Star 复用 GitHub，Sponsor 复用 Support。绘制和测量不得执行 Shell 或写配置。i18n 使用源 JSONC → sync → check，不手改生成头。

## .trellis/spec/native-desktop/rendering-and-ui.md:2480-2500

### 1. Scope / Trigger
设置原生外框颜色、caption、NCCALCSIZE、菜单与 Windows 7 兼容改动时适用，不扩展到主栏或其他窗口角色。

### 2. Signatures
`Window::UsesLegacySettingFrame()` 动态、缓存 RtlGetVersion 的 <6.2 判定；查询失败保守用旧 frame。`ResolveVisibleWindowFrameInsets(..., useThinTopBorder=true)` 仅现代恢复态可收薄顶部。

### 3. Contracts
- 设置外框 `DWMWA_BORDER_COLOR=DWMWA_COLOR_DEFAULT`，由系统处理获焦/失焦与用户边框着色偏好；不能用设置浅/深主题的固定灰色覆盖。
- 旧 Aero 标准隐藏创建后先 `GetSystemMenu(FALSE)` 建立 HWND 自有菜单，再仅清除 `WS_CAPTION`，保留 THICKFRAME/SYSMENU/MIN/MAX；复用既有 FRAMECHANGED/真实尺寸校正后才显示。现代 style 不变。
- 旧系统保留完整四周 sizing frame，不套用现代薄顶；不禁用 composition 或 DWM NC rendering 来掩盖按钮重复。
- captionless 旧窗口显式把 Alt+Space、图标菜单和标题栏右键接回 HWND 原生本地化系统菜单，按还原/最大化/最小化和窗口能力设置菜单项；命令仍走 WM_SYSCOMMAND，SC_CLOSE 隐藏设置。
- 菜单及 HWND 样式仅在窗口线程处理，不持 ImGui/render 锁进入 TrackPopupMenuEx；不使用 WTNCA_NOSYSMENU 破坏系统菜单，也不新增 Win7 缺失的 DLL 导入。

### 4. Validation & Error Matrix
菜单初始化/样式设置失败记录错误码并走既有创建回滚；首次创建、NCCALCSIZE 与最小尺寸共用同一几何。DPI/主题/composition 变化保留既有重算；本地菜单使用窗口持有副本，不调用 GetSystemMenu(TRUE) 重置。

### 5. Good / Base / Bad Cases
Good：现代系统外框随系统焦点着色，旧 Aero 只有一套 caption；Base：系统未启用强调边框时保留系统中性色；Bad：固定灰覆盖系统色、整个禁用 DWM，或删除 caption 后仍假定默认过程会显示菜单。

### 6. Tests Required
完整 Debug|ARM64 Solution 与 --no-window；旧/新 frame 的 round-trip、minimum、八方向 resize/client 边界和系统菜单状态回归。真实 HWND 样式/菜单用既有窗口测试，但仅有明确 GUI 授权才执行；Win11/headless 不能替代 Win7 Aero、焦点、Shell/Snap 和实际菜单视觉验收。
