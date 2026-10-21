# Research: VisualMotionTracker 的算法、接口与离线验收接入

- Query: 在现有 OpenCV 4.10 静态 core/imgproc/video、不增加 calib3d 的条件下，细化半分辨率网格 LK、空间计权平移、变形拒绝、可信参考保持、特征接力及严格误差测试。
- Scope: mixed（仓库/本地同版本 OpenCV 源码/官方 API；算法建议为研究推导）。
- Date: 2026-10-21
- Task routing: 本次研究 hook 的 Active task 是 `10-21-scroll-follow-mag-capture`；主会话已确认本文写在该任务的 research/，由 motion-tracker 子任务引用。本研究不切换全局 active task。

## Findings

### 1. 已读取的合同与当前事实

已读 `.trellis/workflow.md`、motion-tracker 子任务 prd/design/implement、父任务 design/implement、`research/current-architecture.md` 与父任务 `research/implementation-authorization.md`。后者覆盖旧的“只完成 Mag 准备”边界：全部后续阶段已获授权，但本轮仅允许无窗口/严格不可见 HWND，不允许 Computer Use、前台或可见图案测试，不 commit/push。本研究只读调查并写本文；没有运行 GUI、跟踪器实验或构建。

父设计要求图像独立接口 `Initialize/Process/Reset`，输出 `accepted/delta/total/confidence/reason/stats`，半分辨率灰度、8×6 网格约 320 角点、LK 21×21/maxLevel=3、FB、空间计权鲁棒纯平移和内点中位数。只接受帧才能推进参考、累计和补点；严重无效立即 Paused，普通低可信连续 3 帧 Paused，不能自动恢复。父 implement 的误差原文为：静止 **≤0.5 原图 px**；累计 **≤max(3 原图 px, 实际路程×0.2%)**。`max` 表达式不是“3px 和 0.2% 二者同时成立”，路程也不是起终点净位移。不能放宽成画面尺寸百分比或只检查最后一帧。

文件与用途：

| 文件 | 已确认内容/接入用途 |
| --- | --- |
| `Inkeys/Inkeys/CV/ShapeRecognition.cppm:1` | 同目录独立 CV 接口用 C++20 module，标准库位于 global module fragment，导出 `Inkeys::CV`，不向外泄漏 cv 类型。 |
| `Inkeys/Inkeys/CV/ShapeRecognition.cpp:11` | 实现 include `opencv2/core.hpp`、`imgproc.hpp`；14–15 行 pragma 链接 `opencv_core4.lib`、`opencv_imgproc4.lib`；17 行进入 module 实现。 |
| `Inkeys/Inkeys/CV/ShapeRecognition.cpp:1022` | 边界捕获 `cv::Exception` 与未知异常，返回显式拒绝原因。 |
| `Inkeys/Inkeys.vcxproj:415` | 统一包含 `$(VcpkgInstalledDir)$(VcpkgTriplet)\include\opencv4`；1082–1083 行登记 CV `.cpp/.cppm`。 |
| `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:60` | Debug 依赖也选择 Release；71–75 行 `/MT`、`NDEBUG`、取消 `_DEBUG`；83 行已有 opencv4 include；89–90 行已有 Release lib 搜索路径。 |
| `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:139` | 测试直接重新编译真实 CV `.cpp/.cppm`，无需创建新库、新构建系统或复制算法。 |
| `InkeysHeadlessTests/draw3_shape_recognition_tests.cpp:17` | 测试 import 生产 module；24 行简单 Expect；1032 行 `RunDraw3ShapeRecognitionTests()` 返回失败计数。 |
| `InkeysHeadlessTests/animation_tests.cpp:1639` | 唯一 main；1641 行已有早返回 `--shape-recognition-only`；1660–1661 行 `--no-window`；1716 行把纯 CV 测试加入默认总失败计数；1733 行失败返回 1。 |
| `Directory.Build.props:19` | ARM64 triplet=`arm64-windows-static-v143`，安装根 `VcpkgInstalled/Arm64/`。 |
| `VcpkgTriplets/arm64-windows-static-v143.cmake:1` | arm64、静态 CRT、静态库、v143；不需要修改。 |
| `vcpkg.json:18` | opencv4 默认 features 关闭，dshow/msmf/intrinsics/thread；70–72 行锁定 4.10.0#3。 |
| `VcpkgInstalled/Arm64/vcpkg/status:195` | 实际安装为 opencv4 4.10.0、port-version 3、ARM64 静态 triplet；206 起四个指定 feature。 |
| `VcpkgInstalled/Arm64/arm64-windows-static-v143/include/opencv4/opencv2/opencv_modules.hpp:13` | 有 CORE/IMGPROC/VIDEO 等，**无 CALIB3D/HIGHGUI**。 |
| `VcpkgInstalled/Arm64/arm64-windows-static-v143/share/opencv4/OpenCVModules.cmake:111` | `opencv_video` 是静态库，114–115 行只依赖 core/imgproc；实际 lib 目录有 `opencv_video4.lib`，没有 calib3d。 |

因此实现只需 include `opencv2/video/tracking.hpp`，沿用现有 pragma 方式再引用 `opencv_video4.lib`。不要调用 `estimateAffinePartial2D/estimateAffine2D/findHomography`，它们属于未安装的 calib3d。变形诊断可以用 core 的小矩阵 `cv::solve`，而平移模型只估计两个数。

### 2. 纯接口建议（供下一顺序 implement 定稿）

建议沿用相邻 `.cppm/.cpp` 的 `Inkeys.CV.VisualMotionTracker`，以 private implementation 隔离 cv 状态；若使用 `unique_ptr<Impl>`，析构函数在 `.cpp` 的完整 Impl 后定义，避免不完整类型析构实例化。若改用普通 `.h/.cpp`，同样保持公开接口只有标准库和数值类型。无需 HWND、Draw3、UInk、Display、Mag、线程或 UI 类型。单实例只由会话 worker 串行访问；公开 API 不自己创建线程。

- `ImageView`：`const uint8_t* data`、可验证的 `sizeBytes`、`width/height`、`strideBytes`、`PixelFormat{Gray8,Bgra32}`，明确定义 top-down。调用期间借用，返回后不保存调用方指针。BGRA 的 alpha 不参与有效性/灰度判断，PrintWindow 的有效 RGB 可能 alpha=0。
- 捕获质量用普通值字段（例如 `captureValid`）传递，不传 Mag 类型；`Process` 自己仍检查数据布局、黑帧、特征和几何。帧号可以只是单调输入序号，用于诊断，不把 hash 不变当作陈旧无效。
- `Initialize(frame)`：成功建立 F0/特征后 total=(0,0)，失败保持未初始化/安全失败；初始化成功不能伪称接受过一次滚动。
- `Process(frame)`：返回 `accepted`、原图像素 double `delta/total`、`confidence[0,1]`、枚举 `reason`、`pauseRequired`/状态、stats。拒绝时 `delta=(0,0)`、`total=lastTrustedTotal`。候选位移若要诊断应另设字段，防止上层误用。
- `Reset()`：清引用、特征、累计、失败计数和 pause latch。Paused 后 Process 不自动重建基准；恢复必须显式 Reset/Initialize 新会话。
- 原因至少可区分 `NotInitialized/InvalidImage/GeometryChanged/InvalidCapture/BlackFrame/InsufficientFeatures/ForwardBackward/InconsistentTranslation/InsufficientCoverage/NonTranslation/AmbiguousMotion/JumpTooLarge/Paused/OpenCvException/UnexpectedException`。捕获输入无效、几何变化与 OpenCV 结构异常应请求立即停止/暂停；普通视觉拒绝按连续 3 帧计数。
- stats 最少：input/ref seq（或 reference generation）、attempted/forward/backward/FB-valid/inlier 数、原始点数内点率和**空间权重内点率**、occupied/inlier/interior cells、跨度、FB median/p95、translation residual median/p95、runner-up support、affine field variation、added/retained/retired/initial-survivor 数、relay generation、consecutiveRejects；耗时由 tracker 或调用方测量均可。

输入检查必须在构造 cv::Mat 前做有溢出保护的 `(height-1)*stride + width*bytesPerPixel <= sizeBytes`、stride 下限、尺寸上限、正尺寸和已知格式检查。不要用不受控的负步长。两帧实际尺寸/格式/处理比例不一致就拒绝，不悄悄 resize 到旧大小继续累计。

### 3. 坐标与提交事务：首先保证不会吞位移

令 `sX=processingWidth/originalWidth`、`sY=processingHeight/originalHeight`。偶数图像均为 0.5；奇数图像按实际比例换算，不能硬编码乘 2 后造成持续比例误差。所有 total/内容锚点用原图物理 px 的 double，不做 DPI/DIP 转换、整数取整、平滑、deadband 或速度积分。

内容在屏幕向上移 10px，输出 `delta.y=-10`。这与文档 camera 向下滚动的方向相反，测试必须显式固定符号。单点内容坐标 `a = originalImagePoint - total`，即父设计的 `current_image_point-total`。

事务次序：校验与灰度转换 → 从**最后接受参考**跑 LK/FB → 估计/变形/覆盖门 → 在临时容器里准备新参考、存活点和补点 → 全部成功才一次提交。拒绝只允许更新失败计数和诊断；不得替换 refGray、推进点坐标、修改锚点、老化/删点、补点或加 total。失败后再来的正常图仍测 `lastAccepted→current` 的整个间隔。

例：可信 F0=0，F1(-5) 因局部遮挡被拒，F2(-10) 恢复正常；F2 应输出 delta≈-10、total≈-10，而非 -5。两次拒绝后接受一次必须清连续失败计数。第三次普通拒绝后 Paused 锁存，即使 F4 再清晰也不能自动恢复。黑帧立即锁存，另起用例用 Reset/Initialize 测恢复。

像素完全相同的有效高纹理灰度图可明确接受零位移（仍先检查黑/有效性与初始化特征），避免数值噪声在静止序列累积。**仅逐字节相等可走此分支**；近似相等、低 hash 差、位移小于阈值均不能直接归零，否则 0.25px/帧会被吞掉。重复的有效捕获帧是正常状态，不能因重复 3 次就 pause。

### 4. 网格检测、LK 与 FB 的可实施起点

1. BGRA→gray，`resize(..., INTER_AREA)` 到实际半分辨率；Gray8 输入也走相同缩小路径。不做逐帧自适应直方图或阈值二值化。
2. 8×6 整数划格。配额每格 6 个，固定 32 格多 1 个，最多 320；配额是**上限**，不以低质量角点凑数。每格 GFTT，qualityLevel=0.01、minDistance≈8 处理 px、blockSize=3、非 Harris，为起点。用局部 ROI+足够 halo 或全图 mask，返回坐标统一转全图；跨格也执行 minDistance 排斥。
3. 原图外边保留约 11–12 处理 px 的有效 patch 边距，避免反射边界伪匹配；内部分格线不应被当作图像边。纯色没有角点就是特征不足，不能制造随机点。
4. LK `Size(21,21),maxLevel=3,TermCriteria(COUNT|EPS,30,0.01),minEigThreshold=1e-4`；前向 `ref→current`，在前向成功且 finite 的点集上反向 `current→ref`，两方向 status 均需成功。默认 err 是光度 L1，**不是像素位移误差**。
5. `FB_i = ||back_i-ref_i||2 <= 1.0` 处理 px 为起点；保留边界/finite/patch 光度检查。FB 的度量若改 L∞ 必须一并记录，不能混用日志与门槛。
6. 每对帧先不依赖预测也可完成起步。以后如用 `OPTFLOW_USE_INITIAL_FLOW`，只能提供基于上一可信模型的初值，不能由预测直接提交位移；前后向不能复用错误含义的 initial vector。
7. maxLevel=3 是包含第 0 层的最多四层；不是“21×8 以内必能追上”的保证。纹理、重复图案、采样、拒绝间隔都会改变实际可靠范围。大步/无重叠要拒绝，而非重置 total 后假装继续。

局部同版本依据：`.../include/opencv4/opencv2/video/tracking.hpp:134` 起规定图像与点类型、status/err 语义，147–149 行 maxLevel，154–163 行 flags/误差/最小特征值，178–183 行签名默认参数。同版本 `modules/video/src/lkpyramid.cpp:474` 起有特征值/病态拒绝，497 行允许边界外的内建 padding 范围，因此应用自己的有效图像边距仍必要。`samples/python/lk_track.py:55` 的前后向检查和73行排斥补点可参考，不能照搬其85行每帧无条件换参考，也不能运行其 GUI。

### 5. 空间计权纯平移一致估计，避免固定 UI 数量优势

**不能先把所有 d_i 做一次中位数再找内点。** 全图 GFTT 的 strongest 320 可能主要落在工具栏；即使随后称“内点率 90%”，选到的也是固定 UI 的零位移。检测配额和估计权重两处都需限制局部密度。

可复现、无需 calib3d 的确定性共识算法：

- 对 FB-valid 点形成 `d_i=q_i-p_i`（处理 px），按**参考点**所在格分桶。每个 occupied cell 的总票为 1，点权重 `w_i=1/n_cell`；质量权重如添加，也必须在该格重新归一化，不能使高梯度工具栏获得更多总票。
- 以每个有效位移（最多 320）和格内位移中位数作候选 t；逐个 O(N²) 检查 `||d_i-t||2 <= 1.5` 处理 px。320² 只有约十万对距离，便于确定性复现。固定顺序、相同票时按覆盖、残差、候选索引裁决，不能随机挑出对测试有利的解。
- 最佳一致集内按 w 对 x/y 分别取加权中位数，再重新求一次内点。记录 `sum_inlier(w)/sum_allFB(w)` 为 accepted ratio；原始点数量率只作诊断。
- 起点：FB-valid≥40，内点≥40，空间票比例≥0.65；至少约16个格、横向≥4格且跨度≥0.5W、纵向≥3格且跨度≥0.4H，并有足够中部支持（例如中央6×4格至少8格）。这是保守的覆盖建议，**尚未校准**，需结合合法窗口尺寸/稀疏文档的拒绝率确认，不能声称父设计已经固定了这些值。
- 分母还需记录 attempted/reference occupied cells 及有效点存活率。否则内容点全部 LK 失败而工具栏存活时，工具栏会在剩余点里变成“100%”。全图/中部覆盖门和存活质量必须阻止这种退化。
- 比较空间上不同的 runner-up。清楚的≥65%最佳与≤35%次优可容忍工具栏；两组接近就 `AmbiguousMotion`。当最佳是静止且另一组在中部有稳定非零平移、空间支持明显时，应保守拒绝，不能让“多数静止”掩盖滚动。
- 真实静止时各区域一致为0，应高可信接受。少量光标/视频块不应因“存在第二种流”直接拒绝；要求第二模式的多格支持和显著超出跟踪噪声，具体噪声阈值需数据校准。

**适用边界**：以上处理的是固定 UI 拥有“多数角点但少数空间面积”。若固定 UI 本身占据大部分可观测区域，且少数区域运动，单纯图像无法知道用户意图是哪个 pane；应尽可能检测歧义并暂停。不能宣称无需目标 ROI 就能识别任意多数面积固定 UI；尤其微小运动低于测量噪声时更无此保证。V1 的“主屏单一内容”限制应如实保留。

### 6. 变形/重排拒绝必须在平移修剪前看位移场

不能只在已经被1.5px修剪的平移内点上拟合变化；放缩/旋转时这样会留下中心一小片，误报其“纯平移残差小”。从所有 FB-valid 的格内中位流建立诊断模型：

`d(x,y) = t_affine + B * [x-centerX, y-centerY]`。

每格一票、坐标居中并归一化，core `cv::solve(...,DECOMP_SVD)` 解两个3变量最小二乘；用固定少量 Huber IRLS 抑制少数动态格。该模型只作拒绝诊断，**绝不把仿射/缩放/旋转结果用于输出位移**。病态/单带分布不当作通过，应回覆盖门。

同时量：B 在可见/支持区域角点造成的最大位移变化、加权 translation residual、affine residual、模型被多少分布格支持。起点可用：变化>约1处理px，仿射残差显著比纯平移改善（如降一半）且≥65%分布格支持时判 `NonTranslation`。纯平移+固定工具栏的主要区域已有很小平移残差，不应仅因粗糙 affine 系数非零就拒绝；布局重排/多区不一致不能通过覆盖与平移残差门时同样拒绝。

1% zoom 在640×360处理图边缘产生数px变化，1° rotation 更明显；接近0.1%/很小角度可能低于测量分辨率，不能承诺全尺度拒绝。测试至少包含±2%缩放、±1°旋转、平移叠加变形、分区重排，再测0.5%/0.25°附近边界，输出实际变化量与拒绝原因。门槛为显著变形设计起点，不是已实测的灵敏度。

### 7. 亚像素长期误差：保留内容锚点，而非每帧重新命名锚点

仅把每帧 LK 增量取中位数相加，FB、低残差和 double 都不能消除插值造成的系统偏差。例如每帧0.01px偏差，2000帧就是20px。`cornerSubPix` 也不是数学保证，必须测。

在父设计“内容坐标=current_image_point-total”之内，建议明确两个细节，避免实现退化为永久随机游走：

1. 角点在首次检测时做局部亚像素定位（如 `cornerSubPix` 的 `winSize=Size(3,3)`，即7×7全窗，`zeroZone=Size(-1,-1)`、COUNT|EPS 30/0.01），保存该 track 的**固定出生内容锚点** `a_i=point_original-total_at_birth` 和 birth id。这里 winSize 是半窗；不能误把Size(5,5)理解为5×5全窗（实际11×11，见本地imgproc.hpp:2035）。后续 LK 定位可在小邻域重新细化同一角点；跳到邻角/偏离 LK 太大（起点约1处理px）就剔除，不能无门槛吸附。
2. 后续形成绝对候选 `T_i=q_i_original-a_i`，对可信平移一致集作空间加权中位数得到 `T_new`，输出 `delta=T_new-T_old`。出生锚点在该 track 活着时不变；不能每一帧重新 `a_i=q_i-total_new`。既有点从当前图重新定位，使误差尽量成为相对出生帧的局部定位差，而不是逐帧积分。

LK 仍只对最后接受图与当前图运行；这没有改为帧间预测，也不需要全局拼图/长图/新依赖。若前向细化位置被使用，应以细化后的 q 做反向验证或增加相应一致性检查；不能保留来自另一坐标的旧 FB 值冒充已验证。

点的 p 与固定 a/total 可能有小残差：共识先用 `q-p` 选运动群，再用 `q/s-a` 求 total，二者应有一致性门与统计。另一种等价的实现是把用于下一次 LK 的点投影到共同刚体模型 `p=s*(a+total)`，但需要验证图像局部采样误差；不要混用两种语义后重复加 delta。

**接力**：只在接受后保留可信旧点并在稀疏格补充；现存点附近 mask 排斥，优先保留覆盖有用的旧锚点，避免每帧整批重检。新点记录 `a_new=q_original-T_new`，下一帧才参与运动估计；它们不能参与为自身出生帧求 T_new（循环自证）。不因补点而归零 total 或修改旧点的 a。原始点全离屏后，新出生点仍继承同一累计坐标，tracker 可以继续十屏。

该方案把大部分漂移机会转移到点的接力/重新定位；不构成误差保证。若严格亚像素序列仍失败，应先定位局部定位、resize采样、relay偏置、权重和拒绝参考变化的来源，必要时回设计讨论小范围模板/关键帧约束；**不可通过加 deadband、取整输出、以GT选择结果或放宽0.2%来刷过验收**。本研究没有实际序列数据，不能写成“已达到目标”。

### 8. 可信度与暂停策略

先执行硬门，再生成可信度；不要让一个较高的混合 score 抵消几何错误或缺少覆盖。confidence 是工程质量分而非统计概率，可由空间内点率、有效点数、覆盖、FB/残差、捕获质量和跳变因素归一化形成，并记录每个因子。接受条件以明确硬门为准，低于门的 confidence 无论多高都不能发布目标。

- 可信零位移：accepted=true，total不变，清低可信计数。
- 普通特征不足/不一致/覆盖不足：accepted=false，只累计连续次数；第3次 latch Paused，保留最后可信累计。
- 捕获明确失效、黑帧、图像布局/几何突变：立即暂停/停止请求。把全黑作为保守安全拒绝可以，但“图像黑”不能证明底层API坏了；深色合法页面也可能被暂停，应准确命名原因。
- 纯白/纯灰常量、很少角点：不足以认定可靠静止，走特征不足，不能用重复帧捷径接受。不能使用过宽近黑阈值把普通暗色高纹理页面全部当黑。
- 跳变上限应针对“最后可信参考到当前”的总位移，不能只按1帧量检查；普通拒绝间隔可扩大到2–3帧。超出可可靠匹配范围/重叠丢失就暂停，绝不偷偷重建基准以吞掉未知段。

### 9. 确定性离线图像序列（全部无需 HWND）

使用生产 tracker，独立 `SyntheticCanvas/Renderer` 仅产图和私有 ground truth；tracker 只得到 ImageView/普通质量值，不得读 camera/期望位移或目标点坐标。

**画布**：固定显式 xorshift32 种子（如0x13579BDF、0x2468ACE1、0xA5C31E27），生成灰度长画布。可采用960×540原图，纵向画布4W×14H，水平画布14W×4H，每次只保存一个方向，约30MB而非14W×14H。背景、稀疏文字状短横竖线、不同大小矩形/圆、少量细纹理，分布随机但不重复平铺；可固定轻微blur抑制最高频混叠。避免只用棋盘、每屏同图、周期正弦，它们会使错配无法被测出。至少一组文档式稀疏图和一组丰富纹理图。

**渲染**：所有帧从同一张不可变画布按**绝对 camera 位置**抽取；整数用ROI，非整数可用 `getRectSubPix`（gray后转BGRA）或显式双线性采样。不要对上一帧反复 warp，避免把累计重采样模糊当作 tracker 漂移。分数先选1/8、1/4、3/8等能精确表示的步长；1/32插值表量化不能被误当GT。zoom/rotate 用同一画布与显式几何一次渲染，常量边界必须留在 viewport 外。`groundTruthTotal=-(camera_t-camera_0)`；与算法估计独立计算。

固定工具栏/侧栏在抽取内容之后覆盖到 viewport 固定坐标；动态块也在该阶段独立绘制。BGRA stride 既测紧密也测 `width*4+32`，alpha=0/255各一组；奇数尺寸另测实际缩放比。

**必须有断言而非只打印的矩阵**：

| 用例 | 确定性输入与关键断言 |
| --- | --- |
| 静止300帧 | 相同高纹理图300次；全接受、无pause、max total drift≤0.5px、参考/计数正确。再用固定±1级亮度/小幅独立噪声序列迫使其经过真实LK路径，分别报告，不能仅靠相等图捷径证明LK静止稳定。 |
| 十屏整数纵向 | 至少10H净移动（例每帧6px共900帧），每帧保留充分重叠；断言无意外拒绝、prefix累计误差门、初始点存活数最终=0、added>0、多个relay。 |
| 十屏含亚像素 | 至少10H（例每帧6.75px共800帧）；同上，最终误差之外还查全部prefix，不能只测8帧短段。 |
| 很小亚像素 | 0.25px/帧至少2048帧、另测0.375/1.25px；加正负/二维路径。检出deadband和每帧小偏差，不能把这些帧归零。 |
| 反向与水平 | 同一画布沿+Y→-Y、+X→-X、斜向，含回到起点；精确检查符号、每段与闭环误差，路程按各步范数相加，不能用净位移作分母。水平长段也要原始点全离屏。 |
| 固定UI角点多数 | 固定top=H/6、right=W/8，高密度非周期小细节；内容较稀疏但分布充分。额外用全图GFTT/工具栏mask记录并断言其原始角点多数（如>65%），同时记录grid权重占比；期望仍追随内容。不能只画一块没有角点的纯色工具栏就称覆盖此风险。 |
| 面积歧义 | 大部分画面固定、多个中部格另一组清楚运动；期望Ambiguous/coverage拒绝而非accepted零位移。承认低于噪声的不可观测边界。 |
| 动态干扰 | 变化文字块、随机像素块/独立小平移块，占约10–20%分布格；内容主体应通过并守误差。再构造40–50%互相竞争的流要求拒绝，不能选任何一组就算成功。 |
| 拒绝后保留参考 | 在1帧/2帧加入严重局部干扰但不是硬无效，恢复到camera已继续移动的正常图。先断言拒绝、ref generation/total不变，再断言恢复的delta覆盖整个间隔。另用3次拒绝验证pause sticky。每次记录输入帧和ref帧，防止测试其实没有触发拒绝。 |
| 黑/纯色 | 全零RGB（alpha任意）立即暂停；纯白/灰不足3次暂停；暗色但丰富纹理单列，防止过宽黑阈值误伤。黑后不能在同一未Reset会话自动恢复。 |
| 特征不足 | 1–3个孤立角、仅单细条带/单个格、高count但小覆盖图，分别验证count/coverage门，不能仅测0角点。 |
| 缩放旋转重排 | ±2% zoom、±1°rotation、它们+平移、分区重排；total/ref不动，不得累计旋转中心的近似平移。0.5%/0.25°边界输出真实拒绝灵敏度，不能预先宣称通过。 |
| 大步/无重叠 | 不可匹配的大跳/新页面，拒绝并按策略暂停；不能靠Initialize成功归零后继续。 |
| 图像与寿命边界 | null、宽高/stride/sizeBytes非法、尺寸变化、padding、奇数尺寸、Reset后重启。Initialize返回后修改/释放输入缓冲，再用正常序列，证明参考为自有拷贝。 |

累计误差每帧用欧氏范数 `E_t=||estimateTotal_t-GT_t||2`，路程 `L_t=sum ||GT_j-GT_(j-1)||2`，断言所有合法接受帧 `E_t<=max(3,0.002*L_t)`；另输出各轴误差、step误差、max E、final E、误差发生帧、`max(E_t/max(3,0.002L_t))`。静止单独用0.5px门。坏帧期间对“拒绝后仍保持旧total”作状态断言，不拿当前GT误差作为应移动的要求；恢复接受时必须重新满足包括拒绝期间移动的累计门。

每个场景至少打印：seed/图像与步长/帧数、GT路程/净位移、accepted/rejected/pause帧、max/final/error_ratio、最小空间内点率/覆盖、FB与残差分布、added/retained/initialSurvivors/relay、拒绝原因计数与ref seq。失败时打印第一个超限帧及其stats，进程必须非零；不要用“95%帧满足”替代硬门。耗时可报preprocess/LK/fit/refill median/p95，研究阶段不能预填性能结果。

### 10. 工程和验证落点

最小工程改动预计：生产 `Inkeys/Inkeys/CV/VisualMotionTracker.*`，测试 `InkeysHeadlessTests/visual_motion_tracker_tests.cpp`，两个vcxproj登记真实源，`animation_tests.cpp`增加 `RunVisualMotionTrackerTests()` 声明、全量调用和独立 `--visual-motion-tracker-only` 早返回。可接受的 CLI 名称由主会话定稿；该专用入口建议像 shape-only 一样在其他测试和任何窗口逻辑之前返回，只识别严格已知参数。

新增测试无需取屏、D3D设备、字体/浏览器/Office、HWND或测试外部文件。用 `std::cout/std::cerr` 输出，沿用手写Expect失败计数。不要调用 `imshow/waitKey`、也无需 highgui。生产tracker与测试的像素算法必须同一份，不能mock光流后把结果称为图像验收。

后续 implement 构建遵循当前用户 AGENTS：vswhere定位原生ARM64 MSBuild；**同一PowerShell invocation**先 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`、`$env:MSBUILDDISABLENODEREUSE='1'`，完整 `InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`，允许至少5分钟；不单独构建Inkeys以绕过依赖。之后运行 `Build/ARM64/Debug/InkeysHeadlessTests.exe --visual-motion-tracker-only` 和 `--no-window`。完整solution、退出码、第一有意义错误、实际性能/误差由实施阶段记录；本研究没有执行它们。

## External references (OpenCV 4.10.0)

- [Object Tracking API](https://docs.opencv.org/4.10.0/dc/d6b/group__video__track.html)：LK输入/状态/误差、flags与默认参数。API文档仅支持调用合同，不为本研究的阈值、长期误差或运行速度背书。
- [Feature Detection API](https://docs.opencv.org/4.10.0/dd/d1a/group__imgproc__feature.html)：GFTT mask/质量/距离和cornerSubPix接口，用于网格检测与局部细化；不能把细化当成零漂移保证。
- [Geometric Image Transformations](https://docs.opencv.org/4.10.0/da/d54/group__imgproc__transform.html)：离线采样、resize/warp/getRectSubPix，测试应从不可变画布抽取，避免反复重采样。
- [calib3d API](https://docs.opencv.org/4.10.0/d9/d0c/group__calib3d.html)：确认常见Affine估计函数所属模块；本机没有安装该模块，本文建议不调用它们。
- 本地同版本源码根：`Vcpkg/buildtrees/opencv4/src/4.10.0-db32e53cd3.clean/`，仅阅读；安装头/模块列表以上表为准，不修改third-party。

## Related specs

- `.trellis/spec/native-desktop/index.md`：模块边界、工程登记、Evidence等级和质量门。
- `.trellis/spec/native-desktop/cpp-conventions.md`：独立纯接口、module边界、最小修改、保持编码/EOL、简短中文关键注释、共享状态所有权。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：OpenCV manifest静态接入、4.10.0#3/four features、/MT与工程入口；涉及构建范围时以当前用户AGENTS为最高优先。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：错误传播/资源所有权，输出原因不把异常吞成可信成功。
- `.trellis/spec/native-desktop/scroll-follow-capture.md`：现有Mag/旧证据不能替代正式连续捕获；本文合成图仅证明tracker，不证明真实屏幕或正式capture门槛。
- `.trellis/spec/native/quality-and-validation.md`：未测项目须列未验证；该层Draw3专有着色器/GUI要求不应被纯图像tracker研究误称已完成。

## Caveats / Not Found

- **没有实现或实测**：本文没有证明320角点、FB1px、1.5px残差、覆盖16格、0.65权重、细化/绝对锚点、仿射拒绝及任何误差/性能目标已通过。所有未被父design固定的数值均为可检验起点。实施后应记录实际数据，不倒填“研究通过”。
- 独立图像tracker当前不存在（CV目录只有ShapeRecognition实现）；不存在可直接复用的tracker类或现成synthetic motion tests。`--visual-motion-tracker-only`也是建议新增入口，当前不可运行。
- 本地安装确有video/core/imgproc静态库且无calib3d/highgui；这不等于新增源码已经成功编译/链接，仍需真实完整solution验证。
- 内容锚点、独立亚像素重定位可减少帧间累计偏差，但不提供严格数学漂移上界；周期纹理、模糊、反复接力或半分辨率信息不足仍可能失败。若测不到目标，应保留失败证据，不能扩大容差、跳过小步或重新初始化total。
- 固定UI“多数角点”与“多数可观测面积”是不同问题；后者无目标语义时有根本歧义。V1不能声称所有混合内容布局都可可靠自动选主内容。
- 正式capture真机门槛由主会话/捕获实施另行判断；本文研究不放行tracker产品接入，也不能以旧3ace726b基础Mag或离线图像结果代替新的连续捕获、真实像素和屏幕端到端验证。
