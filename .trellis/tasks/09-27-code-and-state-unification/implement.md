# 执行清单

前置：baseline-and-acceptance 冻结 H0、状态回归和可测成本。出口：迁移表、跨入口测试、独立 review、完整 Debug|ARM64 Solution 证据；将接口合同交 UI3/Draw3。

- [x] 读 native-desktop、PPT、guides 规范和生产调用链；职责图、状态写读表和迁移表见 research/ 与 migration-table.md。
- [x] 识别笔型/形状裸写、宽色混代、Bar/MouseHook 普通读与异步旧结果；先记 state-ownership.md、state-snapshot-design.md 和 F-009 再实现。
- [x] 最小修改正式调用入口及已有职责文件，BOM/CRLF 与关键中文注释均保留；H0 Release 算法/输入子链已冻结，但 Bar 整帧锁等待没有无 GUI 测量，不能宣称性能无退化。
- [ ] 跨入口、重复/快速切换、取消和迟到 PPT 的确定性生产交错验证仍待补；2026-10-03 正常可见主栏/工具/属性操作已有无异常记录，不能替代指定交错。独立 reviewer 已复核原修正及 `0a19182c`；严格无窗已有逻辑测试不直接覆盖 IdtState，最小自动补证入口见 closeout-20261003.md。
- [x] git diff --check、完整 Debug/Release|ARM64 Solution、适用 --no-window 测试已有退出 0 记录；最近实际构建/像素证据见父任务 automation-20261003.md。指定 PPT/手势交错、Win7 与锁等待尾延迟保留未验证；新增补证后须核最终实际版本，不据旧构建宣称新检查通过。
