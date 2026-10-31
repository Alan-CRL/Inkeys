# Scenario: 独立终态尾段重构（阶段2）

## 1. Scope / Trigger

适用于canonical Modeler目录中的本地terminal_tail_reconstructor扩展、其共享编译清单、离线CLI与未来调用方。阶段2获用户2026-10-12明确实施授权，依赖阶段1c34d3a56验收；当前尚未接产品，不表示绘制末钩已修复。求解器只能验证输入尾窗，未来产品仍须检查补丁与未传入的稳定前缀/轮廓之间的关系。

## 2. Signatures

- ReconstructTerminalTail(request, scratch) -> TerminalTailOutcome。
- ReconstructTerminalTailControl(request, scratch) 为端点平滑位移对照。
- Request显式携带reference/raw/endpoint、半开replacement_range、baseline_token和config；Outcome为Apply/NoChange/Rejected、原因、diagnostics与patch。
- patch位置来源TerminalTailSource与几何raw_match分别编码。scratch单调用方独占，span仅在下次调用/修改scratch前有效。

## 3. Contracts

- 13个上游运行源及原头byte exact；新扩展单列本地来源。不改原StrokeModeler布局、Update追加、Predict、Save/Restore、参数或Result。
- 第一版保持点数，前两个非退化入口点必须原值固定，终点精确为真实Up。重复入口或过短窗口返回拒绝，不能为凑平滑修改固定点。
- 只输出几何位置/映射，不伪造旧速度/加速度/压感，不反馈模型；raw来源和reference属性来源不能互相替代。
- 几何按弧长匹配，限制raw分支有序与窗口；位移场正则，不把整条真实曲率压平。人工pin末边不能当强形状先验。
- 无L1/像素/Windows知识；config距离随输入单位设置，默认值是实验起点，不能作为生产通用阈值。physical_turn_spacing以相同输入距离单位指定，随单位缩放；完整窗口转角按固定空间尺度检查，不能仅看原顶点转角。物理时间由调用方/报告保留，求解不从钳制的display time推断raw对应。
- 点数/匹配/检查/迭代预算有界；finite、范围、来源、入口、endpoint、位移、长度、相邻反向重叠、尾窗内自交及固定物理弧长转角检查通过才发布Apply。失败或无改善patch恒空，输入不变。
- 三消费者共享14TU（13上游+1本地），仅标记源沿用阶段1独立优化与MT/Release Abseil；不混旧merge、不改业务flags。

## 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 不足/退化入口 | InsufficientWindow，原子空补丁 |
| 非finite/非法config/range/source | InvalidInput，输入不变 |
| 超预算 | BudgetExceeded，有限工作，无部分发布 |
| 分支不可信/真实尖角/过大形变 | 拒绝或NoChange并说明原因 |
| endpoint-safe参考且无改善 | NoChange，保留旧几何 |
| 参考末点尚非真实Up | 全部硬门通过后可Apply修复endpoint，不据此声称末钩改善 |
| Apply | 固定入口/真实终点精确、finite、来源有序、范围正确 |

## 5. Good / Base / Bad Cases

- Good：实际前向模型生成尾段，明确可改窗口与真实Up，raw有序，报告改善和形变，再由调用方核验token应用。
- Base：模块opt-in独立，原模型回放与阶段1逐字段相等；短窗口保留原路径。
- Bad：把Patch伪装Result、按显示时间硬匹配、全局最近跨自交分支、Up后扩大到已L1、仅移动L0而Stored仍旧、伪称GUI/设备修复。

## 6. Tests Required

原19case/128412assert精确回放；两预测器的弧/快转Up及S/折返/自交/角点/稀疏/重复/停笔正负矩阵；原子失败、成功后复用scratch、预算/nonfinite/source/range边界；相同物理弧长效果指标与预热成本。

使用既有demo/test solution及完整InkeysRepo.sln Debug|ARM64，无窗口CLI/Headless。具体运行结论归阶段2子任务报告，配置存在不代表已通过；GUI/实体笔/Win7和产品Stored/taper/Touch接入另阶段验收。

## 7. Wrong vs Correct

Wrong：有限大endpoint权重、原Result导数配新位置、调baseline或删坏样本以通过。

Correct：硬入口/终点、显式本地补丁、有序分支、原子回退、公开负样本和适用范围；阶段2不commit/push。

## 阶段三：完成态局部拟合合同

`ReconstructLocalTerminalTail(request, scratch, LocalTerminalTailConfig)` 为独立入口，保留阶段二接口。raw 可为空或只有一个点；真实 Up 为固定终点，局部入口及修正段前的位置逐点固定。原模型 pin 边不作为末端方向/强形状先验；真实角点保护仅使用进入修正段的证据，不能因正常前段存在角点拒绝整个窗口。

Draw3 的 `RecordPenTailRawInput` 使用实际物理输入时间、位置和 accepted realPoints 索引边界，固定64点、160ms；合成 stationary Update 不入缓存，Reset 清空，接续清终态不清最近缓存。模型窗口硬限制512点；不按压缩模型时间推断物理对应。

仅 FinalizeStoredStroke 复制完整 realPoints 后拟合，允许修改当前笔划旧 L1 尾段；活跃 realPoints 和 committedIndex 不修改。先验证 token/range/source 和范围外前缀相交及笔宽公切线条件，成功只改副本位置，再沿既有 taper 与完成笔划重绘路径生成 Stored。失败原子保留基线；等待断触接续不提前固化。

距离默认值是画布像素实验起点；没有原始点时真实急转与人工 pin 的辨别能力有限。保守范围外相交检查可能拒绝原本已有交点的笔划。无窗口求解/生产 probe 通过不能替代实际设备、GPU、断触视觉或 Windows 7 验证。阶段三具体证据归 draw3-local-tail-finalization 子任务。

临时人工验收反色调试（2026-10-13）：产品 Draw3.StrokeGeometry 的 kDebugCompletedTailVisibility 显式开关，仅将实际发布且位置改变的完成尾段 RGB 取反、alpha不变。InkStroke 瞬态索引不改变 Style/文档格式，L1/L2/tile 使用统一 DrawStoredStroke；taper 插点必须映射索引。失败/NoChange不着色，日志使用 OutputDebugStringA（需调试输出采集）。无窗口测试不能证明实体GPU显示正确；人工反馈仍未验收，不得结束阶段三。

2026-10-13 无反色调查补充：生产大弧回放必须覆盖输入尺度和速度，8px 手造轨迹的 Apply 不能证明快速大弧有效。固定 3px 位移门已在半径300/1000px、0.15秒弧线中拒绝候选；必须区分拟合未调用、NoChange、求解拒绝、调用方拒绝和最终发布。raw 单点不能自动作为充分模型窗口边界，停顿淘汰 raw 后仅余 Up 可导致一点评估窗；修复必须保持物理时间边界。临时诊断现写入进程 TEMP 的 Inkeys-Draw3-tail-<pid>.log（v2），每进程128笔/2MiB、最多512模型点/64raw，含实际EXE与文字原因；日志失败不改变几何，缺 end 不可视为完整记录。该同步磁盘诊断不用于生产性能验收。

现场诊断解释合同（2026-10-13）：局部求解现行 NoImprovement 同时涵盖候选末端角超过绝对门和相对改善不足；max_displacement在该早退之后才计算，默认0不能解释为无位移或通过位移门。仅尾窗日志可重建局部请求，不能声称完整窗前相交/Controller生命周期已验证。后续重采样验收必须独立看曲线几何与折线空间误差，不能靠增加顶点降低单顶点角蒙混通过。

## 自适应完成态修复合同（2026-10-13 最新实现）

产品调用 `ReconstructAdaptiveTerminalTail`；前述阶段二和旧local入口保留作对照。触发条件为末段方向突变的可见横向尺度；不可见折线保持，raw与模型一致的真实角点不按人工pin处理。最大位移仅为诊断，不作为发布门。尝试次数、输出点数、细分深度及匹配/验证总工作均有硬上限，失败不发布部分结果。

- 在明确的模型历史窗口内按弧长扩窗。稀疏模型入口向稳定前缀方向取整，实际范围必须覆盖请求弧长与过渡余量。入口位置/进入方向固定，真实Up位置固定，末端方向由自由P2控制点求解；不能强制恢复平行方向。
- 可信raw可提示更早的入口：可靠模型段单调投影、同修正侧及前进趋势、显著法向偏离。从Up向前保留连续可信分支，建议窗口覆盖最早支持点并留出渐变距离。单点跨度与偏离可信度有限制；这些是辅助证据限制，不是候选位移门。raw缺失/仅Up仍使用模型与Up拟合。
- 有可信同侧提示时，对可靠模型段中点/终点施加局部法向半空间约束，防止用外鼓补偿内缩；原位置最小二乘度量内最多16轮投影，未满足则扩窗。该检查为有限采样约束，不宣称全连续区间解析保证。
- 连续曲率数值检查的空间尺度取可见横向回拉量，不把切向pin长度全部当成转弯半径；保留正常曲率、反曲、回弯、相邻反向重叠和自交保护。曲线细分误差与顶点角度是不同指标，不能靠加密顶点掩盖急弯。
- 输出可重采样，保留原参数锚点与有序相邻分数来源。Draw3副本按来源插值r/time并映射committed边界，随后执行既有taper；活跃realPoints不回写。原始输入和模型历史边界独立：raw64、模型物理边界128、160ms、模型输入最多512点；stationary接纳只更新模型边界，不伪造raw。
- 产品/demo按尾窗直径适配画布坐标可见性和采样误差，分别clamp(diameter×0.1,0.5,1)和clamp(diameter×0.03,0.1,0.2)。临时反色仅标记实际发布的变化尾段，日志增加attempts/repair/raw_hint/output/sampling_error等证据。完整完成态性能测试须关闭同步磁盘诊断，仅测算法不能冒充完整Up成本。

回归包含真实8笔、无raw/仅Up/稀疏/加密采样、自由末向、入口余量、孤立离群点和近轨远端单点、不可见末钩、真实角点及U/S、U形末钩不抄近路、合法预算耗尽与非finite。回放图必须直接使用C++报告，并按同一案例统一坐标比较。阶段三继续等待人工Draw3验证；未运行GUI、真实设备或Win7不得标为完成。

近直线回退可见性合同（2026-10-13）：必须先检测到末端突变，随后以可靠入射方向与Up回拉向量的叉积除以入射向量长度，测量真实横向距离。不得把角度截断到90度再乘pin长度，否则近180度共线回退会误触发。无突变/低于可见阈值返回NoChange与空patch；仍有可见侧向偏离的钝角不能统一禁用。回归含2484日志第55笔（0.469px侧偏）、多方向/多边倒退及可见斜向对照，原8笔输出须保持。

真实输入尾部支持合同（2026-10-13）：对可见异常，若唯一原始起点锚点后的尾部与模型尾部在visibility_distance内双向连续一致，应NoChange保留原轨迹。模型/raw末点都必须已为实际Up；顶点吻合只能作为证据，不能绕过线段内部或Up验证。用1-Lipschitz距离上界、有界double中点细分验证，距离工作计入match预算；不充分继续拟合，预算耗尽原子拒绝。raw少于3点不能提供该保护；默认需中间输入，严格顶点证据可支持直接末边但仍须连续检查。回归包含33568日志26笔、旋转/尺度、零/一/两raw、可见绕行、Up不一致和活跃真实末端90度角点，原8笔位置/来源必须保持。

## 阶段四正常颜色诊断合同（2026-10-13）

阶段三71858b0b已签名提交；阶段四首次正常颜色人工验收发现抬笔突然出现未绘制长斜线，根因尚未确认。后续2026-10-13用户确认鼠标/触控板、实体笔和触摸均未复现异常，要求暂缓旧事件并完成阶段四；该人工确认与日志覆盖分开记录，不得称旧事件已定位修复。前述临时默认反色/同步TEMP记录为历史状态，不能沿用为当前产品默认。

- 正常默认反色关闭、debug marker为空、没有尾轨迹I/O。只有进程环境变量精确为`INKEYS_DRAW3_TAIL_TRACE=1`才启用完成记录，首次查询锁存；不进入求解参数，不改变几何发布或颜色。Down锁存enabled，Move仅维护固定尺寸平凡数值状态，不做磁盘I/O。
- 输入诊断分别保存整笔最初contact身份/Down与当前contact身份/Down；接续保留整笔累计计数但重置当前序列差基准、旧终态及模型Up。消费快照必须在覆盖lastInput/lastConsumed之前记录，重复deferred Up不重复计数。纯值handle/generation不延长或事后解引用ContactRecord。
- 完成日志的model是拟合前realPoints，completed已过拟合+taper/公切线，stored已加canvas offset。后两种笔锋处理只改半径或在线段内部插点，不能将completed称为纯求解器输出。每套最多512点，raw64、模型边界128；完整笔划只扫描首尾、包围盒、弧长、最大边与位置hash。超出序列化尾窗时，不得凭尾窗未发现异常证明完整Stored无该线。
- 每进程128次记录、文件总量2MiB；缺end视为不完整。首写CREATE_NEW，旧PID主文件存在时用时间戳和最多8个候选CREATE_NEW，锁存成功路径后OPEN_EXISTING。不得覆盖、删除或追加旧进程日志。创建/短写/预算失败不影响完成几何；手动启动脚本和解析器必须支持后缀。
- 默认/on的确定性生产回放、真实coordinator合并/接续/Reset诊断合同验证可以证明记录机制，但不能替代实体异常复现、GPU像素、Win7或最终跨架构发布验收。没有现场真实记录前不得用远距Up或固定最大位移门猜测修复。

阶段四最终验收（2026-10-13）：四配置主/模块solution、334115生产/9245尾段/128412原模型、完成态保存/历史/等待接续、Headless与资源/静态导入对照通过，原基线hash保持。默认正常颜色且无轨迹I/O；ARM64Release完整Finalize30轮prefix0中位/P95为0.0471/0.0493ms，prefix1000为0.1464/0.1498ms，不是GPU或端到端抬笔延迟。最新人工鼠标/笔/触摸通过；旧偶发及6按用户决定暂缓，Win7/GPU Debug Layer/Release实体GUI仍未执行。
