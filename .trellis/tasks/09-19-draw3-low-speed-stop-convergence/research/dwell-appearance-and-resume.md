# 停笔笔锋老化与续画门禁回归

基线：c88d8989；用户于 2026-09-20 明确鼠标停笔后模拟笔锋应消退为正常笔身，如顿笔，续画再自然出现笔锋。

## 已确认根因

- ApplyLiveTipTaper 以 points.back().time 为当前时间，endpoint pinned 后不产点即不老化。六个 0.3px/10ms 点、基础半径 2.5px、55ms taper 经公切线约束后末端约 1.177px；停一秒后仅续画 0.3px，旧尾段即恢复 2.5px。这个突变无需压感或速度改变。
- consumeLatestSnapshot 在 endpointAdmission.active 时每次 BeginEndpointAdmission(newRaw)；第一 Result 落后旧可见点就拒收并 Pin(newRaw)，acceptedResultCount 为零，无法满足整批通过的解锁条件。预测与 L1 被长期关闭，真实 raw 被直线硬接。
- taper/tangency 不修改中心坐标；不要把中心线位置未到与半径老化混为一谈。
- 原 f9c9ee70^ 鼠标循环逐帧 Update 同坐标，恢复后正常接 model 曲线；不必为本轮引入 Save/Restore 或重编库。
- 上轮测试多数绕过 L0 taper/L1/控制器；所谓十秒保持只重复断言点数，不执行帧循环，不能作为停止输出的证明。

## 当前约束

采用显示时钟老化、固定旧停点恢复、模型/显示时间分离；详细合同见当前 prd.md 与 design.md。旧研究中的 fully-developed completed floor 与每个恢复 raw 都做终点钉住方案已被本轮设计取代。

## 实施审查补充

- 压缩长停模型时间后，`PinEndpointGeometry` 不能把显示时间赋给 `widthEstimator.lastTime`；后续 Append 仍在模型时间轴估算基础宽度。通过独立时间轴和模拟压感长停恢复断言防止静止后笔宽估算被锁住。
- `appendTerminalFallback` 同样必须用普通笔的显示时间（不早于现有尾点）生成失败回退点，否则 Up 更新失败会把已老化尾部的时间拉回压缩模型轴。
- 固定旧停点恢复不用极窄角度锥限制正常转向：通过前向进度、速度方向和到旧停点的有界距离过滤惯性，覆盖真实 180 度与 90 度恢复。
- 诊断 `endpointError` 测量接纳的真实几何末点，而非内部模型末点；否则逐 raw 硬钉的错误实现也可能通过“存在模型滞后”的产品测试。

## 上游只读核对

核对 google/ink-stroke-modeler 的 stroke_modeler.cc、position_modeler.h 提交记录及近期 PR，未发现可直接替代本次应用层笔锋计时修复的改动。公开 Save/Restore 已在固定库头文件与本地源码存在，但本轮不需要使用。

- https://github.com/google/ink-stroke-modeler/blob/main/ink_stroke_modeler/stroke_modeler.h
- https://github.com/google/ink-stroke-modeler/commits/main/ink_stroke_modeler/internal/position_modeler.h

只读调查未运行 GUI；可见手感仍需明确区分于自动化验收。
