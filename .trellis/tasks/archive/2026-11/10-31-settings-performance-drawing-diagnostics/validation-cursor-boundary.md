# R11 CopyButton末项边界修复验证

日期：2026-11-01。

## 根因与修复

新增CopyButton使用SetCursorScreenPos恢复横向起点时，Y保留了按钮之后的ItemSpacing；ImGui的CursorMaxPos不含该spacing。若按钮是正文child的最后一项，EndChild→End检查IsSetPos后认定未登记内容边界，触发用户截图相同的报错。

共用helper改为正offset的临时Indent/Unindent，offset=0跳过，删除首尾SetCursorScreenPos。没有新增Dummy或纵向间隔，没有清内部旗标，也没有关闭assert/tooltip或修改第三方ImGui。标准尺寸/右齐、按钮LastItemData、原native间距、完整复制行为保持。

## 红/绿证据

| 步骤 | 结果 | 记录 |
| --- | --- | --- |
| 只加严格回归，坏helper不变；solution Headless目标编译 | 退出码0 | validation/cursor-boundary/red-test-build.* |
| --setting-attention-only --no-window（坏helper） | 退出码3，真实EndChild打印与截图相同原始message及imgui.cpp:10837断言 | validation/cursor-boundary/red.* |
| 最小helper修复后完整InkeysRepo.sln Debug / ARM64 | 退出码0，0错误 | validation/cursor-boundary/green-build.* |
| --setting-attention-only --no-window（修复后） | 退出码0，所有CopyButtonWindowEnd矩阵errors=0 | validation/cursor-boundary/green-attention.* |
| --setting-copy-only --no-window（修复后） | 退出码0，旧完整复制/布局与严格末项均通过 | validation/cursor-boundary/green-copy.* |
| 主代理静态复核、BOM/CRLF、git diff --check | 通过 | 本轮工具输出 |

严格回归真实执行EndChild/End，无后续标题或Dummy清标记；覆盖root/固定child/AutoResizeY、滚动/裁剪、非零7.5 DIP ItemSpacing、外层缩进、恰好/小于按钮自然宽度及全部Runner DPI。保持原assert/tooltip配置，检查真实ErrorCallback/ErrorCount和按钮ID/标准Y推进。

根因调查见research/cursor-boundary-investigation.md，break-loop分析与规范同步见research/cursor-boundary-analysis.md；没有扩大修改SettingRow/NavigationRow等已正确预登记边界的路径。

## 边界

仅改共用CopyButton与现有无窗口回归，不改设置/绘制/格式化逻辑，不改工程、依赖或全局配置。未启动应用/设置GUI、Computer Use或hidden HWND fixture。未创建commit、push或自动归档；历史HomeWARP限制保留，未重复无关基准。
