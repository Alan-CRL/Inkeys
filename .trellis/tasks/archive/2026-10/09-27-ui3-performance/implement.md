# 执行清单

前置：baseline-and-acceptance 与 code-and-state-unification 的口径/状态合同、阶段构建通过。出口：UI3 独立原始数据、优化/噪声结论、独立 review 与完整构建；释放采样环境供 Draw3。

- [x] 复核 08-09、09-22、08-11 等历史任务与现有生产 diagnostics；见 research/current-cost-triage.md、svg-path-review.md。
- [ ] 冻结 UI3 场景/口径，串行采样，区分冷/首/稳/长期和多轮离散度。
- [ ] 定位最大可归因成本，逐单元最小修改、确定性测试、前后对照、独立复审。
- [ ] 核对失败呈现、设备重建、快速反向、cache invalidation、光影/SVG/path 视觉等价。
- [x] 全 Solution Debug/Release|ARM64 构建与适用 no-window、生产无 HWND WARP/D2D 离屏测试；真机 UX/ULW 保留人工门禁。

2026-09-28 自动阶段交接：exact-mask 整数平移候选三轮离屏测得局部成本下降，但 BGRA 有 451 像素差异，已撤销生产优化；F-004 idle 时钟、F-019 Scene 缓存和 F-021 底栏内存序是正确性修复，不宣称性能收益。SVG/path 有生产资源链调查但尚无 parse/raster/upload/draw 分段时间；全窗口 GetDC/ULW、长时间资源趋势、Canary/Inkeys2 同机对照与 UI 真机交互均未验收，上方相关项继续未勾选。当前工作阶段可交给 Draw3，UI3 Trellis 子任务仍为进行中。
