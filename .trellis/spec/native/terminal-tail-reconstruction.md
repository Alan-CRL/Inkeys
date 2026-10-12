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
