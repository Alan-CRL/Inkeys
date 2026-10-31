# 阶段2实施批注与初始检查

2026-10-12：用户已审阅阶段2计划，明确“创建阶段二子任务，并批注开始实现”。task.py create --parent关联父任务后，准备PRD/design/implement与筛选context，经validate成功后start，status=in_progress。阶段1c34d3a56门已满足。

实现分工：独立本地求解器/共享清单，离线回放/计时，独立checker。没有产品调用、GUI、commit或push。此前新agent容量错误在未完成实现前发生，复用已有实施agent继续，不切模型、不改变范围。

API保持点数，固定前两个非退化参考点和真实endpoint；caller属性来源与raw几何对应分开；半开范围及baseline token；复用scratch，失败patch为空。raw物理时间保留在调用方报告，几何求解不从display time推断对应。默认参数是实验起点。

首次静态检查已发现并要求修正：

- raw数据权重乘(n-1)造成点数相关的有效正则化；改为弧长积分权重，后续用实测选λ。
- 已pin真实Up时endpoint-reference.back为0会令端点对照失去比较意义；weak terminal需明确可靠尾段到Up的平滑连接处理。
- raw重复顶点会使相邻Angle退化为0而漏掉真实尖角；检查应跨重复点保留真实转向证据。
- 仅检查末端转角不足以保护入口后新增折弯/相邻反向；增加对应门禁与独立物理弧长指标。
- uint64来源顺序不能经double插值核验，>2^53会丢精度；保留整数区间/fraction与原来源精确核对。

检查/编译/效果结论尚未完成；以上是实施决策记录，不能作为PASS。基线EXE/报告和13上游cc/18头共47个artifact hash已冻结于Build/modeler-baseline/stage2-evidence/frozen-baseline.json。离线accepted gate明确为模拟，不等于真实产品helper或实体笔轨迹。

## 首轮实际验证

ARM64 Debug首次Rebuild104.14s exit1：新测试私有Distance与导入ink::stroke_model::Distance发生ADL重载歧义，只改新TU函数名。监督脚本增量目标project:Build不受solution支持，改为solution project目标，错误日志单独保留；没有修改工程配置。随后增量44.20s exit0，原19case回放SHA与阶段1完全相同0119fd2a…02b646。31个上游文件hash保持、两消费者14TU、同包Abseil头和无旧merge检查通过。

首轮tail replay：96窗口，默认10Apply/4独立物理末钩改善，但共2failure：loop两预测器55ms窗口，sweep raw_weight4/lambda.003的full max physical turn从1.1035620469变1.6336079852，增加.530超.35研究硬门。该轮exit1，未通过；first-tail-replay-failed.json保留。公开四点相邻重叠回归通过。核心补固定物理弧长全窗检查并计入预算，不放宽原测试；sweep汇总还需纳入所有硬形状失败，避免局部eligible掩盖失败。
