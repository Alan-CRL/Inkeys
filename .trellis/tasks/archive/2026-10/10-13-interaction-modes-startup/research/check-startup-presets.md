# 独立 Trellis 审查：启动位置预设与环形进度

结论：最终代码质量门通过，无剩余代码/配置/国际化/规格问题。保持任务进行中；未启动GUI、创建commit或push。

## Findings (fixed)
- File: Inkeys/IdtMain.cpp；Inkeys/Inkeys/Display/Display.cpp/.cppm
  - Issue: 初版Preview早期从screen DC读取系统DPI，正式Bar采用主显示器有效DPI，mixed DPI下缩放和24 DIP边距可能不同。
  - Fix: 实施代理按审查建议增加窄的Display::QueryEffectiveDpi，只复用现有动态Shcore/GDI策略，不提前枚举拓扑/EDID；Preview与正式快照使用相同策略。
- File: Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp
  - Issue: 初版DrawProgress忽略DrawRing路径创建/结束sink的HRESULT，可能错误记录已绘制的可见进度。
  - Fix: 实施代理返回HRESULT并独立回传visible，仅成功时记visible；失败先EndDraw，再沿既有Retry/device-loss/opaque Bar恢复路径处理。
- File: InkeysHeadlessTests/startup_preview_state_tests.cpp
  - Issue: 初版Config值快照使用不存在的copy constructor，与实际生产FIFO的默认构造+赋值不同，编译失败。
  - Fix: 使用Config payload默认构造后operator=，直接验证生产支持的冻结快照语义；未修改Config公共复制接口。
- File: .trellis/tasks/10-13-interaction-modes-startup/research/replay-startup-placement-ui.py
  - Issue: 新预设页面缺少真实ImGui块/Combo与整体侧栏几何的回放。
  - Fix: 审查代理新增提取回放，覆盖三语与0.6/1/1.5/2缩放，0→1→重复1→2→0三次冻结写盘记录，说明裁切、标签/下拉重叠、16个侧栏矩形及固定页脚边界。

## Findings (not fixed)
无剩余产品问题。最初审查推测的导航重叠经真实矩形回放证实不存在（Experimental446..482 DIP、divider486、Community490..526）；临时32 DIP导航修正已完全撤销，原36 DIP尺寸保持。

## Verification
- Lint: pass。git diff --ignore-submodules --check；i18n check en-US/zh-TW均340/340，100%；产品文件BOM和CRLF审查通过。
- TypeCheck: pass。主会话完整InkeysRepo.sln Debug|ARM64，原生ARM64 MSBuild，退出0；日志build-startup-presets-verified.log。旧C4244/C4267不作无关修改；poststep的pwsh PATH提示由独立i18n check补充，不修改构建配置。
- Tests: pass。最终产物InkeysHeadlessTests.exe --no-window退出0，PASS animation correctness；日志headless-startup-presets.log。真实Config Mini/All/非法回退/冻结赋值/下次启动持久化、共享几何DPI×zoom/负坐标/24DIP边距、展开宽度缓存、原进度reducer和顺序committed-alpha交接通过。
- 生产状态赋值提取回放：主会话/实施代理ARM64编译运行退出0，覆盖3种位置、非法值回退、Floating/Free/fold、MainBar/子图标/文字/PNG直接透明终态及Win32宏保留。
- 新预设页面/完整侧栏回放：审查代理与主会话ARM64编译运行退出0；12组writes=3、clips=0、overlaps=0、navigation_failures=0。
- 真实离屏D2D/WARP像素：生产StartupPreview.Drawing.h及shared n3 builder，center/corner两形态×1/1.5625/2/2.25倍率通过；0/50/100%、圆头、失败0红色轨道、反光mask/端点、主体精确80×scale及填充alpha。40份BMP位于Build/StartupPreviewVisualQA；审查代理查看corner2x50、corner2.25xfatal和center1.5625x50，形态与预期一致。
- 代码路径确认：IdtMain唯一Mini冻结值传Preview及Initialization；UI只冻结写盘；首次corner几何/折叠在render注册与首个layout前设置，布局只应用一次；左右展开方向和PPT/白板场景分支保持；缓存发布固定展开layout目标而非80；共享builder无Bar renderer导入循环；现有设置defer死锁修复未退化。

## 验证边界
未创建HWND或进行GUI点击/真实ULW首帧、应用重启、实际mixed DPI热插拔验收。状态回放使用最小属性/按钮list桩，UI回放使用默认字体、写盘记录器和侧栏占位标签（检查真实cursor/Navigation矩形）；因此不是嵌入字体或现场窗口像素验收。离屏像素调用实际生产绘制原语，不是另写一套视觉模型；真实窗口committed-alpha和Win7仍需专用环境，不能据此声明现场PASS。
