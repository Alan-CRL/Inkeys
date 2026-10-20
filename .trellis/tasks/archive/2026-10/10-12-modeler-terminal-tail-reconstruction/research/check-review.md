# 阶段2独立检查记录

2026-10-12。检查范围：独立 terminal_tail_reconstructor.h/.cc、共享编译登记、新无窗口 CLI/test TU，以及 ARM64 Debug 实际回放。未修改前向模型、产品调用路径或依赖版本。

## 已发现并修正

- 相邻重复 raw 顶点会使 Angle 退化而遮蔽真实尖角：实现方增加 distinct_raw_indices，保留原 raw 索引和 source，在不同位置间检测真实转向。实际 real_corner/duplicate_corner × 两预测器 × 三窗口共12组均为 sharp_corner。
- 前两个点固定不能单独证明第三点连接与中段形状：实现方增加原顶点转角门、相邻反向重叠检查及固定物理弧长完整窗口转角门。首轮两条 loop55ms sweep 的 full turn 增量约.530，超过原.35门；保留失败报告，增加核心检查，没有放宽测试。
- 数据项多乘 n-1 导致有效正则化随节点数量变化：实现方改用非均匀弧长梯形权重，只正则位移场，不直接压平参考曲率。当前只是有界迭代求解，不宣称全局最优或任意采样完全不变。
- 测试通过 double 比较来源 ID 会损失大整数精度：改为整数区间/fraction 顺序，并逐项检查 source 位模式保留；增加 >2^53 成功等价及相邻整数反序拒绝。
- 测试 sweep 汇总不能仅看效果及负样本：现在独立硬检查失败亦计入 hard_check_failure_windows，任何此类失败都会取消研究配置资格；上下文包含 fixture、窗口、权重与单位。
- Project 在闭区间 lo=rawTotal 时 upper_bound 跳过最后有效段的 t=1，重复 Move/Up 末零长边导致 quick_curve 假 AmbiguousMatch：仅 Project 改 lower_bound，AtArc 保持 upper_bound。新增公开 API 回归覆盖两预测器、三窗口、两候选、末端去重与内部重复共24对；比较 outcome、补丁位置/来源和匹配几何，而不强求变化的段下标/工作次数一致。
- 检查方补全新 physical turn 诊断序列化；测试同样保留 physical_turn_spacing、单位缩放、非法间隔、极小间隔和超长路径预算边界。

## 最终代码检查

入口前两点与 endpoint 是赋值硬约束，不靠有限大权重。源 ID 顺序不经浮点数转换；caller 属性来源与 raw 几何匹配分离。点数、工作和迭代都有最大限制。物理采样在 size_t 转换前检查有限数量及剩余预算，逐样本 Spend，末样本精确使用 endpoint；极小 spacing 不会形成无限循环。结果仅在全部门禁后写入 patch，失败清空旧 patch；成功后复用 scratch 的 span 生命周期在接口中明确。

原13上游运行 TU及18原头共31份 hash 与冻结基线精确一致；连同16份旧基线报告/EXE，共47份冻结 artifact 重算变化0。共享14TU只多本地扩展。CLI 在窗口/GPU 分派前返回。没有伪造 Result 的导数、压感或原模型状态。

## 已验证的实际证据

- Build/modeler-baseline/stage2-source/ARM64-Debug/tail-replay.json：最终四配置各96窗口，116105断言，failures=[]；默认18 Apply，12独立物理末钩改善。改善来自 circle/quick_curve × 两预测器 × 三窗口。
- 默认另6条 Apply 属于 loop/self_cross，均通过当前负样本研究限额和独立形状门；不能把这6条算作末钩改善。
- 9组预声明权重组合：全部 hard_check_failure_windows=0、negative_research_safeguard_damage=0；其中8组达到研究效果资格，raw_weight=.25/lambda=.3没有改善，不合格。
- 24组重复 raw 等价回归通过；tiny_spacing_budget/huge_path_budget 的两候选均明确 BudgetExceeded，nonfinite/zero spacing 均 InvalidInput；原子失败及来源大整数检查通过。
- 原 API 回放 SHA-256 为 0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646，与阶段1完全相同；19 case/128412 assertion 保持。
- git diff --check exit0。主会话已完成 ARM64 Debug demo/test 与主 solution 编译、最终增量及 Headless --no-window exit0，并检查 shader 301-304 资源；检查方未启动重复构建。

## 限制与后续验证

最终 ARM64 Debug/Release、x64 Release、Win32 Release 全部回放通过；三个 Release 架构的最终预热成本矩阵亦完成。逐项读取原始 JSON 复核，具体结果见 verification.md，不能将模拟器成本外推原生 x64/Win32 或产品帧性能。没有 GUI、实体笔、D3D运行期或真实 Windows7 验证。

accepted 几何是明确的离线 distance-decrease-then-pin 替身，未调用真实产品接纳/L1/Stored/taper/reconnect。0.5输入单位采样及.35弧度增长限额、负样本.1弧度/.25单位限额是可复现研究合同，不能宣称通用视觉安全。两预测器在当前最终消费几何上相同，并非两个独立的设备证据。自交检查只看到传入尾窗；未来调用方必须核验稳定前缀/完整轮廓关系、baseline token、可改范围、属性重算及原子应用。

注意 endpoint-safe 基线的无改善返回 NoChange；代码对尚未到真实 Up 的基线允许先完成 endpoint repair（通过硬门但可不满足额外末钩改善），不应将这一情况称为算法效果改善。已建议主会话同步 spec 的无改善措辞，无需改求解器。

截至此次最终代码/Debug证据检查，没有另行发现需要修改的实际代码缺陷。未commit/push。

## endpoint repair 正向覆盖补充

主会话批准后新增五点直线的公开 API 用例：reference 最后点(4,0)尚未 pin，raw 真实 Up=(4,.2)。两候选必须 Apply，前两点/真实 endpoint/来源/范围/token 精确、全部位置 finite、输入不变；不断言末钩改善。核心与参数未改，最终四配置的增量/回放结果由主会话补入 verification.md。

## 最终四配置与成本复核（收尾）

逐项读取四份最终 tail-replay.json：均96窗口/116105断言/0失败，默认18 Apply/12物理改善；新增非pin endpoint repair两路径均Apply。四份原API replay SHA均0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646，再次重算47冻结artifact变化0。

三份最终 tail-benchmark.json 均24case/8504检查/0失败，16预热/256测量。原生ARM64普通窗口median .1733–.1918ms、p95 .23115–.296575ms；512点压力候选预算拒绝median4.11485–4.2632ms、p95 5.1763–6.267575ms。x64/Win32运行于ARM64机器，报告明确模拟执行边界；Win32压力median51.9781–58.42035ms如实记录，不当作原生性能结论。工作预算不是墙钟deadline。

verification.md和algorithm-selection.md的默认覆盖/权重资格/效果样例及成本表与实际报告一致。cross-architecture-numerics.md区分同架构原API精确对照与跨架构新增诊断差异，明确公开raw_fraction并非跨架构bit exact、不隐藏Win32 stylus 13ULP样例，未据此调整浮点合同。当前矩阵1920次默认/扫描调用的状态/点数/来源/位置相同不等于全输入确定性；保护前缀/产品宽度taper/真实采样仍待接入验收。

最终独立代码和证据检查未发现未解决的实现缺陷；lint/diff通过，既有solution编译及无窗口报告由主会话生成并复核。此记录只更新检查文档，无新增代码、构建、GUI、commit或push。
