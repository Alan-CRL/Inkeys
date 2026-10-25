# 第三轮圆头接缝与明显横幅波纹独立审查

依据 ripple-revision.md（覆盖上一轮仅笔画轻微提亮/背景不变约定）、check.jsonl 所列 native-desktop 规范与实际 HomeBrand/Pages/设置会话/WARP 路径。产品已冻结，最终完整编译、全部无窗口回归与真实 WARP 图像复核通过。本记录区分首次失败、实际产品修复和最终证据。Reviewer 未启动共享构建/测试、GUI、Computer Use、外链或 commit/push。

## Findings (fixed)

- File: Inkeys/Inkeys/UI/Setting/Setting.HomeBrand.h
- Issue: 初版 DrawRipple 深色峰值绿通道为 174+96×0.92≈262，IM_COL32 未钳制会把溢出位写进相邻通道。主会话识别并由实现代理修复，reviewer 只读复核。
- Fix: 深色通道增量改为 81、浅色为 104，所有 RGB/alpha 峰值均在 [0,255]；仍维持明显柔光波带。

- File: InkeysHeadlessTests/setting_home_warp_tests.cpp
- Issue: 首版 opening 像素计数把字标矩形内的背景也计为 ink，可能未验证艺术字笔画本身提亮；原 27 帧相位的共享波带可能已越过多数笔画。
- Fix: Reviewer 反馈后实现代理增加早期 14 帧真实生产 DrawData 的隔离字标 RGBA 与静态对照，断言 100+ 像素 RGB 强变化且 alpha 变化严格为 0；中晚期横幅扩散独立验证。不是用替代字标绘制函数或仅 CPU 理论颜色冒充 WARP 像素。

- File: InkeysHeadlessTests/setting_home_warp_tests.cpp
- Issue: 首版 cap 只取 r−1.2 的保守核心，可能避开用户截图的约 1px 侧边接合环带。
- Fix: Reviewer 反馈后实现代理将沿 shaft 侧接合区域 margin 收至 0.4 物理像素，同时保留 cap 真外缘的自然 AA 排除。1/1.875/4 倍测试横跨 cap/shaft 接合区，严格检查内部 alpha≥250；真正外周仍允许透明 AA。

- File: InkeysHeadlessTests/setting_home_warp_tests.cpp / setting_design_tests.cpp
- Issue: Reviewer 在尚未冻结的中间版本发现开场性能预热未真正进入波纹，以及 footer-only 滚动把像素目标再除倍率的 fixture 问题。
- Fix: 实现代理在首次源码冻结前已将 entrance benchmark 初始 serial 改为 2，让首个 warm frame 相对 disabled 初始化的 serial=1 真正进入；footer scroll 全像素计算并扣实际 clipTop。Reviewer 严格匹配拒绝改写已修代码，未发生构建期间重叠编辑。保留预热后缓存扩容为 0 和真实字标离屏/hero 可见的严格断言。

- File: Inkeys/Inkeys/UI/Setting/Setting.HomeBrand.h
- Issue: 首次真实 WARP 在浅/深主题的 1.875x 与 4x cap 接合带分别发现 3/22 个内部 alpha 缺口。Reviewer 读取 RGBA 证实 4x row504 x527 alpha=0 夹在 x526=19 与 x528=60 之间，且 row505 实体右缘内缩，不能归咎普通外周 AA。只统一首末单点宽度仍让极短末段突变，首末截面法线也存在夹角。
- Fix: 在 k 重合首尾的接合半径内保持同宽，再按弧长 Smooth 过渡回原压感；首末采用共同入射方向，邻近一笔宽范围内的法线平滑归一到共同截面。最后半圆仍复用实体/AA截面顶点；原曲线/彩虹/真正透明外周保留。既有 alpha≥250 与 0.4px 接合侧采样未放宽。最终 .06 版本两主题 1/1.875/4x 的 153/546/2501 个样本全部 gaps=0。4x 原 row504 x527 从 alpha0 变214，旁边 x526=255/x528=109，形成正常向外衰减；row493–496/503–506 无夹在非零之间的零 alpha。原生 GUI 未启动。

- File: Inkeys/Inkeys/UI/Setting/Setting.HomeBrand.h
- Issue: cap 修复后的 .06 波峰版本虽已完整测试通过，实际 dark-late WARP 斜侧仍有明显三角齿纹，像素变化数量不能代替视觉复核。
- Fix: 主会话协调实施将背景/笔画共享 crest 半宽改为 .12，维持原峰值、band .12、66×43 网格和所有严格断言。Reviewer 直接查看最终 dark-late PNG，波峰斜侧明显更柔和，字标和整个横幅仍清晰可见，文字/按钮可读；未新增网格、shader 或纹理。最终再次编译及全套测试通过。

## Findings (not fixed)

- 最终产品与测试未发现尚待修复的本次问题。主会话及 reviewer 均直接检查最新版 WARP dark-late 快照，最终 .12 波峰的斜侧平滑明显改善，接受固定预算取舍；不声称完全消除所有有限网格插值痕迹。
- 本轮未执行旧 cap 实现的 mutation 对照，不声称新增测试已在旧版上确认失败；接缝证据以当前实际接合环带 alpha 采样和 WARP 快照为准。
- Win7 SP1+KB2670838 实机与原生 HWND 最终观感不在本次自动验收范围；本机 FL11.0 无窗口 WARP 不等于 Win7 实机验证。

## 审查证据

- 原 k 回折笔画首尾位置相同但压感宽度不同；新网格统一两端截面宽度。真端点改为轮廓外半圆，弧两端直接复用笔画实体/AA 截面顶点，内部接合不再使用独立全圆抗锯齿边；重合起点跳过重复圆头。曲线、彩虹、透明外周和 y 下伸仍来自原矢量几何，无 PNG/位图遮缝。
- 横幅波带在原渐变/边框之后、字标/正文/按钮之前提交。圆角网格使用圆弧上的边界点及内接三角形，边界 alpha=0，限制在原 8 DIP 圆角内；文本与按钮覆盖在波带上方，可读性和命中位置保持。
- 波带使用 66×43 顶点固定上限网格，尺寸/DPI/中心位置改变才重建；波带动态提交只读预计算径向值。字标共享同一中心、reach 与进度，颜色调制保留 alpha；其归一化椭圆径向值也已改为初始化预计算，动态 Prepare 不逐顶点 hypot。
- 开场进度改为由整个 hero 的实际非空 ClipRect 可见性推进；仅字标离屏时字标几何早退，但背景正常推进；hero 全离屏暂停。原 ShowSerial/真实导航进入、离页取消、现有动画开关/速率与隐藏恢复时钟均沿用；resize/theme/滚动不增加 entries。
- 非鼠标、按住/拖动、弹层/覆盖层/失焦门禁仍只影响视差；字标布局包络、文本和 Fluent hit 不参与透视变换。纯绘制仅返回既有动作，不触发 Shell 或写配置。
- WARP helper 显式 FL11.0、生产 DX11 backend/CSO/字体纹理、真实 RenderHome DrawData、无 HWND 或 swap chain。背景波纹的外界不变判定改为真实圆角 hero 范围；纯视差继续要求 logo 范围外不变。结束及禁用要求像素精确还原。

## Verification

- Lint / 静态格式：PASS。git diff --check 通过；此 C++ 路径无独立 lint 命令。此次产品辅助头、Pages.cpp/.h、CPU/WARP 测试及审查记录均严格 UTF-8 无 BOM、bare LF=0、末尾 CRLF；主会话最终全量 checkout 编码对照另记录。
- TypeCheck / 编译：PASS。主会话 ARM64 原生 MSBuild 完整 InkeysRepo.sln Debug|ARM64，同 invocation PATH workaround。ripple-build-final-result.json ExitCode=0，95.6648653 秒；已有依赖 warning 没有越界清理。
- Tests：PASS。ripple-headless-final-result.json ExitCode=0，223.7218716 秒；stderr 长度 0，stdout 结尾 PASS animation correctness。所有真实三语言/七倍率/浅深/窗口/导航与 tap、Tab/Enter/Space、拖动取消、原主页滚动及开关/速率/真实进入/隐藏恢复/离屏回归通过。
- WARP：显式 FL11.0、生产 backend/CSO/字体/RenderHome DrawData，无 HWND 或 swap chain。冷 AutoResizeY clipped=1/visible=11，entries=1，elapsed=0.183333。浅深六组 cap 的 153/546/2501 个接合带样本全部 gaps=0；实际透明 ink solid=23207、AA=2403、下伸=1314、outside=0。
- 笔画与背景分别通过真实像素验证：浅 ink changed=15537、strong=12281；深 changed=15466、strong=11690；两者 alpha-changes=0。浅背景 changed=48297/strong=38775，深 changed=47896/strong=38824；开场圆角 hero 外 outside=0，结束及关闭精确还原。纯视差四角继续要求字标包络外变化为0。最终 PNG 由原始 RGBA 解码，非 GUI 截图或 AI 合成。
- 性能：五模式各20帧实际预热、100帧采样。静态/idle/moving/returning 为13807顶点、57942索引、6批次；开场为16645/74322/6，仅增加2838顶点/16380索引。所有 ImGui 分配及品牌/波纹缓存扩容=0，idle重算=0；计数不代表整页或驱动零分配。CPU median/p95（微秒）：static 2099.5/3608.8，idle 2455.7/3484.1，moving 3566.1/5986.2，returning 3066.7/4144.9，entrance 3558.6/4615.1。完成 WARP 提交/读回为同轮实测，跨模式波动明显，不能把入口低于静态的耗时解释为动画提升性能，也不能外推可见窗口帧率。
- i18n：本轮没有源文案改动，主会话 check 报告 en-US/zh-TW 各384/384 PASS，无手工生成头改动。
- 首次失败闭环：首轮 --no-window ExitCode=1，158.8467122 秒，仅4项高倍率 cap 断言失败。Reviewer 真实 RGBA 诊断保留，产品共同截面/弧长平滑修复后严格断言未降；.06 中间版本 Exit0/210.2206312 秒保存为 cap-verified 历史证据，最终 .12 以本节日志为准。未执行旧版 cap mutation，不据此声称历史版本测试已失败。
- Spec / 记录：已逐项复核 ripple-implementation-validation.md 的最终像素、五模式数值和失败定位，与原始日志一致。rendering-and-ui.md 仅更新本任务首页合同，共截面/短弧長过渡、明显整 hero 波带、整体可见性、固定预算与 FL11.0 边界均准确；端头 AA 外轮廓描述已限定真端头接合，避免过度宣称其他急转接头完全无内部 AA。spec-review.md 的第三轮手动审计与实际修改一致。
- 范围：未启动交互式 GUI、Computer Use、浏览器或外链，未 commit/push。Win7 实机及原生窗口最终观感仍为人工项。除协作安全点的局部产品/fixture 修正外，reviewer 没有重叠编辑实现源、没有并行构建共享产物。最终无其他本次未修代码问题。
