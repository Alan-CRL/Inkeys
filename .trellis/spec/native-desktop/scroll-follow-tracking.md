# 通用滚动批注的独立视觉追踪

## 范围与接口

`Inkeys/Inkeys/CV/VisualMotionTracker.h/.cpp`为普通头/Pimpl纯图像模块，公开类型只用标准库和数值。Initialize/Process(MotionFrame, TrackerDebugSnapshot* = nullptr)->MotionResult，Reset；不创建线程，不依赖HWND、Mag、Draw3、UInk或主栏。沿原OpenCV4.10.0#3静态core/imgproc/video，不引入calib3d/highgui，不改依赖或OpenCV全局运行策略。

MotionFrame的image为top-down Gray8/Bgra32借用data/sizeBytes/strideBytes/宽高/format，加timestampSeconds、sequence、captureValid。借用仅当前调用，内部拥有参考灰度；alpha不参与灰度。先验证尺寸/布局/overflow再构造Mat。MotionResult含accepted、state/reason、原图double delta/total、confidence、参考/特征/覆盖/FB/残差/接力/阶段计时统计。内容向上是负Y；拒绝delta=0、total保持最后可信值。Initialized成功total=0不是已有滚动。

## 接受事务与暂停

仅可信帧一次提交参考图、存活点/补点和累计量。1/2次普通拒绝保留原可信参考与所有锚点，恢复帧必须包含跳过间隔的完整位移，不能吞中间运动。连续3次普通拒绝锁存Paused；无效布局/capture/几何/时间/异常及黑帧立即Paused。Paused Process不自动恢复，显式Initialize或Reset才重开。

不以hash相同判停更，不deadband、取整、近似归零；有效静止帧仍可通过真实LK接受零位移。confidence为工程质量分，不是概率，硬门优先。消费侧不得另外丢弃Tracker已提交的可信参考而仅忽略位移。

## 当前算法参数

- 原图160x120到16384x16384仍需特征门；BGRA转灰，INTER_AREA至floor(width/2),floor(height/2)，按各轴实际尺寸还原一次，不盲乘2或DPI。
- 8x6网格，32格7点/16格6点，最多320；GFTT quality=.01/minDistance8/blockSize3，边界12处理px；格检测4px halo，cornerSubPix半窗3、30次/EPS.001。旧点跨格也按配额、优先保留老出生锚点，点数不能逐帧增长。
- LK21x21、maxLevel5（从0计最高层，实际层数受图像尺寸限制）、30次/EPS.01、minEig1e-4；细化偏差<=1处理px，细化后重新反向LK，FB<=1处理px，检查双向status/finite/patch边界。
- 拟合及出生锚点的每点票为1/max(该参考格FB-valid点数,3)，每格最多1票；不足3点的稀疏格按可靠性降权。确定性候选纯平移支持半径1.5处理px，内点x/y加权中位数重求一致集。spatialInlierRatio为内点拟合票/全部实际拟合票，不能再以occupiedCells作分母；.65数值不变但统计模型已经校准。FB-valid/内点>=40，拟合票支持>=.65，存活>=.35；>=16内点格、中部6x4>=8格、横向>=4列/跨度.5宽、纵向>=3行/跨度.4高。归一化二维跳变>.25拒绝。
- 歧义检测独立保留原单位格票：每点1/格内FB-valid点数，次流票/occupiedCells；不能复用拟合稀疏降权而掩盖真实少数流。常规runner-up门保持候选距主模式>3处理px（双半径）、票>.35；零主模式（长度<.3处理px）独立检查距主>1.5处理px、>15%空间票且>=6个中部格的次流，拒绝AmbiguousMotion并记录次流票，防固定面积多数被误认可靠零运动。
- 在平移裁剪前，以所有FB-valid格中位流做core::solve/SVD、5轮Huber(.75)仿射诊断；支持区角点变化>1处理px、仿射残差<平移残差*.55且>=65%格残差<=.8时拒绝NonTranslation。只作拒绝，绝不输出缩放/旋转。
- 固定每点出生锚点a=pointOriginal-totalAtBirth，终身不重建；绝对候选pointOriginal-a求newTotal，delta=newTotal-oldTotal。可信补点按当前total反算出生锚点，下一帧参与，不重设世界原点。

独立check补充的多数固定区域/中部运动用例中，4/10原图px次流会拒绝；1/2原图px仍落在现有1.5处理px一致集半径内，会被合并并接受近零结果。该边界属于当前模型的合并规则，不能承诺任意混合pane或少数区域微小运动的可靠目标选择；更细多模态模型不在本阶段范围。

## 验证要求与已知证据

GT必须从不可变非周期长画布按绝对camera一次采样，不递推warp、不从Tracker输出选GT。静止300漂移<=.5原图px；每个合法prefix累计误差<=max(3原图px,实际路径*.002)，路径不是净位移。覆盖亚像素小步2048、>=10屏/全初点离屏、水平反向二维、固定UI角点多数、稀疏文档、动态干扰、暂停/拒绝保持参考、BGRA alpha0与padding/奇数/寿命、低信息/覆盖/缩放旋转/重排/无重叠。

当前实施矩阵6918合法帧全接受，全prefix通过；12屏亚像素最大约.92px，静止0px/微噪声约.006px，水平10屏约.36px。具体最终数值、每序列P95/点数/接力和GT证据见motion-tracker任务research/implementation-current.md。960x540/961x541 Debug测试耗时不能外推主屏Capture+Tracker FPS，不能作为真实Mag或屏幕墨迹验收。独立check另留报告，不用实现自测代替审查。

`InkeysHeadlessTests --visual-motion-tracker-only`为严格单参纯无窗口入口，参数位于任意位置但有额外参数时exit2/无输出；同矩阵纳入--no-window。完整solution遵AGENTS Debug ARM64原生MSBuild、同PS单一Path和禁node reuse，保留BOM/EOL。

本轮只允许无窗口/完全不可见HWND，禁止Computer Use和前台/可见测试。正式Mag worker真实读回、系统穿透/双窗/浏览器WordPDF/触控笔和旧Windows仍须必要实屏验收，不由合成图放行全部功能。


## 2026-10-23 大单步校准与尺寸边界

真实长程首次128原图px单步曾在maxLevel3连续一致性拒绝并暂停。只将双向LK的maxLevel3改5，其余门槛不变；同一新增128正例旧参数确实断言失败，新参数完整矩阵原6918+native704合法帧通过，大图静止300漂移0、128步长接力284帧最大误差约.00118px；大图多数固定中部4px、新页无重叠、1.02缩放、1度旋转均正确拒绝并粘滞Paused。所有新负例必须使用可实际构建到最高层5的2880×1808图，旧960×540图最多到3不能替代它们。较小画面可能裁剪层数；不得承诺所有分辨率或任意内容均可接受128px，合成通过仍须原步长/总距离真实Mag与墨迹复测。


## 2026-10-23 重复纹理与密度票校准

实际Guest一次200px滚动中，13帧完整输入及原算法非计时字段精确回放复现：frame10可信49px，11..13停在空间票24/37<.65并Paused；正文实际像素已正确移动200px，不能归因Mag没有更新。143正确FB点分布24格，34错误点分布底部13个多为1..4点的格；错误反向流与重复段落节距吻合是有依据的别名解释，非所有捕获失败的统一原因。隔离floor3候选在同输入可信得到199.999939px，旧6918+native704合法帧及4大图负例全部通过；不得把候选对旧记录的89字段差异当作exact replay PASS，原算法精确回放证据保持独立。

新增真实像素密度保护：12稀疏运动格/36密集固定格须以原单位格次流.25拒绝零主流；24+24反向流交换密度后均以原单位格次流.5拒绝；仅8密集格保留时即使56个FB点、存活和fit足够仍因Coverage拒绝。每项验证最后可信参考不变、第三次Paused与不自动恢复。形变门是variation/残差/支持率联合条件，不可用单项variation<=1替代；目标reason也必须匹配，不能让低特征早失败冒充保护通过。

测试夹具须确认稀疏真实流经LK/FB后仍有足够格支持。旧±8px双流小patch在BF原算法与候选均会丢一半稀疏格，unit次流.333不足门而选择主流，这是保留的模型边界，不能用原始GFTT点数当FB支持，也不能由此承诺可靠自动选择多个pane。修正夹具的±4px全图运动在半图两流间距4>双半径3，双密度均拒绝；原失败不追溯改判。新模型仍须完整构建/无窗口回归与产品真实输入验收，隔离测试不能直接放行全部功能。

## 2026-10-23 可选只读快照

诊断参数默认空指针，旧调用方式与结果不变；启用时在原检查位置记录有界纯数值快照，保留早退、短路和异常拒绝语义，不重新拟合。checks.condition 是原表达式真值，firstFailure 才是实际拒绝门；未执行值必须显示为未执行。referenceSequenceBefore 是实际匹配的旧可信参考，接受后 statistics.referenceSequence 已更新。点/flow/bounds 除各轴实际处理比例还原，anchor/delta/total 不二次缩放。完整跨层合同见 [scroll-follow-debug-window.md](scroll-follow-debug-window.md)。新增严格 --scroll-follow-debug-only 仅跑本次短差分，不运行原长序列。
