# Implementation Order

1. 调整 Setting 页导航、分组顺序、条件显示、文案、局部状态和动态高度。
2. 删除废弃配置的活动读写，并在传统配置保存快照中清理旧 JSON 键。
3. 更新简中资源，运行 i18n 同步，再补齐繁中和英文并执行检查。
4. 静态搜索删除项残留，执行 `git diff --check`。
5. 使用 ARM64 原生 MSBuild 构建 `InkeysRepo.sln` 的 `Debug | ARM64`，运行 `InkeysHeadlessTests.exe --no-window`。
6. 由 Trellis 检查代理审查需求覆盖、配置数据流、i18n 和验证结果；任务保持活动，不 commit、不归档。
