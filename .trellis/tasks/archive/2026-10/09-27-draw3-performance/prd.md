# Draw3 性能专项

## Goal

集成 Draw3 的输入到成功呈现全链路调查、最小优化与对照

## 依赖与门禁

入口：依赖 baseline-and-acceptance 的 Draw3 轨迹与计时口径、UI3 阶段完成并释放构建/采样资源；不得共用 UI3 结论。出口：Draw3 独立成本分布、输入/持久化语义复验、独立 review 和完整 Solution 构建完成，交安全/恢复集成阶段。

## Requirements

- 独立分析集成后的生产 RTS/输入、队列、状态门、modeler/prediction、几何/dirty、上传、绘制/合成、成功 Present、history/persistence；区分采样、消费、呈现率。
- 测量 Down→首次成功 Present、持续延迟/积压、Up→最终稳定、帧分位数、CPU/GPU 与长文档资源；不把逻辑提交或软件计时冒充像素/光学延迟。
- 保留必要输入样本、Down/Up/Cancel、顺序、压力、转角、笔感；覆盖笔/荧光笔/橡皮/激光/开放形状、慢/快/折返/停动/抬笔/取消/切工具/多接触。
- 保留 UI3/Draw3 独立设备线程及 L0/L1/L2、PPT 页身份、fallback、resize/DPI、device-lost、CPU/HLSL、premultiplied alpha、durable UInk 合同。

## Acceptance Criteria

- [ ] Draw3 有独立成本分布、资源趋势、三轮以上可比前后数据与未测真机边界。
- [ ] 每个优化保留输入/文档语义，有生产逻辑回归、独立审查与完整构建证据。
- [ ] UI3 结果不外推给 Draw3；仅超过噪声的收益计为性能提升。
