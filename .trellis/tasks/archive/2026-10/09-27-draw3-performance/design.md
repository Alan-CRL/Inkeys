# Draw3 性能设计

- 使用集成后的生产 Host/RTS/renderer/document 链路；旧 demo 只作历史线索。对输入采样、消费、Present 成功分别计数。
- 仪表按 Down/Move/Up/Cancel→队列→modeler/prediction→geometry/dirty→upload→draw/composite→success Present→history/save 划段，记录队列深度和资源趋势。
- 优先审热路径分配/复制、重算/重建、GPU buffer、dirty、擦除遍历、重放、页面切换与 cache 容量；优化前先给可重复成本证据。
- 不用 latest-only 丢必要样本；批处理必须证明顺序、时间戳、压力、多接触和 Down/Up/Cancel 不变量。
- 保持独立 device/owner、L0/L1/L2、fallback、CPU/HLSL 和 durable save；软件 Present 时间只作软件可见代理。
