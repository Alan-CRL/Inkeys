# 阶段三独立检查

检查范围：ReconstructLocalTerminalTail、产品与 demo 的 raw 缓存/完成态接入、模块测试；不改阶段二既有求解器，不运行 GUI、不提交。本次为静态独立检查；完整构建、实际回放、性能和效果图由主会话统一执行，以下不据此宣称运行验收成功。

## Findings (fixed)

- 文件：Inkeys/additional/ink_stroke_modeler/terminal_tail_reconstructor.cc，RunLocal 候选生成。
- 问题：候选坐标在 double 域计算后直接窄化为 float，再检查 IsFinite；极大 finite 输入可能超出 float 可表示范围。
- 修复：本检查指出后，主会话在转换前加入 finite 和 abs <= float::max 检查，失败走 InvalidInput 原子回退；已阅读实际修复。source manifest hash 由主会话同步。

## Findings (not fixed)

- 本次未发现其他可证明的发布/缓存语义错误。以下是设计和验收边界，不作为机械源码缺陷改动。
- 当前真实转角保护依赖选中 raw 节点和局部范围；零辅助时仅凭模型轨迹与 Up 不能可靠区分真实突然转向和人工 pin。这属于已接受的弱证据模式，仍需真实设备反例评价，不能称为识别任意真实角点的保证。
- 产品范围外前缀检查是保守的：修改后的边与前缀有交点即回退，不比较该交点是否原本存在。因此含历史交叉的部分笔划可能保持原折线；它不会因此发布新交点。调整该行为属于几何策略，不在独立检查中放宽。
- 窄化修复需主会话增量编译；检查时 LocalEdgeCases 未看到极大 finite 输入专用回归。建议补真正覆盖转换前拒绝的例子；NaN 测试不能替代此路径。
- 模块无窗口 probe 检查缓存、清终态后恢复、Reset 和 Stored 合同；GPU 重绘和接触设备端到端接续未实际运行。不能将静态 Controller 路径和 demo 回放等同于设备效果验收。

## Reviewed contracts

- RunLocal 调用先清 patch_points；所有失败使用空 patch；成功 scratch 的 span 只在下次调用前有效。应用方即时核对 range、count、token 和每点属性来源，随后复制位置，无持久引用。
- 点数硬上限及可配置匹配/验证预算先检查；finite 与 source 顺序验证先于几何使用。局部起点至少为 1，原前两点及正常前缀保留，终点硬设 Up。
- 最后 pin 边不用于模型末端方向和强形状先验；稀疏和空 raw 均不强制退出。候选发布前检查形变、长度、离散与物理转角、自交和反向重叠。
- raw 缓存是 array<64>，160ms 物理窗口，拒绝非 finite 和时间倒退；Record 在 Down/Move/Up/reconnect 的真实模型输入处使用物理 QPC，未使用模型时间偏移推断 raw。
- ClearPenTerminalState 保留缓存供接续，新输入淘汰过期点；ActiveStroke::Reset 清 count。等待接续仅绘实时尾段，FitCompletedPenGeometry 只在 FinalizeStoredStroke 的复制上调用。
- 产品应用检查范围外前缀，新修改边扫描次数有上限，失败时未写 output；已有 L1 圆胶囊属性约束得到检查。坐标拟合先于最终 taper，不回写实时 realPoints、模型 Result 或预测。
- Controller 原完成路径使用最终 Stored Stroke，清 L0/L1 后重绘完成笔划再合入 L2；未新增历史全量重绘路径。此结论为静态调用链证据。

## Verification

- Lint：git -c diff.ignoreSubmodules=all diff --check 已执行，输出无 whitespace error；有父任务文件 LF->CRLF 工作树提示。普通 git diff --check 受 HiMsg 子模块权限限制，因此仅忽略子模块检查本次 diff。
- TypeCheck/Build：本代理未执行；按协调由主会话统一 ARM64 原生 MSBuild，必须以实际退出码和日志记录。
- Tests：本代理未运行二进制；已审查新增回放、非法配置/预算、来源及成功后失败 scratch 测试和生产 probe。实际运行结果由主会话报告。