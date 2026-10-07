# 执行清单

前置：baseline-and-acceptance 冻结 H0、状态回归和可测成本。出口：迁移表、跨入口测试、独立 review、完整 Debug|ARM64 Solution 证据；将接口合同交 UI3/Draw3。

- [x] 读 native-desktop、PPT、guides 规范和生产调用链；职责图、状态写读表和迁移表见 research/ 与 migration-table.md。
- [x] 识别笔型/形状裸写、宽色混代、Bar/MouseHook 普通读与异步旧结果；先记 state-ownership.md、state-snapshot-design.md 和 F-009 再实现。
- [x] 最小修改正式调用入口及已有职责文件，BOM/CRLF 与关键中文注释均保留；H0 Release 算法/输入子链已冻结，但 Bar 整帧锁等待没有无 GUI 测量，不能宣称性能无退化。
- [x] 2026-10-03续接已用实际IdtState/bridge和BarInteractionSession补跨入口、重复/快速切换、受控迟到请求、FineDial物理tick/跨代/cancel/失效Commit；最终Debug/Release ARM64 CLI实际exit0，独立复审完成。该项只确认逻辑交错；真实HWND/RTS/Office和成功FineDial保存仍有边界，见closeout-20261003.md续接节及两份state-production-verification报告。
- [x] 本轮最终五文件版本 git diff --check、完整 Debug/Release|ARM64 Solution、新状态CLI和既有Headless均通过；实际日志在Build/automation-perf/release-closeout-resume-20261003，独立复审见research/state-production-verification-review-20261003.md。旧像素相等证据只用于原渲染算法（本轮未改）；真实Office/可见手势副作用、Win7与呈现尾延迟仍未全验，不与已PASS的受控条件交错混淆。
