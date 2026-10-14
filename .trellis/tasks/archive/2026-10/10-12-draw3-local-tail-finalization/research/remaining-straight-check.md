# Review: 短直线原始路径支持保护

日期：2026-10-13。范围：`TerminalRawSupportsPin`、其自适应调用位置，以及 33568 日志第26笔和对应反例。阶段三仍等待人工验收；本次不处理6，不运行GUI，不提交。

## Findings (fixed)

- 文件：`Inkeys/additional/ink_stroke_modeler/terminal_tail_reconstructor.cc`。
  问题：旧的严格顶点吻合检查直接返回 NoChange，绕过新加入的线段内部及双向支持验证。可见 raw 绕行但模型 pin 两端重合时仍误判吻合；真实Up与reference末点不一致时也可能被该旧分支保留。
  修复：审查发现后通知当前实现代理，由其将旧判据改为 `strict_raw_support` 证据布尔值。可见异常仍须通过新 helper 才能按 raw 支持返回 NoChange；已有严格证据仅允许可信尾部锚点位于 raw 倒数第二点，保留输入自身的直接末边。
- 文件：`inkStrokeModelerTestTests/modeler_adaptive_tail_tests.cpp`。
  问题：已有 `true_corner` 的末边超出扫描范围，无法证明新保护保留真正会触发检测的原始角点。
  修复：实现代理增加5点、活跃90度末端角点 `true_terminal_corner`，期望 NoChange；绕行与Up不一致的反例保留。

## Findings (not fixed)

- 没有在本次范围内遗留的源码问题。几何支持检验约束固定唯一 raw 顶点附近的尾部，而非整个 raw 历史；如果锚点重复或对应缺失，会继续原拟合，不凭不足证据返回吻合。
- 数值、无窗口回放和完成态probe不能替代本次人工Draw3复测；阶段三不得因此结束。6按用户明确要求继续暂不处理。

## 静态审查

- 到固定折线集合的距离是1-Lipschitz。每个源子边使用 `max(端点距离)+半弦长+epsilon` 上界，满足可见尺度才接受；否则在double坐标中取中点继续细分。两端吻合不足以掩盖中间偏离。
- model→raw与raw→model均须通过，防止原始输入绕行被单向邻近检测掩盖。helper先验证raw终点、reference终点均与真实Up一致，raw少于3点不能提供这类保护。
- 锚点仅从末端之前的raw点中选择，存在两个候选即不作为可信保护；helper之前已有位置finite和source严格有序验证。只消费该锚点后的raw连续后缀，不借前面其他分支匹配。
- 深度最多12，输入点数既有上限，所有目标线段距离计算计入既有match_work。预算耗尽不被当作吻合，外层拒绝BudgetExceeded且patch为空；无法在深度内证明上界也继续原拟合。
- helper只在异常方向突变及真实横向可见尺度触发后调用；未改变拟合、权重、扩窗、上游13TU或产品属性映射。

## Verification

- Lint：`git -c diff.ignoreSubmodules=all diff --check` 通过。仓库没有这组C++文件的独立lint入口。
- TypeCheck：既有 `inkStrokeModelerTest.sln Debug|ARM64` 的测试目标编译通过（原生ARM64 MSBuild；退出码0，23.078秒），产物SHA256 `9a913dc3872e65e693e5a31e1595d2eea478c6b670b2bc54100c4d37385ba0cc`。主产品完整solution验证由主会话记录。
- Tests：首轮9237项检查发现2项失败，正是上述旧早退绕过；修复后的最终回放 `Build/modeler-baseline/stage3-source-native/ARM64-Debug/remaining-straight-final/adaptive-replay.json` 为9245项、0失败。第26笔、旋转和三倍尺度均NoChange/空patch，零raw/仅Up/两raw仍Apply，真实终端角点保持，绕行反例Rejected(SharpCorner)，端点不一致与35次预算耗尽断言通过。
- Tests：独立完成态集成0失败，原Modeler 128412项检查通过；最终原模型报告SHA256 `0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646`。独立比较本轮修改前后前8笔实际C++输出位置数组，逐值相同。所有无窗口执行退出码0。
