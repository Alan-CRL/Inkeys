# VisualMotionTracker 生产实现与严格离线验证（2026-10-21）

本阶段实现独立生产 Tracker、真实 OpenCV 图像算法和现有 Headless 注册。完整 Debug|ARM64 solution、扩展定向矩阵和最终重建产物的完整 `--no-window` 回归全部通过（最终exit0，157,453ms，末尾PASS animation correctness，stderr为空）。本文不代替后续独立 trellis-check，也不构成真实屏幕或完整滚动批注验收。

## 文件与接口

- 新增 `Inkeys/Inkeys/CV/VisualMotionTracker.h/.cpp`，普通头文件/Pimpl 隔离 OpenCV，公开类型仅标准库、图像/时间数值。`Initialize/Process(MotionFrame) -> MotionResult`，`Reset()`；本类没有线程成员或线程启动入口，不创建窗口；OpenCV保留既有运行时并行实现。只静态引用现有 OpenCV 4.10 core/imgproc/video，未修改 manifest、triplet、工具链、Capture 或 Draw3。
- 新增 `InkeysHeadlessTests/visual_motion_tracker_tests.cpp`，直接调用生产实现。两个工程与主工程 filters 登记真实源码/头，`animation_tests.cpp` 增加全量调用及严格 `--visual-motion-tracker-only`。该参数出现在任何位置时都只允许恰好单参；多余参数在其他测试或窗口路径前返回 2。
- `MotionFrame` 包含 top-down `Gray8/Bgra32` 的 data/sizeBytes/strideBytes/width/height、有限 timestampSeconds、sequence 和 captureValid。借用只限当前调用，内部 `resize` 始终生成自有灰度参考；BGRA alpha 不参与灰度。
- 输出 accepted、原图物理像素 double delta/total、confidence[0,1]、MotionState、MotionReason、参考/点数/覆盖/FB/残差/接力/耗时统计。内容上移为负 Y。初始化成功为 Initialized，delta/total=0；Paused 不自动恢复，显式 Initialize 或 Reset 开新会话。

## 最终算法与参数

1. 在构造 cv::Mat 前校验正尺寸、已知格式、最小 stride、sizeBytes 和乘加溢出。当前可输入尺寸 160×120 至 16384×16384，仍须特征/覆盖门通过；几何或像素格式变化、非有限/倒退到可信参考前的时间、captureValid=false、异常立即 Paused。处理图最大灰度值<=2作为黑帧立即 Paused；暗色丰富纹理单独通过。
2. BGRA→Gray 或 Gray8，INTER_AREA 到 floor(width/2)×floor(height/2)。分别用真实处理宽/原宽、处理高/原高还原；奇数尺寸不硬乘2。没有 deadband、整数输出、hash/近似重复归零分支；静止同图也经过真实 LK。
3. 8×6网格，32格上限7点、16格上限6点，最多320；GFTT quality=.01、minDistance=8处理px、blockSize=3，边界12处理px。格内检测使用4px halo，亚像素定位 cornerSubPix 半窗3、COUNT30/EPS.001。旧点跨格后也执行每格配额，按原有出生顺序保留老锚点，避免累计增长到320以上。
4. LK21×21/maxLevel3、COUNT30/EPS.01、minEig=1e-4；细化前后同一点偏差<=1处理px。细化后重新反向 LK，欧氏 FB<=1处理px。两向 status、finite、有效 patch 边距全部检查。
5. 每个 FB-valid 参考格总票为1，确定性穷举所有候选的纯平移支持（半径1.5处理px），内点 x/y 加权中位数并重求一致集。至少40 FB-valid/内点、空间内点票>=.65、存活>=.35；至少16内点格、中央6×4至少8格、横向>=4列且跨度>=.5宽、纵向>=3行且跨度>=.4高。不同运动 runner-up>.35拒绝；零主模式和>15%空间票、>=6个中部格的清楚非零运动并存也拒绝，不能把固定面积多数当可靠零位移。归一化二维跳变长度>.25拒绝。
6. 在平移裁剪前按所有 FB-valid 格的中位位移场解3变量仿射诊断，core::solve/SVD、5轮Huber IRLS（.75）。全支持区角点变化>1处理px、仿射残差小于平移残差的.55且>=65%格残差<=.8时 NonTranslation。该模型只拒绝缩放/旋转，绝不输出其变换，也不依赖calib3d。
7. 每点出生时固定内容锚点 `a=pointOriginal-totalAtBirth`，终身不重建。当前绝对候选 `pointOriginal-a` 的空间加权中位数得 newTotal，delta=newTotal-oldTotal，并与帧间共识核对。仅可信帧在临时容器完成保留/补点后一次提交 reference/tracks/total；新点按该次可信 total 反算锚点，下一帧才参与估计。拒绝不换参考、不老化删点、不补点、不修改锚点或累计；普通拒绝连续3次锁存Paused，严重无效立即锁存。

confidence 为工程质量分，结合空间票、存活、覆盖、FB和残差；硬门优先，不是统计概率。图像不能确定任意多 pane 的用户目标，显著歧义会拒绝；低于分辨能力的变形或运动不作普遍保证。

## 离线证据

固定非周期长画布 seed=0x571AD19，尺寸3840×11040，独立稀疏文档种子为该seed XOR 0x13579BDF。水平序列用长画布转置。每帧从同一不可变画布按绝对 camera 用 getRectSubPix 一次采样，未递推 warp、未从 Tracker 输出反算或选择 GT。Tracker 只收到像素、时间/序号/质量值。GT固定为 camera 初始坐标减当前坐标；路程按每步欧氏长度累加，反向回原点也不改分母。

全部6918个合法帧必须接受且每个prefix误差满足 `<=max(3原图px, path*.002)`；两个静止300帧各使用更严格`.5px`。没有删除失败序列、放宽门槛、取整或用GT干预算法。下表来自最终重建产物的 `motion-tracker-headless-final.log`；全部 rejected=0、paused=0。axis/step/FB/residual/覆盖最小值、参考序号和各阶段P95详见原日志与 `motion-tracker-sequences-final.json`。耗时为调用方实测完整 Process（不含测试渲染）而非只求阶段和。

| 序列 | 帧数 | 路程px | 最大误差px | 最终误差px | 最大误差/门槛 | 补点累计 | 初始存活 | 最大点数 | ms中位/P95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| stationary300 | 300 | 0.00000 | 0.00000 | 0.00000 | 0.00000 | 0 | 319 | 319 | 9.19070/15.24110 |
| stationaryNoise300 | 300 | 0.00000 | 0.00613 | 0.00223 | 0.01226 | 257 | 296 | 320 | 11.01740/16.56820 |
| integer12screens | 540 | 6480.00000 | 0.00031 | 0.00015 | 0.00005 | 15495 | 0 | 320 | 15.94840/23.05860 |
| subpixel12screens | 760 | 6561.19273 | 0.91984 | 0.90788 | 0.07457 | 19410 | 0 | 320 | 10.84000/22.80350 |
| quarterPixel2048 | 2048 | 512.00000 | 0.12196 | 0.08136 | 0.04065 | 3723 | 0 | 320 | 6.55740/10.56410 |
| tiny2d | 500 | 112.67348 | 0.12954 | 0.06105 | 0.04318 | 948 | 157 | 320 | 6.67510/8.09530 |
| reversal2d | 640 | 2458.22146 | 0.15532 | 0.07631 | 0.04156 | 10088 | 0 | 320 | 8.92840/13.13860 |
| horizontal10screens | 650 | 9669.09138 | 0.35779 | 0.34827 | 0.02364 | 15893 | 0 | 320 | 8.81860/12.55400 |
| fixedDenseUi | 240 | 780.00000 | 0.20048 | 0.10746 | 0.06683 | 21225 | 0 | 320 | 12.32810/16.66940 |
| sparseDocument | 300 | 391.51149 | 0.13362 | 0.08754 | 0.04454 | 2116 | 34 | 320 | 8.02340/10.28860 |
| dynamicPatch | 160 | 684.68971 | 0.10613 | 0.08322 | 0.03538 | 6462 | 0 | 320 | 11.08690/14.34500 |
| oddDimensions | 240 | 812.21918 | 0.22374 | 0.21993 | 0.07458 | 3370 | 0 | 320 | 12.99030/18.84250 |
| bgra | 120 | 560.44625 | 0.11547 | 0.11387 | 0.03849 | 2253 | 0 | 320 | 8.61480/14.37590 |
| darkTexture | 120 | 270.00000 | 0.06049 | 0.00006 | 0.02016 | 1391 | 104 | 320 | 7.76030/9.60120 |

固定UI在顶部1/6和右侧1/8覆盖非周期高密度纹理，全图未分格GFTT共6384点，其中UI4433点，占69.439%，有显式>65%断言。正文较稀疏仍全接受，空间内点比例最小约.726。多数画面固定、中部明确运动的负例为AmbiguousMotion（主票.821、次票.179），不接受错误零位移。

另外直接断言：1/2帧50%相反流干扰拒绝后参考/total不变、恢复delta覆盖整个间隔；第3次拒绝Paused锁存、正常图不自动恢复、Reset/Initialize重启；黑0/1立即暂停，纯白/灰3次暂停；3个孤立角和54角点细条带分别拒绝；±2%缩放、±1°旋转、+0.5%缩放及±.25°边界都拒绝，平移叠加变形也不提交；两组对向运动/重排及无重叠新页拒绝。布局矩阵覆盖null、负/零/过大尺寸、stride下限/溢出、截断size、未知格式、capture失效、NaN/倒退时间、几何变化、Gray/BGRA padding最小size、BGRA alpha=0运动和Initialize后原缓冲立刻销毁。所有点数上限/初始全离屏和多轮接力均有断言。

## 构建、静态与命令证据

- 使用vswhere定位当前VS的ARM64原生MSBuild；同一PowerShell先规范单一Path，MSBUILDDISABLENODEREUSE=1，TEMP/TMP为Build/ScrollFollow/TestTemp。完整 `InkeysRepo.sln /nologo /nr:false /m:1 /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64 /v:minimal`，进程等待10分钟。正常宿主require_escalated；没有尝试在sandbox里触发旧C1902，也没有更改SDK/依赖绕过环境。
- 首次build exit0，30.24s；r2 exit0，19.66s；最终build exit0，17.61s（外部17,986ms）；三次均0 warning/0 error。最终日志 `Build/ScrollFollow/motion-tracker-build-final.log`、`-console.log`、`-stderr.log`。生成的可复用脚本 `build-motion-tracker.ps1` 位于Build，不提交。
- r1定向exit0，324,291ms，旧320预算跨格增长已在r2修正；r2完整扩展定向exit0，116,133ms，`motion-tracker-only-r2.log`/`-stderr.log`。最终源码再经完整 `--no-window` 执行相同矩阵及新增布局/点数/完整耗时断言：exit0，157,453ms，Tracker failures=0、末尾PASS animation correctness，stderr=0B。原始日志 `motion-tracker-headless-final.log`、`motion-tracker-headless-final-stderr.log`。
- 严格参数三例（命令在首位有多余参数、在--no-window之后、在未知参数之后）全部exit2、stdout/stderr=0B，`motion-tracker-bad-argv.json`及对应日志；没有进入任何窗口测试。
- 7份源/工程文件strict UTF-8、原BOM、工作树CRLF、尾随空白和EOF检查通过；3份工程XML解析通过，`git diff --check --ignore-submodules=all`通过。主树diff仅必要新增登记，没有大面积格式变化。记录 `motion-tracker-static.json`；Git子模块不可访问，本阶段没有修改或声称验证它们。
- ARM64 dumpbin /imports 退出0，`motion-tracker-headless-imports.log`未见OpenCV/FFmpeg DLL、highgui/calib3d；源码仅链接现有core/imgproc/video静态库。

最终产物SHA-256（只标识本轮构建，不代表GUI验收）：
- `Build\ARM64\Debug\Inkeys.exe`：`71AEC815B79C3790881FA2C122D6BA4C38A6B60295F3A99A2694509CB7F582F6`
- `Build\ARM64\Debug\InkeysHeadlessTests.exe`：`A7A01625ACC0C92FEFEB09E38D5118FB32607B119172C537F63890006705B529`

## 遗留与边界

此阶段没有运行普通Inkeys.exe、Computer Use、可见/前台/置顶、旧freeze-mag-printwindow、draw3-hidden-test/eraser等可能显示的入口；没有commit/push。全部图像证据为纯合成离线，不证明正式跨线程Mag真实像素/排除/20FPS，也不证明Draw3/主栏/UInk产品端到端。未执行其他架构、Win7/8.1、真实浏览器/Word/PDF/视频、实际笔/触摸、长期生产性能。最终Debug每帧耗时只属于此机器和固定960×540/961×541样本，不能外推主屏分辨率或Capture+Tracker帧率。下一步由主会话顺序独立trellis-check并继续产品集成，必要实屏验收仍为父任务停止门槛。
