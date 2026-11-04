# Bug Analysis: 三态动画节拍与容量追扩

2026-10-28。本记录对应已通过短测的保守版本，完整矩阵/构建/像素及退出码以最终实施报告为准。

## 1. Root Cause Category

- **E / Implicit Assumption**：共享Scheduler使用普通sleep_until，却隐含依赖其它绘制模块的系统timer period。Draw3选择态会释放period，离屏也没有Draw3；绘制<15ms的251组样本，下一批开始仍P50=31.13ms。复用独立waitable timer后实际成功提交间隔明显改善。
- **E / Implicit Assumption**：把宽高变化/世界坐标viewport包络与相对移动锚点的对称存储容量视为同一件事。主栏根可由居中和displayCenterX/Y逐帧SetDirect派生，当前root的tar不再代表真实驱动终点；重复计算current reach会追扩。
- **D / Test Coverage Gap**：固定16ms Step证明几何，不证明真实节拍；预热后的96组旧轨迹无扩容，也不能排除首次/新峰值。成功测试总exit0不自动证明sink内保存的每张attempt图片完整成功。

## 2. Why Fixes Failed

1. 仅收紧隐藏面板容量：面积减少，但当前viewport相对移动root的reach继续变大，导致一个转场多次重建。保留为失败候选，没有把进程exit0误当性能达标。
2. 扩大横向future root预测：Centered预测门与显示/工作区交接门交错，SetDirect丢端点的路径仍可达到，首D→P继续约19–22次增长。继续扩展预测模型会增大布局耦合，不符合本任务最小修改目标。
3. 最终收敛：横向保留原有对称安全预约和既有余量，宽度依据整段实际曲线极值；纵向不为从未打开的面板预约整套最大高度，结合真实displayCenterY段预留。短测首pending placement允许一次新目标增长，其余方向/第二轮零追扩。
4. 仅修timer：提交节拍改善，但GetDC/绘制批次仍有超预算，不能据此宣布完成全部性能验收。真实batch与成功间隔分别报告，cold/hot/all样本均保留。

## 3. Prevention Mechanisms

| 优先级 | 机制 | 本任务措施 |
| --- | --- | --- |
| P0 | 真实生产调度回归 | 既有离屏入口接真实Scheduler及公共Bar/PageControl，用有效生产commit stamp，按idle/epoch/generation/serial断链 |
| P0 | 资源行为失败断言 | 按每次未重定向转场核实际target尺寸，禁止持续追扩；首次pending placement一次增长单列 |
| P0 | 正确性事务 | 已知root移动在Observe前全校准；late纯容量/映射仅扩大完整paint/ULW，保留未观察稳定bounds及失败全重试 |
| P1 | 像素证据身份 | 图片逐一关联该轮成功CSV/tuple；缺少关联的图片不混入有效分母；1LSB稀疏差异与bit-exact区分 |
| P1 | 测量环境 | 同机器/环境/默认光效、各版本独立EXE哈希与原始样本；无性能采样期间编译或重负载 |

## 4. Systematic Expansion

- 公共Scheduler的等待修复自然覆盖其Bar/分页/Setting客户端，不为每个client另设节拍器。
- 原则上所有SetDirect派生几何都应从真正驱动读取最终范围；本轮只解决容量所需接缝，不顺带重构其它布局或所有SVG缓存。
- 白板Freeze、背景ACK与SVG重复栅格仍是静态可达成本；没有本轮完整现场归因，不扩大到这些模块。
- C1902与headless wrapper超时/丢exitcode是验证环境问题，与源码错误分开记录；不改工具链/PDB设置绕过，不把内部PASS代替未取得的OS退出码。

## 5. Knowledge Capture

- 已同步 `.trellis/spec/native-desktop/rendering-and-ui.md`：非自旋独立timer/原deadline回退、稳定横向预算、纵向真实驱动范围、早期Observe与晚期完整清绘分工。
- 已同步 `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：真实Scheduler转场负载、cycles/倍率/三轮、有效stamp、cold/fresh/native/Freeze覆盖限制和attempt像素的证明边界。
- 该仓库没有需要本任务同步的Trellis上游模板；不创建额外模板目录，不修改第三方或其它任务。用户未授权commit/push，未执行。
