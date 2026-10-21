# Whiteboard v1 验证与交付记录

## 实现范围

已建立父任务及 storage、theme-menu、scene 三个子任务；保留并关联 08-17-ui3-whiteboard 与 08-23-shared-bottom-page-control-windows 历史。实现代理及独立检查代理的记录位于各子任务 research/。

恢复 UI3 白板入口、左下同窗口菜单、每页六色/三纹理主题，复用共享分页 HWND/Scene 动画及窗口组最小化。Draw3 使用进程会话私有的多页 UInk 文档和异步页级存储，离页/离开/退出保存，完整保留已有 CPU undo/redo/Clear 恢复能力；保存失败保留热页并重试。场景协调器保持首次进入前的桌面底栏快照，PPT/白板互切不覆盖，最终回桌面恢复。

完整原始日志保存在仓库忽略目录 `TestResults/whiteboard-v1-2026-10-15/`，下表证据列均相对该目录。

## 已执行验证

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| ARM64 原生 MSBuild，InkeysRepo.sln Debug/ARM64 | 通过，退出码 0 | solution-build-final-ui.log；前序源码编译见 solution-build-verified.log |
| 独立 UInk 测试工程 Debug/ARM64 | 通过，退出码 0 | uink-build-scope.log |
| --uink-page-file-only | 通过，0 failures | uink-page-tests.log |
| --uink-file-only | 沙箱外通过，退出码 0 | uink-tests-unrestricted.log |
| --draw3-whiteboard-storage-test | 通过，退出码 0 | whiteboard-storage-test.log |
| --draw3-parked-desktop-exit-test | 通过，退出码 0 | draw3-parked-desktop-exit-test.log |
| --draw3-parked-ppt-retained-test | 通过，退出码 0 | draw3-parked-ppt-retained-test.log |
| --draw3-control-fence-test | 通过，退出码 0 | draw3-control-fence-test.log |
| InkeysHeadlessTests --no-window | 通过，退出码 0 | headless-final.log |
| --bar-eraser-offscreen-test | 通过，PageControlScene / EraserVisual 均 0 failures | ui3-offscreen-final.log / ui3-offscreen-details.log |
| i18n | en-US、zh-TW 各 364/364 | 子任务独立检查记录 |
| git diff --check / 编码换行 | 通过 | 原有 BOM 保留，工作区 CRLF 与 core.autocrlf=true 一致 |

100 页测试实际执行 worker I/O、冷读、主题与历史恢复、失败写入/索引提交/读取、旧 revision 拒绝卸载，并释放 CPU 笔迹和历史。GPU 使用 128×128 WARP composition swap chain，无 HWND、无 Present；每页实际填充 undo/composition 缓存，先确认资源非零，再调用生产 Discard 路径并确认六项应用缓存引用计数归零。此结论不是驱动显存立即返还系统的证明。

流式文件测试构建 100 个非空页并校验目录不持有笔迹/extra，按页读取、替换中间页、拒绝重复身份/错序/旧修订、识别损坏与安全删除。peakBufferBytes 仅统计序列化工作缓冲高水位，不能作为整个进程 RAM 测量；元数据目录仍随页数增长。

## 环境诊断

- 本机 Codex MSBuild 环境在同一个 PowerShell invocation 规范化 PATH 并关闭 node reuse，使用 Visual Studio 动态定位的 ARM64 原生 MSBuild。未改 SDK、依赖或项目构建方式。
- 初轮并行模块扫描出现 CL exit 4，串行 MSBuild/CL 参数可完成；参数仅用于本轮命令，未写入工程配置。
- 首轮链接目标输出短暂无法打开（LNK1104），确认无运行的 Inkeys 且输出路径可创建后重试链接成功。
- 沙箱内既有 UInk 替换文件断言失败；独立 Win32 ReplaceFileW 探针同样 error 5。经工具批准在沙箱外运行同一无窗口测试后完整通过，未修改文件保存源码来规避权限。

## 人工验收及已知边界

未启动 GUI、Computer Use、真实 PPT 或任务栏窗口。连续快速进入/退出、真实笔与触摸取消菜单、任务栏最小化恢复、跨显示器/DPI、PPT后台结束和最终视觉动画仍属于人工验收项。

失败的索引提交会保留候选 .uink/current-N.tmp 以避免覆盖已提交版本；持续故障可能留下额外磁盘文件，尚无失败候选清理策略。正常提交仅保留当前及一份备份，不清理旧进程会话。

不支持平移、缩放、页面管理、手动导出、历史画布 UI、跨进程自动恢复。用户已授权提交当前改动，未 push；本次提交不结束任务。三个子任务标记 completed，父任务保持 in_progress 以保留人工验收清单，不执行会自动提交的归档流程。

## 截图反馈修复轮（2026-10-15）

已修复入口 2×2、分页标签主文字槽、独立上方菜单和右侧主题矩形、70×70 预览中心勾、共享 Back 动画及收起负尺寸、居中空间换向、最小化主栏/分页状态、PPT 最小化禁止、Inkeys 国际化标题与 exe 图标。独立检查同时修复外部最小化 veto 和最小化后关闭留下 iconic 锚点。

- ARM64 原生 MSBuild / InkeysRepo.sln Debug|ARM64：非增量链接通过，退出 0（revision-build-nonincremental.log）。此前增量链接器 IncrBuildImage 内部错误 LNK1000，结束已失败进程后仅命令传入 LinkIncremental=false；未改工具链或项目设置。最终仅 LNK4075 EditAndContinue 与非增量链接不兼容提示。
- 主程序 --bar-eraser-offscreen-test：显式等待退出码 0，PageControlScene failures=0，EraserVisual failures=0；包含标签像素、独立面板命中、多个倍率、关闭和反向展开中间帧。详见 revision-offscreen-stderr.log 与 Build/eraser-b/offscreen-results.log。
- 无窗口离屏截图 TestResults/whiteboard-menu-revision.png 已查看，确认独立面板、标签、70 DIP 方形预览和中央勾。不是 GUI 截图或动画体验验收。
- 三语 i18n 368/368，diff whitespace、原源文件 BOM/CRLF 检查通过。
- 真实 HWND 的窗口组测试仅编译，不运行；任务栏图标、owned-popup 最小化恢复、PPT/Win+D、跨屏与动画手感仍需人工验收。现有局部 Window Service 测试无法覆盖进程全局 WndProc 路由，该测试边界已记录。
- 本轮不提交、不 push、不结束或归档父任务。
- 最终构建产物 InkeysHeadlessTests --no-window：退出 0（revision-headless-final.log），主栏/分页/场景及动画回归通过。

## UI3 统一实施轮（2026-10-15，当前结果）

本轮直接实施最新授权：菜单操作140×32.5 DIP、More居中、无箭头、所有间隔5 DIP；背景颜色/背景样式分组与共享More分割线；完整2×2按钮及内部标签、30 DIP原colorSelect SVG、中心对称纹理。共享Scene复用现有属性motion/父子pose/FrameAnimationClock，动态呈现crop与命中/光源/脏区同帧。纯内容更新不重开布局，预览复用设备缓存。

卡顿相关的代码证据和修复：移除业务窗口与逐控件弹性叠加；修复无图标opacity1→0、隐藏内容opacity0→0.3每帧目标振荡；内容更新不再重开布局与输入锁；关闭不再等待5秒hover淡出。没有测真实GUI帧率，不能将这些静态及离屏证据等同于实际FPS改善验收。

| 验证 | 结果 | 证据（TestResults/whiteboard-v1-2026-10-15/ 下） |
| --- | --- | --- |
| ARM64原生MSBuild完整 InkeysRepo.sln Debug/ARM64 | 通过，最终退出0；3个既有hashlib++警告 | ui3-unify-build-checked.log；完整依赖重编见ui3-unify-build-verified.log |
| InkeysHeadlessTests --no-window 最终产物 | 通过，退出0，橡皮216布局及撤回动画均零失败 | ui3-unify-headless-final.log |
| 主程序 --bar-eraser-offscreen-test | 通过，显式等待进程退出0；PageControlScene和EraserVisual零失败 | ui3-unify-offscreen-checked-stderr.log、Build/eraser-b/offscreen-results.log |
| i18n sync/check | en-US及zh-TW各373/373 | 本轮工具输出 |
| 原文件BOM/CRLF及git diff --check | 通过 | 工作区格式审计 |

新增生产Scene测试覆盖：冷PPT转白板复用实例、父子同时展开、0.5/1/2倍动画速度、0.75/1.5倍DPI、负原点、逐帧对照共享motion、同帧Advance/Render幂等、viewport包含当前内容、输入坐标、途中换色不重启、禁用动画立即收起、真实绿色SVG尺寸、按钮内标签、纹理镜像像素、纯白背景可读标签。

首轮离屏失败分为两类，均保留严格检查修复后通过：174项参考时间比较缺少冷启动已稳定前置条件；12项面板/分割线实际缺失来自hover清理重置chrome透明度，已调整执行顺序。实际动画曲线及像素阈值未为通过测试而降低要求。

环境：系统Python alias不可执行，使用已有bundled Python；git子模块读权限限制使用ignoreSubmodules只审查本工作区。沙箱内C1902用同一最小C++探针独立复现，批准沙箱外同一探针退出0；完整构建因此在沙箱外复核。配置文件写入测试同样在沙箱外运行相同--no-window通过。未更改SDK/工具链/工程配置。命令行沿用非增量链接避免前轮LNK1000。

没有启动GUI/Computer Use/真实PPT，任务栏、跨屏及真实动画手感仍需人工验收。本轮未commit、未push，父任务保持in_progress，不结束或归档。

## 统一 UI3 按钮计算、交互与多目标渲染（2026-10-15—16，最新工作）

本节取代前一轮共享 Scene 方案的当前结论，保留旧记录作为历史。实际 Main/MainMore、分页列表与固定属性已接公共按钮/面板函数；旧动态 Scene 控件构建、独立 popup clock/插值/命中及伪主栏 renderer owner 已移除。产品输入统一使用原主栏 worker，续接手势与有序异步业务分离；确认对话框可取消并接入退出排空。任务保持 in_progress，不结束、归档、commit 或 push。

本轮日志目录：`TestResults/ui3-shared-runtime/`。独立检查报告：`research/ui3-shared-runtime-check.md`；功能通过、性能预算未通过，未标总体验收完成。

| 已执行项目 | 实际结果与证据 |
| --- | --- |
| ARM64 原生 MSBuild 完整 solution Debug/ARM64 | build14 退出0，最终公共渲染实现恢复到build12已测版本；build-integration-14.log / final-build-manifest.json |
| 生产共享输入 worker | build14 --ui3-input-worker-test 退出0，生产 mailbox/dispatcher/continuation/deadline/cancel；input-integration-14-stderr.log |
| 实际 D2D 离屏像素/命中 | build14 --bar-eraser-offscreen-test 退出0，PageControlUI3 和 EraserVisual 均0 failures，9组BGRA严格相等；offscreen-integration-14-stderr.log / offscreen-integration-14-results.log |
| 最终 Headless | build14 InkeysHeadlessTests --no-window 退出0，橡皮216布局、Recall及动画通过；headless-integration-14-stdout.log |
| 场景与存储回归 | build6 四项 Draw3 CLI 均退出0：100页白板存储/冷页历史/主题/CPU与GPU缓存引用释放、parked-desktop-exit、parked-ppt-retained、control-fence；对应 *-integration-6 日志。本轮后续未改这些业务实现 |
| Trellis context / 格式 | implement/check各12项 validate通过（既有大文件注入截断警告，代理手动读取相关完整章节）；产品文件UTF-8/BOM/CRLF与diff whitespace检查通过，父prd历史混合换行保留 |

性能验收进行中，不能使用旧轮通过记录替代本轮结果。第一份基准把被生产布局覆盖的按钮坐标目标当作活动，回调数600但主栏提交仅约100，保留 baseline-invalid-activity/，不作为验收。后续虽严格达到600真实提交，独立检查发现生产首帧浮动启动放置覆写fold，使Main/PPT只测到折叠主按钮；隐藏Geometry按压仍能造成空脏区兜底全绘，所以真实提交数量本身不足以证明负载。原27组及build11九组均保留 baseline-folded-full/ 和 baseline-folded-partial/，不再宣称有效展开主栏基线或改善比例。

额外严格回归：3倍率×3设备DPI共9组，真实共享按钮/文字/图形/变换/光效的局部重绘与完整重绘逐BGRA像素相等；未知custom内容、无裁剪和有限SVG探针保守绘制。页码/主题增量更新逐项验证全部17按钮与窗口包络不跳变，再逐20ms批次验证原布局动画继续。曾有一条新测试把Cubic起步小于1像素的取整平台误判为停止，保留严格几何断言并使用5个真实20ms批次修正，未改生产曲线或放宽像素对照。

build12修正启动fixture的完整27组已保留 baseline-expanded/（含exe SHA256与aggregate.json）：每组600实际展开/可见主栏提交，PPT四分页、白板两分页各600，合计16200主栏、48600全部提交，零掉样、三次Idle观测均零额外回调，raw复算量化与summary一致。23组超预算；100%主栏批次P95为13.19–13.48ms，PPT17.77–18.57ms，白板18.89–21.37ms；200%主栏18.38–20.51ms，PPT23.24–25.94ms，白板23.48–25.17ms。仅记录为真实展开路径基线，不将性能标通过。

该基线慢段主要落在GetDC/flush：100%PPT按每批排他阶段合计的P95，计算4.19–4.43ms、Draw2.14–2.24ms、GetDC10.45–11.01ms；不同阶段分位数不能相加当作批次分位数。PPT单个分页计算仅0.26–0.36ms，远光裁剪原已存在，因此不增加隐藏状态/字宽缓存来猜测主要原因。进一步尝试A8柔光九宫格片段级剔除：build13构建、九组跨dirty父面板/独立形状/整按钮分支及BGRA一致验证均通过，但21组候选采样未证明时间收益，并出现计算/Draw/GetDC整段波动。没有将全段波动直接归因这2行候选；按最小diff原则撤回该优化和其专用断言/spec，保留原公共Shape/Word/标准按钮剔除。候选的source、CSV、exe SHA与逐字节恢复hash在 candidate-mask-slices-partial/，不冒充最终通过结果。build14产品渲染代码恢复到build12完整27组所测实现，最终基准代码只多输出Scheduler.Stop后的mask_slices列，不改变活动负载；因此不重复同一生产实现的27组，完整数据对应build12，最终编译/功能确认对应build14。

最终采用实现的完整性能数据（build12，三轮范围，毫秒；目标P95≤16.67/P99≤33.4）：

| 场景 | 倍率 | batch P95 | batch P99 |
| --- | --- | --- | --- |
| 展开主栏 | 100% | 13.19–13.48 | 17.86–20.85 |
| 主栏+四PPT分页 | 100% | 17.77–18.57 | 22.94–26.50 |
| 主栏+两白板分页/菜单主题 | 100% | 18.89–21.37 | 24.36–29.58 |
| 展开主栏 | 150% | 15.98–26.91 | 21.41–40.19 |
| 主栏+四PPT分页 | 150% | 23.08–30.32 | 29.77–70.82 |
| 主栏+两白板分页/菜单主题 | 150% | 22.85–25.84 | 27.60–35.40 |
| 展开主栏 | 200% | 18.38–20.51 | 20.16–29.49 |
| 主栏+四PPT分页 | 200% | 23.24–25.94 | 28.90–60.89 |
| 主栏+两白板分页/菜单主题 | 200% | 23.48–25.17 | 25.43–30.14 |

主栏成功提交间隔P95约33.89–39.39ms，100%主栏单独运行仍约37.04–37.68ms，不能把某组batch达标称为60fps。共有45个batch超过50ms，43个最大排他阶段为GetDC，1个EndDraw、1个计算；最慢293.35ms，GetDC183.29ms。共有69个主栏提交间隔超过50ms。GetDC会flush先前D2D工作，不能把这些等待单独归因到某一个API实现或缓存重建；来源尚未完全收敛。

动画中确有829次新圆角父遮罩创建，不能报为热态零分配，也没有证据支持每帧重建。固定几何/epoch的独立真实事务回归验证context/bitmap/遮罩复用、重复热帧创建计数不增长。当前尚未对所有字体/SVG/CPU分配做完整分配器跟踪，不能宣称全部热态资源零创建。

当前结论：共享UI实现与功能/像素验证已落地，软件渲染预算尚未通过；父任务保持in_progress、implementation_complete=false。剩余项为GetDC/完整事务长帧与提交节奏的性能收敛，以及原生ULW/DWM/任务栏/真实PPT人工验收；不借未执行GUI验证替代软件预算结果。

构建环境：使用同一PowerShell invocation规范化PATH，动态定位ARM64原生MSBuild，完整InkeysRepo.sln Debug/ARM64，命令使用非增量链接。沙箱C1902由同一最小CL探针复现且批准沙箱外通过，未改SDK/工具链。vcpkg找不到pwsh的日志随后按其既有规则回退系统WindowsPowerShell成功；未出现applocal失败警告。已知hashlib转换及触摸LONG转short警告保留，未顺手清理。

没有启动GUI/Computer Use/真实PPT。无窗口输入基准经过生产mailbox/dispatcher与公共按钮反馈，主栏使用标准反馈适配器，分页走真实成功呈现命中和ApplyPointer；复杂手势由独立生产Pump回归覆盖。原生WndProc/驱动端到端、ULW/DWM、跨屏、任务栏及视觉手感仍需人工验收。

## 用户本轮体验验收与提交授权

用户明确反馈“验收完成，比之前好得多”，授权仅为当前改动创建commit，并要求白板任务不结束，后续继续修改。本轮体验验收记为用户确认通过；此前自动性能预算未通过的原始数据与代理未执行GUI的事实均保留。父任务继续in_progress，不归档、不push。

## 当前菜单层级与主题预览修订（2026-10-16）

在用户上轮体验验收与commit 92c3c8b1之后继续本轮三项UI小改，父任务保持in_progress。绘制顺序改为主题→更多→分页栏，三个输入入口共用与绘制一致的前层面板判定，前层空白阻挡后层按钮；色板显示与点击共享重排后的六色常量；样式纹理覆盖完整70×70按钮，24 DIP中心相位产生九点/九交点，标签仍在标准槽位。未新增动画、缓存或渲染路径，未修改画布密度与存储。

| 验证 | 结果与证据（TestResults/ui3-shared-runtime/） |
| --- | --- |
| ARM64原生MSBuild完整solution Debug/ARM64 | 最终退出0，preview-revision-final-build-2.log；同PowerShell规范PATH，动态定位原生ARM64 MSBuild |
| 生产公共D2D离屏回归 | --bar-eraser-offscreen-test退出0，PageControlUI3/EraserVisual均0 failures；preview-revision-final-offscreen-{stderr,results}.log |
| 本轮新增回归 | 六色色块实际像素/六项业务点击映射、整按钮九点及九交点/标签下方纹理、45个真实展开帧的分页>菜单、菜单>主题及三层遮挡；现有DPI/反转/设备重建/失败提交继续通过 |
| Trellis上下文 | implement/check各13项validate通过；原大型规范注入截断提示保留，当前白板规范完整加入上下文 |
| 独立检查与格式 | research/ui3-preview-revision-check.md；产品UTF-8/CRLF和git diff --check通过，父PRD历史混合换行保留 |

首轮35个新增测试失败来自两处fixture问题：size_t参与负数色块坐标运算造成下溢（15项），以及三层重叠时误把菜单当作前景（20项）。保留严格颜色阈值及命中要求后修复坐标类型，增加独立分页圆角区域判据，强制三类重叠样本均存在。中途测试曾引用未export的内部Bar命中类型导致编译失败，改用测试内圆角判据，不扩大产品模块导出。最终构建/离屏结果均通过。三条既有hashlib转换warning未清理。

本轮没有运行GUI/真实PPT，也没有重跑多目标性能基准；上述结果用于这三项UI调整，不改变上一轮性能预算尚未达标的记录。没有新建/结束/归档任务，没有commit或push。

### 本轮人工验收与提交授权
用户确认上述菜单层级、颜色顺序与预览调整人工核验通过，并授权提交当前改动。白板父任务继续保持in_progress，不结束、不归档、不push。既有自动性能预算记录保持原结论。
