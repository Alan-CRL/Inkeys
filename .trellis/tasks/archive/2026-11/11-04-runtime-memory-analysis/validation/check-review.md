# 首批内存优化质量审查

日期：2026-11-05。角色：独立 Trellis check。状态：最终全范围审查通过。已复核主会话构建、回归和前后采样的现有日志；本审查没有启动程序、GUI、构建或修改系统设置。

## Findings (fixed)

无。未发现需要自修的代码问题，没有改动产品源码。

## Findings (not fixed)

无未修复的代码问题。首次 UInk 文件回归的两处目录 lease 断言失败与 WPR 权限限制按下文保留，没有通过修改产品代码、断言、工具链或系统配置来规避。

## 静态证据

- 审查覆盖本轮全部六个产品源文件，共14行新增、15行删除；无工程、依赖、shader、模块接口或公共格式改动。
- 独立解析 `Inkeys/Inkeys.vcxproj` 并扫描220个现存 `ClCompile` 源文件。`alpha_drawpad`、`tester`、`pptdrawpad` 的生产命中仅剩 `Inkeys/IdtImage.cpp:5/7/8` 三个默认空构造。全仓额外使用只有 `IdtDrawpad.cpp:2205` 的clear，该TU在工程 `:951` 明确为None；头文件仅为extern声明。
- `Surface.Core.hpp:19` 默认构造不分配，宽高/句柄/像素成员均为零或空。因此这是停用无消费者的主动分配，不是把分配推迟到首次输入/显示。
- `IdtMain.cpp` 相对HEAD的28处 `Inkeys::Startup::Milestone` 引用及顺序完全一致；保留 `LegacySurfaceReady`、DPI阶段日志及相邻启动顺序。
- `uink_draw3_export.cpp:355` 的const引用指向局部const `canvases` 的元素；该owner覆盖整个循环，循环中不修改/扩容或移动它。所有sourceCanvas/stroke/clear访问为只读，输出仍复制到独立UInkDocument，未借用快照内存。
- `Draw3.AutoSave.cpp:704` 的imported为值返回结果；校验后移动其 `optional<Draw3UInkExportSnapshot>` 所拥有的值到snapshot，之后不再读取imported。sourceRevision仍从独立的read结果读取。
- `Draw3.PresentationAutoSave.cpp:1587` 的imported在身份校验后移动到projected，后续Current/PreviousInterval投影只使用projected、request、found和completion，不再访问移动来源。active/retained/legacy及区间投影分支保持原样。
- `Draw3.DrawingController.cpp:1697` 的captureCanvas返回独占optional；移动进activeCanvases后来源只析构，运行document/runtimes并未移动。`snapshot.canvases = snapshot.activeCanvases` 和retained投影完整保留。
- 三个std::move调用所在TU均已有 `<utility>`；对应snapshot结构按值拥有vector/optional/string，没有新增借用指针或跨线程生命周期依赖。
- 六个文件均与HEAD保持原UTF-8/BOM/CRLF，未混入LF，也没有无关整文件格式化。
- 最终复核时六个源码SHA-256与初次静态审查记录完全一致，记录见`check-source-sha256.json`；构建/回归后没有未经复核的产品改动。

## 规格同步

已审查 `.trellis/spec/native-desktop/errors-logging-and-resources.md` 新增的“内存优化的既有所有权边界”小节。它准确记录本次legacy空占位、独占snapshot转移、编译项/消费者审查和指标不可重复相加，并保留用户要求的设备/层架构及热路径边界；没有擅自扩大行为或兼容范围。原规格的UTF-8/BOM状态与CRLF保持，diff仅新增七行。

## 架构与流畅度（按用户要求静态确认）

本diff没有改变L0/L1/L2、呈现合成、GPU缓存、输入/渲染线程或调度。UI仍使用RenderPipeline的共享WARP device；Draw3仍独立创建hardware-first设备并保留原fallback。未增加或移动任何纹理创建、解码、同步等待到首笔、Undo、切页、显示或绘制热路径。三个死DIB的分配被取消，其余改动减少现有copy工作；没有引入首次使用延迟初始化。以上是静态结论，不是动态流畅度测试声明。

## Verification

- Lint：通过。`git diff --check --ignore-submodules=all` 退出0；消费者扫描、完整里程碑顺序和编码/换行对照通过。
- TypeCheck / 产品构建：通过主会话证据。`build-after.result.json` 显示ARM64原生MSBuild、`Debug|ARM64`、exitCode=0；`build-after.stdout.log` 前段证明六个修改TU均重新编译，末段证明Inkeys、PptCOM、PptCOM.Tests和InkeysHeadlessTests产物生成。既有warning未据此扩大修改。
- TypeCheck / 共享UInk构建：`build-uink.result.json` 记录 `inkStrokeModelerTest.sln Debug|ARM64` exitCode=0；stdout明确包含inkPixelShader、inkVertexShader、laserParticleUpdateCS、laserParticleEmitCS四个shader编译成功，以及共享export TU和独立测试编译/产物生成。
- Tests：适用回归最终全部通过。`test-results.json` 的10项中9项首次退出0；覆盖UInk presentation、Desktop退出保存、PPT retained保存/安装、白板存储、device policy、WARP及hardware像素、startup progress。UInk文件项的首次失败与重跑保留在下一节。
- History：`after-history.summary.json` exitCode=0、无timeout/forced cleanup。独立解析前后stdout，66条measured scenario/block的digest、strokes、stored/retained/visible/redo、eraser、undo/composition/visible tiles及pieces全部一致；未把计时波动当作流畅度验收。
- 采样：前后EXE/PDB身份记录均为ARM64且GUID/Age匹配。四次正常启动样本均约60s、无timeout/forced cleanup，以WM_CLOSE收尾并均退出6；它们是启动/空闲内存采样，不是exit0正确性测试。

## 首次失败与环境记录

首次 `uink-file` 退出1，原始stderr保留 `UInk FAILED line 2134: leased` 和 `line 2603: acquired`；其余page-file子项已报告0 failures。主会话定位为既有测试所需 `TestResults/release-hardening` 基目录缺失，目录lease CreateFile分别得到Win32 2/3。仅创建要求的空目录后使用同一测试重跑，`uink-file-retry.result.json` exitCode=0，stdout明确报告 `All UInk persistence tests passed` 和page-file 0 failures。没有改源码、配置或断言来通过测试；首次失败历史未删除。

WPR实际开始VirtualAllocation录制时返回0x80070005：普通Windows令牌非管理员，`wpr-before.result.json` 明确 `recorded=false`。现有status日志为not recording，本轮无ETL/堆栈归因成果，不把WPR帮助查询或API计数器称为完整heap profile；没有改变系统诊断设置。

## 采样数字的复核范围

正常启动尾部20个样本约5s的Private Bytes中位数：before两次570.79296875/571.7265625 MiB，after两次510.609375/509.71484375 MiB；已从CSV重新计算并与summary一致。两次均值约571.260→510.162 MiB，减少约61.098 MiB（10.70%）。GDI对象38→32，与三张DIB各移除HDC/HBITMAP的源码变化相符。

这是本机2880×1920、同配置ARM64 Debug隔离进程的启动/空闲观测。没有将Private Bytes与Working Set或纹理名义载荷相加；没有单独量化四处副本移除在大文稿加载/保存中的峰值收益，也没有外推Release、实际Office交互或全部用户场景。

## 现有回归入口与规格同步建议

- 产品：`--draw3-history-benchmark`、`--draw3-parked-desktop-exit-test`、`--draw3-parked-ppt-retained-test`、`--draw3-loaded-retained-install-test`；按主会话选择适当Desktop/PPT保存恢复及离屏验证。
- 共享UInk：`inkStrokeModelerTestTests.exe --uink-file-only` 和 `--uink-presentation-only` 在 `contact_input_tests.cpp:3465–3471` 的GPU测试前早退。共享TU的独立构建沿用 `inkStrokeModelerTest.sln Debug|ARM64`。不需要为纯std::move写镜像式新测试。
- 建议主会话在现有资源规格中简短记录：生产Draw3迁移后无消费者的legacy全屏DIB保持空占位；资源分配审查以工程编译项/真实消费者为准；需要转移独占值时避免const令move退化为copy。没有公开UInk schema或渲染架构规格需因本diff改变。

## 最终结论

本轮六个源码与一处资源规格的完整变更通过审查；没有需要自修的代码问题。主产品及共享Solution构建、适用回归、历史语义一致性、格式与规格检查均有对应证据。架构和流畅度按用户要求作静态确认；未执行动态流畅度验收。高风险的生命周期/缓存/历史语义优化仍未实施，不影响本轮已批准低风险范围的质量结论。
