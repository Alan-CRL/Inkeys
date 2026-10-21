# 通用滚动批注的独立视觉追踪

## 范围与接口

`Inkeys/Inkeys/CV/VisualMotionTracker.h/.cpp`为普通头/Pimpl纯图像模块，公开类型只用标准库和数值。Initialize/Process(MotionFrame)->MotionResult，Reset；不创建线程，不依赖HWND、Mag、Draw3、UInk或主栏。沿原OpenCV4.10.0#3静态core/imgproc/video，不引入calib3d/highgui，不改依赖或OpenCV全局运行策略。

MotionFrame的image为top-down Gray8/Bgra32借用data/sizeBytes/strideBytes/宽高/format，加timestampSeconds、sequence、captureValid。借用仅当前调用，内部拥有参考灰度；alpha不参与灰度。先验证尺寸/布局/overflow再构造Mat。MotionResult含accepted、state/reason、原图double delta/total、confidence、参考/特征/覆盖/FB/残差/接力/阶段计时统计。内容向上是负Y；拒绝delta=0、total保持最后可信值。Initialized成功total=0不是已有滚动。

## 接受事务与暂停

仅可信帧一次提交参考图、存活点/补点和累计量。1/2次普通拒绝保留原可信参考与所有锚点，恢复帧必须包含跳过间隔的完整位移，不能吞中间运动。连续3次普通拒绝锁存Paused；无效布局/capture/几何/时间/异常及黑帧立即Paused。Paused Process不自动恢复，显式Initialize或Reset才重开。

不以hash相同判停更，不deadband、取整、近似归零；有效静止帧仍可通过真实LK接受零位移。confidence为工程质量分，不是概率，硬门优先。消费侧不得另外丢弃Tracker已提交的可信参考而仅忽略位移。

## 当前算法参数

- 原图160x120到16384x16384仍需特征门；BGRA转灰，INTER_AREA至floor(width/2),floor(height/2)，按各轴实际尺寸还原一次，不盲乘2或DPI。
- 8x6网格，32格7点/16格6点，最多320；GFTT quality=.01/minDistance8/blockSize3，边界12处理px；格检测4px halo，cornerSubPix半窗3、30次/EPS.001。旧点跨格也按配额、优先保留老出生锚点，点数不能逐帧增长。
- LK21x21、maxLevel3、30次/EPS.01、minEig1e-4；细化偏差<=1处理px，细化后重新反向LK，FB<=1处理px，检查双向status/finite/patch边界。
- 每FB-valid参考格总票为1，确定性候选纯平移支持半径1.5处理px，内点x/y加权中位数重求一致集。FB-valid/内点>=40，空间内点>=.65，存活>=.35；>=16内点格、中部6x4>=8格、横向>=4列/跨度.5宽、纵向>=3行/跨度.4高。归一化二维跳变>.25拒绝。
- 常规runner-up门保持候选距主模式>3处理px（双半径）、票>.35；零主模式（长度<.3处理px）独立检查距主>1.5处理px、>15%空间票且>=6个中部格的次流，拒绝AmbiguousMotion并记录次流票，防固定面积多数被误认可靠零运动。
- 在平移裁剪前，以所有FB-valid格中位流做core::solve/SVD、5轮Huber(.75)仿射诊断；支持区角点变化>1处理px、仿射残差<平移残差*.55且>=65%格残差<=.8时拒绝NonTranslation。只作拒绝，绝不输出缩放/旋转。
- 固定每点出生锚点a=pointOriginal-totalAtBirth，终身不重建；绝对候选pointOriginal-a求newTotal，delta=newTotal-oldTotal。可信补点按当前total反算出生锚点，下一帧参与，不重设世界原点。

独立check补充的多数固定区域/中部运动用例中，4/10原图px次流会拒绝；1/2原图px仍落在现有1.5处理px一致集半径内，会被合并并接受近零结果。该边界属于当前模型的合并规则，不能承诺任意混合pane或少数区域微小运动的可靠目标选择；更细多模态模型不在本阶段范围。

## 验证要求与已知证据

GT必须从不可变非周期长画布按绝对camera一次采样，不递推warp、不从Tracker输出选GT。静止300漂移<=.5原图px；每个合法prefix累计误差<=max(3原图px,实际路径*.002)，路径不是净位移。覆盖亚像素小步2048、>=10屏/全初点离屏、水平反向二维、固定UI角点多数、稀疏文档、动态干扰、暂停/拒绝保持参考、BGRA alpha0与padding/奇数/寿命、低信息/覆盖/缩放旋转/重排/无重叠。

当前实施矩阵6918合法帧全接受，全prefix通过；12屏亚像素最大约.92px，静止0px/微噪声约.006px，水平10屏约.36px。具体最终数值、每序列P95/点数/接力和GT证据见motion-tracker任务research/implementation-current.md。960x540/961x541 Debug测试耗时不能外推主屏Capture+Tracker FPS，不能作为真实Mag或屏幕墨迹验收。独立check另留报告，不用实现自测代替审查。

`InkeysHeadlessTests --visual-motion-tracker-only`为严格单参纯无窗口入口，参数位于任意位置但有额外参数时exit2/无输出；同矩阵纳入--no-window。完整solution遵AGENTS Debug ARM64原生MSBuild、同PS单一Path和禁node reuse，保留BOM/EOL。

本轮只允许无窗口/完全不可见HWND，禁止Computer Use和前台/可见测试。正式Mag worker真实读回、系统穿透/双窗/浏览器WordPDF/触控笔和旧Windows仍须必要实屏验收，不由合成图放行全部功能。
