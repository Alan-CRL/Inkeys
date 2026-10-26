# 第五轮主会话验收：并行书写、Hold与曲线归位

2026-10-11，沿用 fluent 工作区和10-10-setting-home-design子任务。最新用户批准的修改已实施并完成本轮自动化验收；任务保持in_progress，不结束/归档，没有新增commit/push，HEAD和索引保持a3b22667基线。

## 实现与精确时序

- 现有透明矢量、六笔原曲线与彩虹配色一次缓存；按笔画时间提交前缀和共用截面的活动半圆端帽，写完直接使用原完整网格。已确认的k接合修复保留。
- 速率1：120ms稳定等待→530ms六笔并行书写→300ms完整居中静止、覆盖alpha1→270ms背景渐退和三次Bezier空间曲线同时归位→110ms单次1.5DIP/小于1%的落定。总长约1.33s；.95s之前不渐退或位移。
- 只消费真正false→true的ShowSerial；内部切页返回、重复显示/激活/主页点击、尺寸/主题/滚动不重播。HideSerial在渲染线程布局前消费，Home导航及外层Scroll/ScrollTarget/拖动storage归零，快速Hide→Show没有中间渲染帧也在首布局顶部。
- 关闭弹层需非空guard，复位清旧子页焦点并重建设置root的Focus/NavInit；键盘Tab入口不依赖窗口重新Appearing。输入检查包括Show前已按住的键，取消当帧保持屏障。无输入帧保留正常Fluent配色。
- 根据真实font/i18n/DPI/倍率/导航/外层clip检查完整横幅、起始/目标/曲线包络，17-float实际布局键缓存。尺寸不足永久跳过本Show，放大不补播，中途缩小收束。覆盖只占32DIP标题栏以下客户内容区；单份前景字标，布局及命中不运动。
- 原弱反光/水波调用不再播放。既有视差/一次按钮反馈在归位后接管；复用原ImDrawList/DX11帧循环，没有新增shader、离屏纹理、device、线程、timer、配置和文案。

## 构建与完整无窗口回归

- 最终完整InkeysRepo.sln Debug|ARM64：ARM64原生MSBuild动态定位，PATH workaround同一PowerShell invocation，1800s允许时长，**退出0，45.874s，0 error**。18 warnings均为未修改的HiMsg/hashlib++路径，本轮Setting/测试源无warning。
- InkeysHeadlessTests.exe --no-window：**退出0，246.169s，stderr0字节，PASS animation correctness**；未创建交互式GUI窗口或使用Computer Use。
- 三语言真实文案/字体、浅深、导航展开/收起、720×520/960×700/1440×900及320/480窄场景，沿用1/1.25/1.5/1.875/2/3/4倍率的420个实际布局组合，按实际容纳分别播放或静态：224播放，196跳过。每种倍率均覆盖两类。
- 原许可/仓库/Star/赞助tap、真实Tab/Enter/Space、按下拖动取消与旧主页滚动/控件回归保留。新生命周期包括Show/重复Show/内部返回、Hide无中间帧/旧滚动/其他页/实际弹层、键保持/释放/取消、尺寸跳过与缩小、动画开关/速率/零delta。
- 快速重开键盘回归在正常ImGui非扁平child机制下，用首个真实Tab到主页content child、原生Enter进入，再真实Tab到Star；不伪造NavActivateId，不借助Ctrl+Tab恢复。

## WARP实像素

明确请求Windows11 ARM64 WARP FL11.0，复用生产CSO/backend和实际RenderHome/Navigation/EntryOverlay DrawData，1000×1100离屏RTV，不创建HWND。

- cold AutoResize第一帧内层字标clip空，但真实外层有效尺寸使前景一次入场，不消费额外时间、不先闪静态页面。
- .13/.38/.65/.8/.949/1.08/1.27s浅深快照；.65/.8/.949完整Hold全帧RGBA相同，覆盖alpha1且原地。之后实际曲线路径偏离直线并同时渐退，标题栏之外才变化；底层文字几何/UV/RGBA、按钮命中矩形保持。
- 从真实前景提取六笔未完成索引和七个有界活动半圆，不提交未来截面/未到达末帽；透明清屏下六个活动cap/shaft在1/1.875/4倍分别采331/1387/6854个实体像素，浅深各一次，缺口全部0。
- 原k闭合接合带在1/1.875/4倍采153/546/2501个实体像素，浅深缺口全部0；最终完整透明AA/y下伸保留，归位结束和禁用后的整页RGBA与静态精确一致。视差只有字标周围像素改变，外部变化0。

## 同机性能

每模式20帧预热+100帧测量；单位ms。CPU包括生产页/导航/字标提交几何；完成提交包括WARP和GPU copy/Map等待，并非纯GPU耗时或原生窗口帧率。

| 模式 | CPU median | CPU p95 | 完成提交 median | 完成提交 p95 | 顶点 | 索引 | 批次 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| static | 2.642 | 4.144 | 30.626 | 59.744 | 14007 | 58608 | 10 |
| idle | 2.487 | 3.238 | 31.181 | 43.213 | 14007 | 58608 | 10 |
| moving | 3.350 | 4.497 | 31.824 | 61.232 | 14007 | 58608 | 10 |
| returning | 3.241 | 4.389 | 30.782 | 43.176 | 14007 | 58608 | 10 |
| writing | 2.878 | 3.846 | 37.136 | 43.496 | 14172 | 10572 | 11 |
| hold | 2.665 | 3.075 | 38.559 | 44.084 | 14011 | 58614 | 11 |
| dock | 2.495 | 2.924 | 37.210 | 40.531 | 14011 | 58614 | 11 |
| settle | 2.520 | 2.959 | 29.486 | 32.019 | 14007 | 58608 | 11 |

全部模式预热后ImGui分配=0、字标缓存增长=0，原曲线/截面/索引块capacity固定；idle/writing/Hold/Dock/settle字标视差缓存重建=0。实际moving/returning按原姿态改变需要重算原缓存。统计不覆盖既有std::string/vector或驱动分配，不能声称整页零分配。入场最多加165顶点和一个前景批次，书写前缀索引减少；Hold/Dock只多4个覆盖矩形顶点。稳定终态恢复原预算。

覆盖层有实际WARP填充成本，完整提交的median及p95存在明显等待/调度波动，未承诺60FPS或Win7实机速度。

## 首次失败/审查修正与边界

- 首次受限runner构建78.554s退出1，C1902 PDB manager mismatch；与既有任务的环境问题一致，安装的CL/mspdb服务版本匹配。相同solution/工具链在普通用户环境122.184s退出0；没有改SDK、项目或源码绕环境。
- 独立审查修复键保持遗漏、空弹层关闭断言，以及仅清Focus导致无缺帧重开Tab不可达。发现最后一项时主动中断第一次headless（86.536s，退出-1；不计PASS），修复后重建并完整重测，以本文件最上方最终结果为准。
- 规范同步只改rendering-and-ui.md末章；trellis-update-spec技能未安装，按已读取workflow Phase3.3手工同步，见brand-entry-spec-review.md。独立检查见brand-entry-check-review.md。
- i18n check为384/384 PASS，无新增文案不做无效sync；最终UTF8/BOM/CRLF/末尾换行与diff/HEAD/index审计见ignored encoding-audit-brand-entry.json。
- 本轮没有交互式原生设置窗口/Win7实机测试；完整Setting.cpp常驻协程接线做静态审查，headless生命周期通过共用真实helper验证，不能冒充HWND显隐闭环实机证据。Win7 SP1+KB2670838仅静态沿用原FL11.0 API及兼容管线，实机和原生最终观感仍待人工。

## 可审阅产物

- 日志：Inkeys/Cache/home-design-20261010/build-brand-entry-verified.log、headless-brand-entry-verified.stdout/.stderr及对应-result.json。
- 实际WARP像素：同目录entry-light/dark-130/380/650/800/949/1080/1270.rgba，主会话无损解码PNG；属于离屏测试快照，非交互窗口截图。
- 规划、实施及独立审查记录保留在当前子任务，用户后续可以继续调整；没有finish/archive或Git提交操作。
