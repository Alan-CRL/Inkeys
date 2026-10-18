# Research: Google Ink 新平滑实现的有限核查

- Query: 原库 README 提及迁移到 newer input-smoothing，是否提供本问题值得比较的替代机制？
- Scope: external；仅核查算法与接口边界，不开展整库迁移或兼容性承诺
- Date: 2026-10-12
- Version: `google/ink@b6def591a73a6aa0ffce1e58dc69b39de31484ba`，查询时 main，提交时间 2026-10-08T01:22:47Z

## Findings

### 1. 官方迁移线索属实

Ink Stroke Modeler 当前 [README](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/README.md#related) 明确说明 Google Ink 原先依赖该库，后来迁移到新输入平滑实现；这条说明的 [2026-03-10 提交](https://github.com/google/ink-stroke-modeler/commit/da42d439389c90ec7574f0381ec53e7f5be0c2eb) 是文档变化，不是旧 modeler 的修复补丁。

### 2. 新实现具备可重建的尾部

`SlidingWindowInputModeler` 接收 real/predicted 输入批次，保留原始窗口和 stable/unstable 分界；后续输入到来时重建未稳定部分。头文件解释稳定边界在最后真实输入之前半个窗口。这比旧库持续追加不变 Result 的消费合同更接近“可修改近期尾段”。[头文件](https://github.com/google/ink/blob/b6def591a73a6aa0ffce1e58dc69b39de31484ba/ink/strokes/internal/stroke_input_modeler/sliding_window_input_modeler.h#L44)

实现按对称时间窗口平均分段线性输入，临近起终点时同时缩小窗口；端点窗口退化为零后取输入位置。这里没有旧位置弹簧积分，因而不会从该机制产生旧式惯性回摆；但去重 epsilon 可能省略近点，不能无条件承诺最后一点逐位精确。原始窗口不足或曲线稀疏仍会有裁角/平滑偏差。[位置与端点处理](https://github.com/google/ink/blob/b6def591a73a6aa0ffce1e58dc69b39de31484ba/ink/strokes/internal/stroke_input_modeler/sliding_window_input_modeler.cc#L258)

### 3. 与 Inkeys 的接入差异

`StrokeInputModeler` 通过 brush 的 input-model 配置选择 passthrough 或 sliding-window。它接收真实及外部预测批次，并在每次扩展前删除旧预测结果，没有旧 `StrokeModeler::Predict()` 接口。[包装层](https://github.com/google/ink/blob/b6def591a73a6aa0ffce1e58dc69b39de31484ba/ink/strokes/internal/stroke_input_modeler.cc#L31)

因此不能只替换现有 merge.lib：模型输出增量消费、预测来源、稳定游标、原始尾部缓存、压感/速度、物理 Up 锁定和保存合同都要适配。路径在 `internal/`，本次未验证其独立稳定 API、Windows 工具链、现有三架构 ABI 或性能。

## 研究建议

把它加入同轨迹离线比较，或借鉴“窗口内重建 + 稳定前缀 + 端点窗口缩小”的思路，作为现有库参数对照仍无收益时的另一候选。它使运动和收尾使用同一算法边界，值得比较，但没有找到明确承诺修复 Inkeys 这次大曲率 Up 弯折的记录，不能把 Google 已迁移等同于本产品已找到验证有效的替换方案。

若后续继续此方向，应复用综合报告的曲率/采样/末端角度矩阵，并单列输入预测移除时的尾段变形、端点 epsilon、短划、压感、固定 L1 和 Stored 一致性。这只是后续实验提案，不授权迁移或改代码。

## Caveats / Not Found

- 本次仅阅读固定 SHA 的三个源文件、README 与迁移文档提交；未对 Google Ink 全库、发行与平台支持作审计。
- 未编译、运行新模型或做两模型同轨迹比较；“无旧式弹簧惯性”是从算法推导的差别，不能推出“没有任何转角或内外偏差”。
- 主会话新增此补充研究；上游子报告的未扩展声明仅指它自身调查范围。
- 相关本地约束见 `research/recommendation-and-validation.md`、`.trellis/spec/native/runtime-and-rendering.md:394-462`。
