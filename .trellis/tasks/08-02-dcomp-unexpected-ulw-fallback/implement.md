# DComp 意外回退 ULW：实施计划

## 实施步骤

- [x] 读取 `trellis-before-dev` 及 native presenter 相关规范，复核 `.cppm/.cpp` 契约和原文件编码/换行。
- [x] 在 `transparent_presentation.cpp` 删除 Qualcomm VendorId 特判并恢复单一 DComp-first 候选数组。
- [x] 更新 `runtime-and-rendering.md`、`platform-and-resources.md` 与 `cross-layer-thinking-guide.md`，删除 QCOM 全厂商 ULW 规则、补充兼容判定证据要求，并记录未来欢迎页透明度测试备忘。
- [x] 用 `rg` 复查主业务源码中的 VendorId/架构/OS 行为分支，确认没有同类宽泛模式降级。
- [x] 检查差异、编码和 CRLF，只保留任务范围内修改。

## 验证

- [x] 使用 ARM64 `MSBuild.exe` 构建完整 `inkStrokeModelerTest.sln`：`Debug|ARM64`，超时不少于 5 分钟。
- [x] 运行 `ARM64/Debug/inkStrokeModelerTestTests.exe` 与 `ARM64/Release/inkStrokeModelerTestTests.exe`。
- [x] 启动修改后的 ARM64 程序并检查日志：当前 Adreno X1-85 首先尝试且成功激活 `DirectCompositionVisualTree`。
- [x] 执行 `git diff --check`、`git diff --stat`、`git status --short` 和最终 Trellis 质量检查。
- [ ] 在真实桌面背景上目视确认透明结果：Computer Use 授权超时，本次未完成，不能由 DComp 初始化成功替代。

## 风险点

- 已知旧记录中一台 Adreno X1-85 曾在 DComp 成功后显示黑底；当前自动检查只能确认选中模式和 API 成功，最终桌面 alpha 仍以实机视觉为准。
- 不修改历史 baseline/research；它们描述的是 2026-07-19 当时的测试事实。
