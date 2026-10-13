# 临时尾段反色独立检查

2026-10-13。检查范围：Draw3.InkDocument.cppm/.cpp、Draw3.StrokeGeometry.cpp 的本轮增量。阶段三仍进行中，人工反馈无可见改善，尚不能确定设备端根因；本检查不把合成通过当作人工验收。

## 结果

没有发现需要直接修复的确定逻辑缺陷，未修改产品源码。

- FitCompletedPenGeometry 只在 solver Apply、token/range/source、笔宽和稳定前缀相交/工作预算全部通过后写回位置。只有位置确实变化才返回首个改变点前一条边的起点，不因小于 0.1px 而隐藏标记，不强迫求解成功。
- marker 由 FinalizeStoredStroke 携带到内存 InkStroke，不改 StoredInkStyle/point 属性；普通笔之外不设置标记。构造函数拒绝无效段索引。
- 完成态 L1、历史 tile/重绘都进入 DrawStoredStroke；拆分的点范围共享一个连接点，线段不重复。begin 在边界及之后的范围反色，边界之前的范围保留正常颜色；首尾裁剪仍复用既有范围规划。
- 颜色只变成 1-R/1-G/1-B，alpha 原值传给正式 renderer 常量缓冲。未改 shader 或 GPU 结构。
- taper 插点发生在边界之前或边界点处时同步后移 marker；插在第一条改变边内部时保留该边起点。局部 tail 的 marker 转换包含 tailStart；marker 位于更早已提交范围时不用转换。后续 EnforceCapsuleTangency 改半径不改点数。
- 恢复构造仅传原 Style/Points 时不携带诊断元数据。标记不是永久笔刷颜色；重新加载后无反色属于预期诊断边界。

## 验证与边界

- git diff --check（忽略子模块）exit0。
- 生产 probe 已新增 applied marker、调用方笔宽拒绝无 marker、solver 拒绝无 marker、非笔无 marker、原 style/opacity、恢复后无 marker、裁剪范围拆分和 RGB/alpha 检查。
- 主会话报告完整 ARM64 Debug solution 与实际 --draw3-pen-taper-test 通过。本检查不重复并发构建；C++ 编译作为类型检查，无独立语言 lint 工具。最终构建/Headless 日志由主会话补入验收报告。
- 建议补充 taper 插点边界索引的直接 probe，以防后续更改破坏映射；当前已有代码推导正确，但没有专项插点回归。
- 当前逐笔原因输出为 OutputDebugStringA。它只面向调试器/相应采集工具，不等于持久日志；若需用户返回拒绝原因，需说明采集方式或另加受控日志。
- 没有启动 GUI/GPU/实体笔；两段不同颜色在胶囊连接点处的真实像素混合、用户笔色及背景下可辨性尚待人工验证。

## Findings（未修复）

设备反馈无改善根因尚不确定。不得修改阈值来强迫反色；需要本次反色版的人工结果与决策原因判断是否触发、被拒绝或者形变量过小。本项保留待验证。

主会话补充验收：审查建议的两类 taper 插点映射测试已加入实际生产 probe 并通过，最终331608断言0失败；外部 Headless exit0。
