# 实施与检查

1. 独立算法实现：新增局部拟合 API，保持旧 API；适配稀疏/零辅助，边界及原子失败测试。
2. Draw3 接入：固定 raw 缓存、最终 Stored Stroke 修正、断触等待/恢复生命周期测试。
3. C++ 回放报告驱动效果图，评估末端折角、正常前段偏差、形变及回退；不得用 Python 拟合代替实际 C++ 结果。
4. ARM64 原生 MSBuild，同 invocation 规范化 PATH，至少5分钟；完整 InkeysRepo.sln Debug|ARM64，独立 inkStrokeModelerTest.sln Debug|ARM64；必要 Release 回放与性能。
5. 独立检查代理审查/修复；记录验证、更新合同及任务。无 GUI、commit、push。

实现由 Trellis implement 代理分工，主会话负责协调、效果图和构建评估。已有阶段二未提交改动必须保留。失败保留原几何和原库基线。
