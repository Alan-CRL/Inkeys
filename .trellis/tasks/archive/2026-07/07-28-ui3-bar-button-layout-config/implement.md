# 实施计划

1. 扩展 `Inkeys.Other.Config`：通用 value codec、带锁配置序列、布局元素 codec、默认 `UI.Bar.ButtonLayout` 及维护说明。
2. 为 UI3 Bar 按钮对象增加 ID、用户可见状态和统一有效可见判断。
3. 增加按钮注册表和重复策略，给所有已初始化官方按钮与 Divider 登记 `Inkeys.Bar.*` ID。
4. 将硬编码 `Load()` 改为配置驱动：保留未知项、允许多个 Divider、过滤并规范化不可重复项。
5. 更新主栏布局、渲染、悬停、命中和锚点消费点，统一使用有效可见性。
6. 静态核对默认数组、ID 映射、重复清理、未知项保留和启动顺序。
7. 执行 `git diff --check`，使用 ARM64 host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug|ARM64`，超时至少 5 分钟。
8. 使用 `trellis-check` 复核规范、数据流与回归风险。
