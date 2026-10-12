# 第八轮实施：开屏时序与有界遮挡快路径

2026-10-12。按 entry-performance-revision.md 和主会话分工直接实施；沿用工作区，不启动 GUI/Computer Use，不执行构建或测试产物，不 commit/push。构建、无窗口 WARP、必要 native 对照及独立审查由主会话统一执行。下述为源码变更和静态检查，性能结论待真实测量。

## 实现

- `Setting.Pages.h/.cpp`：HomeEntryTiming 单一常量为 wait240ms/write1060ms/hold600ms/dock540ms/settle220ms，动画逻辑时间总长约2.66s；仍为原始六笔并行书写、真实 cubic 空间曲线归位和透明 AA 网格。
- HomeMotionState::Preparation 仅保存值键、有限计数；初次真实完整页/导航连续两次 backend+Present 成功且动态字体请求已处理后推进时间。首次测量/字形烘焙不消耗书写时间；最多六次准备预算，失败继续原 HRESULT Retry/DeviceLost 路径并收束动画，防止永久加载遮罩。
- CPU：真正完全不透明且键稳定时跳过整段页与导航生成，继续 ImGui NewFrame、根窗口、caption、输入取消屏障、前景字标及现有共享帧循环。渐退边界1.90s先保持不透明，恢复两次成功完整 child 排布/提交，才进入透明归位，防止跳过 BeginChild 后 AutoResizeY 首次恢复裁切。
- GPU：当前帧 DrawData 原地筛选根窗口（只 clip 到标题栏）、titlebar child 和 foreground；不复制整页、不保留跨帧 DrawList/SRV/ImFontBaked 指针，Textures 列表仍交原 DX11 事务后端处理。不透明才过滤，首透明帧恢复所有真实页面/导航绘制。
- 失效键涵盖 DisplaySize/FramebufferScale、内容矩形、gutter/实际DIP倍率、font/atlas身份、fontSerial、atlas UniqueID/宽高、语言、主题及 device epoch。键变化强制真实页并重验运动包络，保留 Show 消费和当前阶段；Show 重置准备状态；失败/遮挡提交回滚该帧逻辑时间并重新准备。
- 原字标 Mesh / DrawWriting / 圆头接合和采样精度没有改动。先测被遮挡页面成本，避免无实测依据改前缀索引/顶点或降低精度。没有新 shader、texture、device、thread、timer 或 RenderPipeline 全局调度变更；沿用 FL11.0 WARP 和现有 Win7 平台更新路径。

## 有意义的回归与对照

- `setting_design_tests.cpp`：旧时序 fixture 调为2×；共用准备 helper 断言总长、最快rate5、重复完成不计两次、21类键失效、失败回滚/预算、透明前双确认及 Show/导航契约。
- `setting_home_warp_tests.cpp`：冷首开直接使用生产准备/过滤 helper，真实动态glyph/AutoResize/后端成功确认；浅深首透明 RGBA 与同一真实 Frame 的完整页 control 精确比较，完成与静态主页精确比较；键盘真实 Tab 取消及恢复，无伪造 Focus/导航ID。保留原真实三语言、七倍率、字标/k/活动圆头、外链、轻点/拖动/Tab/Enter/Space及滚动回归。
- 同一 Frame 仅用 optimized 测试参数选择完整底层路径或生产快路径，同进度、同字体、RTV 和 FL11.0 backend；writing/hold/dock/settle 各20帧预热+100帧采样，输出 CPU 和 submit+copy/Map完成 median/p95、vtx/idx/batch、ImGui alloc、字标capacity与准备state bytes。copy/Map不是纯GPU时间，不代替native Present帧间隔；ImGui alloc不包含full页面std::string/vector及驱动。

## Debug native 采样

仓库 Debug 也用 NDEBUG/非debug CRT，不能以 `_DEBUG` 判定构建。主会话确认 ownership 后，仅 `Inkeys.vcxproj` 的 Setting.cpp 项追加 Debug 条件 `INKEYS_SETTING_ENTRY_PROFILE`；不改工具链、CRT或优化配置。

显式环境变量 `INKEYS_SETTING_ENTRY_PROFILE=1` 才启用固定512样本；无每帧格式化/分配/写盘，无新调度请求。结束/取消/隐藏后新Show/会话销毁一次汇总 `[SettingEntryPerf]`：分阶段 prepare/wait/writing/hold/dock/settle 的 CPU生成、backend调用、Present调用 median/p95/max；总体帧callback间隔与Present返回间隔 median/p95/max、样本数/丢弃数、fast帧数、最大vtx/idx/batch和固定采样字节。Release完全不编译该诊断；不启用时Debug不调用采样时钟。

采样不含 RenderSettingFrame 协程恢复之前的 resident 初始化/resize/font重建；CPU生成字段含NewFrame/实际UI生成及RTV clear，backend字段是调用提交时间，Present字段是返回等待。Present返回间隔不是屏幕实际显示帧率。512容量超出会计数dropped，不隐藏样本裁切；完整冷首开与1x热重开测量应注明上述边界。

## 已完成静态检查

- 修改的六个既有文件对HEAD保留UTF8/BOM与CRLF（bare LF=0）；没有国际化可见文案修改。
- `git -c submodule.recurse=false diff --ignore-submodules --check` 通过；普通git递归子模块报HiMsg权限问题，未修改子模块/权限，静态diff显式只读本任务主仓。
- 本代理未执行编译、产物、GUI或性能测量，不能宣称已有性能收益/60FPS/Win7实机通过。后续按主会话实际构建和测量反馈修复，再由独立检查复核。

## 首轮构建/测量反馈与测试隔离修正

主会话完整 ARM64 solution 构建成功（Exit0，292.67s，0errors）；首轮 headless Exit1，273.9984s，15个 HomeWARP 断言失败，其余回归未新增失败。失败没有标为通过。

只读现存实际 RGBA：light settle 与静态基线的下方123185差异像素通道最大差全为1；dark124656差异像素多数差3，布局/文本坐标未偏移。ImFluent AnimateColorU32 在连续主题/Disabled颜色过渡使用量化U32缓存；AnimFrameContiguous 缺席后直接回精确target。新Tab检查曾放在像素夹具之前，实际 Disabled/导航状态和跨主题量化基线因而不可比较。

本代理后续仅改测试：每主题静态基线先执行两个真实非主页ImGui/DX11帧，再建立正常完整首页的稳定RGBA，检验既有页面缺席重现契约；Tab检查移到所有浅/深像素验收之后，以真实跨帧鼠标位移恢复导航hover状态。没有放宽RGBA或更改全局/主页控件配色。旧batch<=2假设也修为逐命令诊断，并保留只允许root/caption和foreground所有权、有效纹理与caption/foreground clip，核验两个root clip命令加单份foreground三批次。

首轮同帧同设备基准给出writing full CPU2921us/完成39252.8us、fast1435.8us/12296.7us；hold full2706us/39974.8us、fast1278.6us/14560.4us。上述仅为首轮失败回归期间测量，并非最终通过证据或native可见FPS。产品逻辑及原始几何未追加修改，待主会话再构建/回归及原生测量验证。

## 通过回归后的 Solid 根重复填充候选

第二轮主会话完整 ARM64 solution + --no-window 已通过（headless Exit0，234.0755533s，stderr为空）；静态精确RGBA、首透明、Tab及恢复断言均通过。主会话必要native四次同配置Show记录writing CPU约0.59–0.68ms、backend0.09–0.11ms、Present11–13ms，Dock Present20–23ms；callback gap median17.79–17.95ms、p95 24.3–28.65ms。此前静止/复位回归恢复验证没有发现新的产品缺陷。

按主会话追加授权，只对活动主页入场且Solid、style.Alpha==1、WindowBg.w==1及有效规范化RGB时，根Begin前SetNextWindowBgAlpha(0)。原NoBackground候选未采用，因为该flag还会删除outside border。alpha override只省根背景AddRectFilled，保留原flags、border、caption、child、输入与前景透明混合。本帧backdrop和WindowBg快照在根Begin前读取，判断与最终Clear用同份快照；caption主题按钮仍下一帧Apply。Mica/Acrylic/半透明style或background/静态页面严格回退原背景路径。

共享predicate进入真实Warp Frame；Submit增加可选同生产Solid clear，默认旧透明清屏不动。新增浅深8阶段（首帧/写/hold/透明边界/dock/settle/终态）的full-root与suppressed-root逐像素RGBA和标题/外框比较，三个透明/部分Alpha回退；same Frame 20warm+100采样分别对照Solid写/hold/dock/settle根背景保留和省去。Debug/env固定512采样只增加原Sample尾部padding的bool，输出真实solid-root-suppressed总体/各阶段计数，没有新时钟/线程/环境配置。Win11如实际Mica而计数0，则native只证明回退正常，不能以其噪声声称Solid收益。

本代理只完成源码和静态格式检查，未构建或执行候选。待主会话真实WARP精确RGBA/性能与native回退验证；若不能证明等价或收益，则撤销该局部候选，不保留死代码。
