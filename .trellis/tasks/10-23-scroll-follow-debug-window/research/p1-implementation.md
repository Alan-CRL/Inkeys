# P1 只读 Tracker 快照实施与验证

2026-10-23，基于 feature/move 980dc39b。仅改动 VisualMotionTracker.h/.cpp、visual_motion_tracker_tests.cpp、animation_tests.cpp；未改生产窗口、Capture、Business、工程 XML、OpenCV 配置或依赖。没有 commit/push。

## 接口及所有权（P2/P3 交接）

- `Initialize(const MotionFrame&, TrackerDebugSnapshot* debug = nullptr) noexcept` / `Process(...)`。现有调用不变，MotionResult 语义不变。调用方串行持有快照存储；调用结束后是完整按值数据，不持借用图像或 OpenCV/窗口对象。
- Snapshot ARM64 sizeof=71000 bytes；固定 points[320]、addedPoints[320]、cells[48]、checks[Count]。所有记录函数 noexcept，无诊断 vector/string/new。BeginDebug 原位 destroy_at/construct_at，不在关闭路径声明大型聚合临时量。
- inputSequence、referenceSequenceBefore/After、referenceGenerationBefore/After 与 referenceBefore/AfterValid 分开。Initialize Reset 后没有旧参考；Process 的 before 才是本次计算使用的可信参考，after 是最终结果所报的新/旧参考。
- processingWidth/Height 与 scaleX/Y 在实际 Gray 完成后才有效。点 reference/forward/refined/backward、addedPoint.point、候选 flow、bounds 为处理图像像素；除以各轴实际 scale 才得到输入坐标。point.anchor 与 totalBefore/candidateDelta/candidateTotal/result.delta/total 已为原始输入像素，不能再次缩放。奇数尺寸不能盲乘 2。
- points 记录原参考槽索引、出生代次、内容锚点、参考/当前格，以及 Forward、Inside、细化、Backward、finite、FB、细化距离、共识、保留/退休有效标志。status=false 时可有原始输出位置，但不代表有效匹配；未执行的计算有独立 bool。残差只在原生产内点循环算过的点上 residualComputed=true。
- cells 的 reference/forward/fbValid/inliers/competitor/fitWeight/originalCellWeight 使用参考坐标网格；retained/added 使用候选当前坐标网格。affineSampleValid 和 affineModelValid 明确区分已有格样本与真的算出仿射模型。
- mainMotion 为最终生产共识；strongestCompetitor 为生产真正构建过 other 一致集的最高 originalCellRatio 候选；eligibleRunnerUp 仅由 separation > 2R 且推动生产 runnerUpRatio 的候选更新。普通 RunnerUpHigh 的 rejectingCompetitor 复制 eligibleRunnerUp，零主流早退则记录那个实际导致拒绝的候选。ambiguityComplete=false 表示候选扫描被真实早退截断。不补跑未执行拟合/聚类。
- mainMotion/competitor 的 referenceBounds/currentBounds 仅表示证据支持外接范围，不能称客户区/ROI。图例可按 point.inlier / nearStationary / competitor / fbValid 及各中间阶段绘制；retired 仅在保留阶段真的执行后有意义；addedPoints.committed 区分临时补点与已提交补点。
- reachedStages/completedStages 为 `1ULL << enum(stage)`；checks 按 enum index 读取。每项记录 evaluated、condition（原表达式真值）、measured、comparison、threshold；**condition=true 不统一表示失败**。Coverage 是 required predicates 的 AND，仿射是 variation/residual/support 的 AND，其他多数门是 reject predicates；用 firstFailure 指向实际拒绝门，未执行项显示“未执行”，不要重判。
- firstFailure=Count 表示没有实际失败门；exception 单独保留 OpenCvException/UnexpectedException，原捕获层仍决定生产结果；Paused/NotInitialized 只有 TrackingState 检查，无伪造 CV 阶段。
- candidateTotalValid=true 才能展示出生锚点估计的 candidateDelta/Total；result 为返回 MotionResult 的逐字段拷贝（包括原时长）。主共识不代表整帧接受或实际应用运动。pointBorder/consensusRadius/refinementDistanceLimit/fbErrorLimit/nearStationaryLimit 明示相同生产/显示单位门限。

## 保持生产算法的审查

原 &&/|| 各操作数通过返回原 bool 的记录函数或局部 lambda 记录，保持短路和调用顺序。保持 LK/GFTT/采样、全部阈值、权重、中位数、Inliers、NonTranslation、拒绝/参考事务。norm/Length/Percentile 使用原位置结果，不为记录 measured 值重复执行；Coverage 行列 count 也只计算一次。附加支持范围、点标志等只在 debug 非空时执行，不参与任何生产决定。诊断所需限值仅把原相同字面值命名为 constexpr。

## 构建与短测试

构建前由 vswhere 定位：`C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe`（命令未硬编码该版本）。每次同一 PowerShell invocation：

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nativeMsbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild/Current/Bin/arm64/MSBuild.exe | Select-Object -First 1
& $nativeMsbuild 'InkeysRepo.sln' '/t:InkeysHeadlessTests' '/p:Configuration=Debug' '/p:Platform=ARM64' '/m' '/nr:false' '/v:minimal' '/fl' '/flp:logfile=Build/ARM64/Debug/scroll-follow-debug-p1-build.log;verbosity=normal'
```

- 第一次沙箱运行 exit1 / 64.4477s，三个改动 TU 一致 C1902（PDB manager mismatch），无更早源码错误。按既有环境规则，原命令在正常宿主执行，通过 exit0 / 6.5706s；未修改 PDB/SDK/工程来绕过。
- 最后有效性字段增加后，同一正常宿主构建 exit0 / 7.1441s。保留构建日志在上述 Build 路径。post-build 报 `pwsh.exe is not recognized`，MSBuild exit0 且 EXE 生成；此为该环境 PATH 下的现有后置脚本消息，不是本次源代码编译错误。
- `./Build/ARM64/Debug/InkeysHeadlessTests.exe --scroll-follow-debug-only` 最后运行 exit0 / command 1.9980s，输出 `PASS scroll follow debug short failures=0 snapshot_bytes=71000 seconds=1.33699`。
- 短矩阵明确比较两份独立 Tracker 的全部非计时 MotionResult 字段，并核对快照的精确拷贝、实际点/网格计数、前后参考身份、候选/实际运动和各未执行项。覆盖初始化、奇数尺寸、非紧凑 BGRA ROI、借用缓冲复用、正常/反向、22 帧特征接力（全部初始锚点退出）、1 次拒绝后跨参考恢复、3 次拒绝及粘滞 Paused、真实 FB flat-frame 拒绝、两条 AmbiguousMotion 门、Coverage 短路、NonTranslation 三项联合拒绝、布局/overflow/时间/几何/黑帧提前返回。
- 对四组错参：`--scroll-follow-debug-only --no-window`、逆序、`--scroll-follow-debug-only=bad`、`--scroll-follow-debug-only-extra`，均 exit2 / 0 输出行，未落入 GUI 或长矩阵。
- `git diff --ignore-submodules=all --check` 通过；四个文件保持 UTF-8 BOM 与纯 CRLF。未执行长 OpenCV 全矩阵或 UInk 回归；未启动 GUI。

## 后续及证据边界

本阶段只证明上述短用例的诊断开/关结果精确一致，不代表主程序完整构建、真实 Mag/GUI/性能通过。实际窗口/捕获图像寿命由 P2/P3 负责。Forward 低点数、JumpTooLarge、AnchorDisagreement 及 OpenCV 异常的专门负例未被本短矩阵强制触发；对应原生产位置已记录，独立 review 仍需审查这些未实测分支。P1 文件已交回 main/check，不再继续编辑。
