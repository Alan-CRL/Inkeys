# UI3 性能专项

## Goal

动画、光影、SVG/路径和呈现链路的归因、最小优化与对照

## 依赖与门禁

入口：依赖 baseline-and-acceptance 的 UI3 口径与 code-and-state-unification 的状态合同、阶段构建；未确立可比样本不能声称提升。出口：UI3 独立成本分布、优化/噪声结论、相关测试、独立 review 与完整 Solution 构建完成，才进入 Draw3 写入阶段。

## Requirements

- 沿生产输入/唤醒、snapshot、target/layout、动画、资源、绘制、GetDC/ULW/EndDraw/Present、等待分段，调查 scheduler 竞争、重复工作、锁、首次 cache、光影、SVG/path、资源失败与日志背压。
- 核对既有动画诊断/修复与 raw dt、animation dt、Rebase、clamp、快速反向、同 target no-op、隐藏、设备重建和失败呈现事务。
- 以证据减少无效布局/推进/重绘/重建；检验 dirty 含旧范围/光晕/AA，缓存键、容量、设备 epoch 与失效；不降低画质、帧率、光影或关闭动画。
- SVG/path 分开量解析、几何、细分/描边、raster/upload/draw；不因已有 cache 就跳过。

## Acceptance Criteria

- [ ] 有 UI3 独立原始/汇总指标与至少三轮可比前后对照；成功帧与回调分开。
- [ ] 每个优化有假设、最小实现、确定性时间/失败路径测试、独立复审与收益/噪声结论。
- [ ] HC/Inkeys2 真机不可比部分保留人工门禁；保留 opt-in 低开销诊断。
