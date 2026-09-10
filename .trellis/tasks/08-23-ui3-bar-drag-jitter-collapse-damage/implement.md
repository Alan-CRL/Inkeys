# UI3 主栏拖动闪动与缩窄残影修复 - Implementation Plan

## Implementation

1. [x] 在 `Bar.BottomDock.h` 增加可测试的释放交接判定与过期帧屏幕位移解析，保持当前帧路径不变。
2. [x] 在 `Bar.RenderLoop.cpp` 的帧入口阻止位移吸收前的释放态呈现，并在 ULW 锁内使用实际已呈现 translation 解析目的地。
3. [x] 合并 viewport mapping 与 present mapping 的整窗替换判定，同一布尔值控制 damage 和 `prcDirty`。
4. [x] 在 `Bar.Main.cppm` / `Bar.Interaction.cpp` 增加单快照二维命中入口，替换主体与抓手的分轴组合调用。
5. [x] 为 release handoff、stale-frame translation、full replacement 和单快照双轴逆映射补充 Headless 回归测试。

## Validation

1. [x] 运行受影响的 Headless 测试目标。
2. [x] 运行全部 `InkeysHeadlessTests.exe --no-window`。
3. [x] 运行 `git diff --check` 并检查修改范围、编码与 CRLF。
4. [x] 用 ARM64 host `MSBuild.exe` 构建完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，超时至少 5 分钟。
5. [x] 静态复核：没有可见 GUI、没有额外窗口/提交链、没有改变阈值和动画语义。

## Risk And Rollback Points

- 释放门禁位于共享渲染帧入口；若错误返回 Idle 会丢请求，因此只允许 Retry/Continue 并依赖已发布 generation。
- 过期帧目的地必须读取真实已呈现 translation，不能读取未上屏目标 translation。
- 整窗替换可能增加少量瞬态提交面积，但只发生在映射 tuple 改变时，稳定动画帧仍使用局部 dirty。
- 组合命中只改变快照读取次数，不改变映射公式和控件命中范围。

## 2026-09-09 续修计划

1. [x] 对照初始设计、当前 spec 和现有测试，定位三项复现的实际坐标/动画/damage 根因。
2. [x] 在 UI3 Bar 所属层做最小修复，关键步骤补充简短中文注释。
3. [x] 在现有 Headless 测试中增加能复现旧行为失败的组合回归，不只测试公式镜像。
4. [x] 由独立 trellis-check 审查实现与边界，修复确证问题。
5. [x] ARM64 host 完整构建 InkeysRepo.sln Debug | ARM64，构建至少允许 5 分钟；运行全部 --no-window 测试。
6. [x] 复核 git diff --check、文件编码和原换行；更新任务根因与 spec，仅记录无 GUI 验证结论，不 commit。

## 2026-09-09 验证结果

- 独立 trellis-check 已完成全部 Bar/Headless 改动审查，并修正抓取 X/Y 配对发布、首次转换过期帧目的地及 barrier 发布顺序。
- 最终使用 ARM64 host `C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe` 构建完整 `InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64`，退出码 0，耗时 23.78 秒；构建超时配置为 900 秒。主程序与 PptCOM/测试项目均包含在完整 Solution 中。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 退出码 0，耗时 2.13 秒，输出 `PASS animation correctness`。新增用例覆盖持久像素清除、并发 resize 发布/部分失败、同目标隐藏按钮轨迹、1/4 帧快速重捕获，以及普通后续采样赶超首次吸附帧的 X/Y 连续性。
- 日志：`Build/ui3-bottom-dock-validation/msbuild.log` 与 `headless.log`；验证脚本仅对其子进程消除重复 Path/PATH 环境键，不修改系统或项目配置。
- `git diff --check` 通过，源文件 BOM/UTF-8/CRLF 保留；构建产生的无关 `Inkeys/PptCOM.dll` 差异已恢复。
- 未启动可见 GUI，未创建 commit。真实鼠标/触摸果冻、PPT 按钮收起及截图脏区效果待维护者复测，任务保持 in_progress。

## 当前提交与后续工作

用户已确认问题 2、3 修复并明确授权提交当前改动。问题 1 尚未关闭，下一轮继续追踪反复进入/退出底栏时的 Y 跳帧，不因当前构建和 Headless 通过而认定其完成。

## 问题 1 三帧终态闪回计划

1. [x] 沿实际帧顺序查明捕获后下一帧形变为何归零或使用终态坐标。
2. [x] 补充包含生产状态消费顺序的回归，并实施最小修复。
3. [x] 独立 trellis-check 复核，确认不回归已验收的问题 2、3。
4. [x] 完整 ARM64 Solution 构建、全部 --no-window、编码/换行及 diff 检查。
5. [x] 记录根因及验证结果，保留视觉复测状态，不自动 commit。

## 三帧终态闪回续修验证结果

- 基线提交：f7c9b5d5；本轮产品修改仅 5 个 Bar 文件与现有 bar_bottom_dock_tests.cpp。
- 独立 trellis-check 完成写事务配对、短写区、同帧状态所有权、失败捕获重试及旧帧作废需求检查；补充 RequireVisualRetry 防止静止后无请求而漏画。
- 最终 ARM64 host 完整 InkeysRepo.sln Debug | ARM64 构建退出码 0，19.36 秒；沿用 900 秒超时及完整 PptCOM 依赖构建入口。
- 全部 InkeysHeadlessTests.exe --no-window 退出码 0，1.89 秒，PASS animation correctness；包括旧浮动帧误确认新捕获后的微小 X/Y 直移、并发写者、迟到按下/释放、失败/跳帧种子、非恒等恢复与不同显示原点/缩放、无新输入时重试需求，以及先前问题 2、3 的回归。
- 日志仍为 Build/ui3-bottom-dock-validation/msbuild.log 与 headless.log。git diff --check、BOM/UTF-8/CRLF 与修改范围检查通过；构建生成的无关 PptCOM.dll 差异已恢复。
- 未启动可见 GUI，未创建 commit。问题 2、3 的用户验收保持有效；问题 1 等待同一慢速三帧场景的真实视觉复测，任务不归档。

## 运行时取证执行步骤

1. [x] 独立几何审计与主会话时序/日志调查，结果写入 research。
2. [x] 实现临时有界追踪，接入输入、转换、帧推进/作废、直移、ULW 与成功发布点，保持产品行为。
3. [x] 加入必要记录器测试与日志解析/使用说明，确认字段和单位可关联。
4. [x] 独立 trellis-check 审查，ARM64 host 完整构建 InkeysRepo.sln Debug | ARM64 并运行全部 --no-window。
5. [x] 提供测试程序和日志位置，等待用户两组实测数据；不启动 GUI，不 commit，不归档问题 1。

## 临时追踪版交付验证

- 研究已保存 issue1-geometry-audit.md，区分普通底栏基准、实际抓取点、脱离限幅、浮动脉冲高度和松手吸收快照等证据与待实测关系；本轮没有据此做功能修复。
- 新增临时 Bar.BottomDockTrace.h/.cpp；仅 Bar.Interaction.cpp、Bar.RenderLoop.cpp 增加诊断接线，工程/filters/Headless 登记和现有测试同步。Main.cppm、BottomDock.h、PresentDecision.h 与动画文件未改。
- 最终完整 InkeysRepo.sln Debug|ARM64 构建通过，ARM64 host，900秒超时，26.18秒；全部 --no-window 通过，2.58秒并生成 TRACE_FIXTURE。已有问题2/3回归仍通过。
- 已从实际 app CL.command.1.tlog 确认 Interaction、RenderLoop、Trace.cpp 编译启用诊断定义；未运行实际 GUI。
- 原生 JSONL 经 analyze_bar_bottom_dock_trace.py 解析成功，已知成功抓点误差为0；另外验证20/10位移反例、最终ULW位移、未知目标不抹掉抓点数据、实际输入时间和生产快照吸收前后分离。
- 日志自动位于所运行程序目录 log/bar-bottom-dock-trace-*.jsonl，使用步骤见 research/issue1-trace-guide.md。用户结束手势后等待约2秒，提供此次运行全部分段日志；问题1保持开放等待实测。
- 独立检查已修正资源失败记录、数据来源标记、文件尾部限额和刷新错误计数。git diff --check、编码/CRLF及修改范围检查通过；恢复构建生成的无关PptCOM.dll，不提交commit。

## 实机轨迹修复步骤

1. [x] 从冻结日志定位并保存首个独立于输入追赶的几何偏差与跨模式恢复反例。
2. [x] 明确实际抓点与成功形状交接合同，最小修改竖向计算/重基准，保持诊断。
3. [x] 用实测序列做失败前/修复后回归，并独立核对已验收问题2/3。
4. [x] ARM64完整Solution构建、全部无窗口测试、日志解析和编码检查。
5. [x] 交付保留调试输出的修复版，等待用户人工复测，不commit或归档。

## 2026-09-10 实机轨迹修复验证

- 基于原始 run1057671125702 的1280个成功帧与独立抓点公式定位：真实 Down 点未被约束、跨模式速度/形状基准混用、吸收后成功快照坐标不一致。完整证据见 research/issue1-real-trace-geometry.md。
- 功能修改为4个Bar文件与现有bar_bottom_dock_tests.cpp；保留前一轮全部追踪和工程定义，并增加独立的生产求解器/raw/effective/成功高度字段。阈值、频率/阻尼、已验收的水平抓手及居中收短行为保持。
- 新回归复现f138/f354/f776/f1216旧抓点误差并验证新实际抓点；f776按当前输入把成功底端移至1820再收敛到1824；覆盖失败首帧重新播种、速度999.7的脱离/重新捕获、h84释放终态、纯高度恢复中重新抓取再释放、-158/+205位移吸收与source/destination恒等、55px正常窗口追赶及明确正高度保护。
- 独立 trellis-check 完成全范围复核；修正纯高度恢复的release布局锁遗漏和已知/未知高度fixture断言冲突，没有剩余任务内确定缺陷。既有未改代码的类型转换警告未扩大清理。
- ARM64 host完整InkeysRepo.sln Debug|ARM64构建exit0，66.9秒；构建超时900秒并包含PptCOM依赖。全部InkeysHeadlessTests.exe --no-window exit0，3.09秒，PASS animation correctness。
- 新原生fixture run1668117835113经离线解析，独立实际抓点/raw=520px、candidate/effective误差均为0、q=.75、实际高度80正确序列化；旧日志两段仍解析出1280成功帧。解析器另验证55px追赶剔除、有效目标与独立probe分离、缺失值不补0。
- msbuild.log/headless.log在Build/ui3-bottom-dock-validation；原始日志及冻结副本的SHA256均核对未改变。已恢复构建生成的无关Inkeys/PptCOM.dll差异。
- 程序为Build/ARM64/Debug/Inkeys.exe，PE已确认ARM64。未启动GUI、未commit；问题1与诊断保留待用户人工复测，任务保持in_progress。
- 本轮可执行文件SHA256：`44f31dc1a7dec0621805b739bb7fc12e1468044c64ea81751677517ea66664f8`。
