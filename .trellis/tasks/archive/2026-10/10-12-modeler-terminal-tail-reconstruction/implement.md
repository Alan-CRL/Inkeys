# 执行计划

## 授权和依赖

- [x] 用户批准创建与开始阶段2；无本阶段commit/push授权。
- [x] 父任务关联，阶段1c34d3a56验收PASS，对照EXE/报告保留。
- [x] validate/start，派发模块与测试实现，先协调API。

## 实施

- [x] 独立模块、共享清单/过滤器及本地来源，原13源/18头31个SHA保持。
- [x] 无窗口--modeler-tail-replay与--modeler-tail-benchmark，保留冻结baseline fixtures/入口。
- [x] control与raw候选正负矩阵，原子失败、预算、有序映射、精确连接/终点。
- [x] 可复现输入/参数/效果及成本报告，测量后选择候选并说明失败边界。
- [x] trellis-check复核并修复实际缺陷。
- [x] InkeysRepo.sln Debug|ARM64完整Rebuild、Headless --no-window；demo/test ARM64 Debug及三架构Release，原API逐字段精确对照。
- [x] 三架构新几何CLI；无构建干扰的预热后成本测量。
- [x] 更新spec/父任务/日志，清理本阶段无用缓存，保留报告/EXE/关键链接证据。

## 命令约束与回退

复用Build/modeler-baseline/run-build.py，backend stage2-source/stage2-main，旧目录只读。vswhere动态定位native ARM64 MSBuild；同一PowerShell invocation保存Path、Remove Env:PATH后恢复单一Path并设MSBUILDDISABLENODEREUSE=1。构建timeout3600秒。既有sandbox C1902用获准正常host环境解决，不改flags。

原Modeler回放与阶段1.1 JSON精确相等，无.0→.1例外。产品仅构建/资源检查，不启动GUI。若候选未满足，保留负样本和报告，不放宽硬合同或修改基线；新模块尚无产品调用，移除本地扩展及登记可回到c34d3a56。后续产品阶段依赖此阶段验收。
