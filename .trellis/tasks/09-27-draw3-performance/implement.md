# 执行清单

前置：Draw3 基线口径已冻结，UI3 阶段完成且不再并行占用采样/构建资源。出口：集成 Host 独立成本分布、真实生产逻辑回归、独立 review 与完整构建；交安全/恢复收口。

- [ ] 读 draw3-integration、input、shaders、native 规范与真实 Host/RTS/test 入口。
- [ ] 建成本分布、输入轨迹/文档场景、资源趋势基线和可测/不可测边界。
- [ ] 对每个瓶颈做最小修改和真实生产逻辑回归，覆盖工具/速度/停动/取消/多接触。
- [ ] 串行采样三轮以上，复核压感、转角、笔迹、历史/PPT 页/保存；独立 reviewer 审 owner 与 diff。
- [ ] 全 Solution 构建、Draw3 适用测试及 shader/resource 链；真机笔感保留人工门禁。
