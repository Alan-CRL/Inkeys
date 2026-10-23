# P1 独立审查与自修复

2026-10-23；只审查 `VisualMotionTracker.h/.cpp`、`visual_motion_tracker_tests.cpp` 及 `animation_tests.cpp` 的短入口。基于任务批准的 feature/move 980dc39b；未审查 P2/P3 窗口、Capture、Business、GUI。初期按 main 要求只读；P1 写入方释放后才修改归属文件，无并行构建或回滚他人改动，无 commit/push。

## Findings (fixed)

- File: `Inkeys/Inkeys/CV/VisualMotionTracker.h`
  - Issue: 诊断类型顶部原注释把所有坐标描述为处理图像像素，但 `anchor`、`total`、`delta` 原本就是原图/内容坐标；按旧注释消费会发生二次缩放。
  - Fix: 已向 main 报告，由实施方将注释改为明确区分点/flow/bounds 与 anchor/total/delta。审查确认改后与实际 Refill/出生锚点计算一致。
- File: `Inkeys/Inkeys/CV/VisualMotionTracker.h/.cpp`、`InkeysHeadlessTests/visual_motion_tracker_tests.cpp`
  - Issue: 零主模式歧义检查只公布 interior>=6 的数值、次流 originalCellRatio 及 zeroRatio 布尔，缺少生产比较使用的 `.15` 数值门限，后续显示端只能再写一个字面值。
  - Fix: 新增唯一生产常量 `ZeroModeRatioThreshold = 0.15`；原 `otherRatio > .15` 在相同短路位置使用它，BeginDebug 原值复制到 `snapshot.zeroModeRatioThreshold`。扩展原 sparse-minority 短用例，核对该值、zeroRatio 的实际执行与拒绝候选比例；没有新增算法负例或改变阈值。

## Findings (not fixed)

没有剩余已确认的 P1 代码缺陷。以下是交接与验证范围，不能视为已经完成的 P2/P3/P4 验收：

- `.trellis/spec/native-desktop/scroll-follow-tracking.md` 尚需由 main 随最终功能规范同步默认空的快照参数、阶段/检查有效性、坐标单位和关闭路径约束。本 reviewer 不跨归属修改最终规范。
- Forward 低点数、JumpTooLarge、AnchorDisagreement、OpenCV/其它异常的专门负例没有被短矩阵强制触发；只静态核对原代码路径与异常转换，遵 main 要求不扩展算法负例夹具。
- 完整产品编译、实际 Mag 排除、GUI 与性能不属于本次 P1 定向验证；本报告不将 headless 通过外推为这些项目通过。

## 实际代码路径审查

- 对照 HEAD 原算法逐段检查 Validate 的 OR、Forward status/Inside、Backward status/finite、refined Inside/距离/FB、Coverage 的 AND、NonTranslation 的 variation/residual/support AND，以及零主流/常规 runner-up 的先后顺序。记录函数返回原 bool；各短路项仍在原位置执行。LK/GFTT、权重、确定性共识、中位数、出生锚点、拒绝/暂停与唯一可信提交的数值和次序均保持。
- 全部非计时 MotionResult 在两份独立 Tracker 上精确对照。额外静态提取头文件确认 SameMotion 覆盖全部 30 个非计时 MotionStatistics 字段；顶层 accepted/state/reason/delta/total/confidence 同样覆盖。快照 result 直接复制返回结果，包括真实时长。
- Process 的 before 身份来自原可信参考，after 身份来自最终实际结果；拒绝保留参考，第三次拒绝 Paused，Paused 只记录 TrackingState，不伪造 CV 阶段。Initialize 延续原先 Reset 后的全新会话语义，beforeValid=false。
- mainMotion 为生产最终一致集；strongestCompetitor 为已构建过的最高单位格比例候选；eligibleRunnerUp 只在 `separation > 2 * ConsensusRadius` 且推进生产 runnerUpRatio 时更新。普通 runner-up 拒绝来源复制 eligibleRunnerUp，零主模式早退单独保存实际拒绝候选；ambiguityComplete 标识扫描是否完成。点的 competitor 标记归属于 strongestCompetitor，不能解释为必然触发拒绝的候选。
- 候选/标记的处理坐标与参考/当前 bounds 一致；网格 fit/FB/共识按参考格，retained/added 按当前格。仅在原内点循环已算过的点上标 residualComputed；addedPoints.committed 由实际 accepted 结果决定。支持外接范围不代表 ROI。
- 候选数值门限来源已核对：单半径由 consensusRadius 提供、双半径沿原 `2 * ConsensusRadius` 表达式，零主长度由 nearStationaryLimit 提供，新次流比例由 zeroModeRatioThreshold 提供，interior 的实际比较/6 和普通 runnerUpRatio 的实际比较/.35 均由原检查点填 checks。显示端应消费这些数值与 evaluated 标志，不重判算法。
- 关闭路径不持有/分配 TrackerDebugSnapshot、点数组或图像；BeginDebug 在空指针时立即返回，非空时直接原位 construct_at 清零调用方已有存储。新增记录只用固定数组/标量、noexcept 函数或无分配赋值；不新增 OpenCV 调用、诊断容器、字符串或能逃入生产 catch 的分配。Candidate 的附加遍历只有开启时执行。
- 新 CLI 扫描全部 argv，只有严格单参数 `--scroll-follow-debug-only` 运行短矩阵；不会调用 TestValidSequences、长 `--visual-motion-tracker-only` 或 `--no-window`。

## Verification

- Lint: pass。仓库没有独立 C++ lint/type-check 脚本；执行 `git diff --ignore-submodules=all --check` 的 P1 四文件范围，无输出、exit0。额外按二进制检查四文件相对 HEAD 均保持 UTF-8 BOM、纯 CRLF、可解码 UTF-8。
- TypeCheck: pass。vswhere 动态定位原生 ARM64 MSBuild；同一 PowerShell invocation 先规范 Path/PATH、设置 MSBUILDDISABLENODEREUSE=1，执行现有 `InkeysRepo.sln /t:InkeysHeadlessTests /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false`，未创建新构建系统、改工程/依赖或规避 PDB 配置。沿 main 已确认的 C1902 环境证据在正常宿主执行，exit0，7.904s；日志 `Build/ARM64/Debug/scroll-follow-debug-p1-check-build.log` 为 0 Warning(s)、0 Error(s)。这只证明测试 target 编译，不是完整 Inkeys 主程序编译。
- 构建环境消息：现有 vcpkg applocal 步骤提示 `pwsh.exe` 未找到并记录子命令9009；MSBuild 总退出0、EXE生成且下方实际测试通过。未为消除环境消息修改源码、项目或工具链。
- Tests: pass。`Build/ARM64/Debug/InkeysHeadlessTests.exe --scroll-follow-debug-only`，exit0，输出 `PASS scroll follow debug short failures=0 snapshot_bytes=71008 seconds=1.27269`。覆盖初始化、正常/反向、实际奇数尺寸/非紧凑BGRA、借用缓冲复用、短接力全部初始点退休、跳过拒绝后恢复、粘滞Paused、真实FB拒绝、Coverage短路、两种AmbiguousMotion、NonTranslation联合门及无效布局/时间/几何/黑帧。
- Tests: pass。四组额外/变形参数（debug-only + no-window、逆序、`=bad`、`-extra`）独立进程均 exit2 且 stdout/stderr 全空。
- 未执行长算法矩阵、UInk 回归或 GUI。最终 P1 snapshot 大小为 71008 bytes（相对实施报告的71000增加一个 double 门限字段）。
