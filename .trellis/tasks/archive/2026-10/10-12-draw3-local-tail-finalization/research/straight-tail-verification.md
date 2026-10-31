# 近直线末端误触发修复验证

2026-10-13。本轮用户限定只修复1/7近直线误修；6的小末钩无法复现，暂不处理。沿用阶段三任务，保持人工验收门，不commit/push或运行GUI。

## 修改

仅修改自适应入口可见性门：先保留anomaly检测，再用可靠入射方向与真实Up回拉向量的法向距离。删除把角截断至90°的可见性估算，复用原有visible_correction，不改拟合参数、窗口策略或其他保护。无侧向可见折线时返回NoChange和空patch，因此不会产生修正反色；没有一概禁用钝角末钩。

新增内嵌现场2484日志第55条的49个模型点与80ms原始窗口（仅Up）。该记录旧版applied、173.3°、末段4.006px，但实际侧向偏离约0.469px；新版NoChange。记录尚未与截图某一具体数字精确对应，不能将此作为所有设备上1/7问题已穷尽的证明。

## 结果

- 既有demo/test solution Debug ARM64依赖构建成功，原生ARM64 MSBuild，PATH规范化，3600秒构建时限。
- 自适应回放8421检查、0失败；现场55及水平/竖直/斜向/多边/亚像素侧偏倒退均NoChange、空patch。
- 可见斜向钝角回拉正例仍Apply；正常无raw圆弧NoChange。
- 之前8笔case的完整JSON（位置、来源和诊断）与inward-side版本精确相同。
- 完成态集成0失败，原Modeler19case/128412检查通过，baseline SHA256仍为0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646。
- 完整InkeysRepo.sln Debug ARM64 Build退出0。产品SHA256：0a4ac8550a56927f0ccb733c21f089aca53ad18130907d1af86f40407e41743a。
- 新产品`--draw3-pen-taper-test`退出0，334062断言、0失败。

证据：`Build/modeler-baseline/stage3-source-native/ARM64-Debug/straight-tail-fix/`及`Build/modeler-baseline/stage3-main-native/ARM64-Debug/straight-tail-product-probe.log`。

本轮只更新Debug测试产品；未重建Release或其他架构。未重复与本次求解器变更无依赖的Headless全套（上一轮已通过）。反色保留供人工复测；阶段三不结束。
