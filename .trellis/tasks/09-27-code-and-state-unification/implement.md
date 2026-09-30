# 执行清单

前置：baseline-and-acceptance 冻结 H0、状态回归和可测成本。出口：迁移表、跨入口测试、独立 review、完整 Debug|ARM64 Solution 证据；将接口合同交 UI3/Draw3。

- [x] 读 native-desktop、PPT、guides 规范和生产调用链；职责图、状态写读表和迁移表见 research/ 与 migration-table.md。
- [x] 识别笔型/形状裸写、宽色混代、Bar/MouseHook 普通读与异步旧结果；先记 state-ownership.md、state-snapshot-design.md 和 F-009 再实现。
- [x] 最小修改正式调用入口及已有职责文件，BOM/CRLF 与关键中文注释均保留；H0 Release 算法/输入子链已冻结，但 Bar 整帧锁等待没有无 GUI 测量，不能宣称性能无退化。
- [ ] 跨入口、快速切换、取消和迟到 PPT 的真实生产交错/GUI 验证尚未执行；独立 reviewer 已审 diff/调用链，源码层旧候选路径已修，严格无窗已有逻辑测试不直接覆盖 IdtState。
- [x] git diff --check、完整 Debug/Release|ARM64 Solution、适用 --no-window 测试均退出 0；真 UI/PPT、Win7、多架构与锁等待尾延迟保留未验证。
