# 运行时重做与分支丢弃

## Goal

在现有尾部撤回、热前像和分块合成树上增加每页独立的运行时重做。用户按数字键或小键盘 `6` 能恢复最近撤回的 Stored Stroke；撤回后成功写入新 Stored Stroke 时，旧重做分支立即变为不可达。

## Requirements

- `6` 与小键盘 `6` 发布 Redo Canvas command，忽略自动重复，并与撤回/翻页一样等待活动 contact 全部结束后按 FIFO 消费。
- 每个 `CanvasRuntimeHistory` 保存独立的 LIFO redo 栈。成功 Undo 压入候选；成功 Redo 弹出候选并恢复 visibility、可见链、Tile 引用和 composition generation。
- 新 Stored Stroke 成功追加到 `InkCanvas` 后立即清空当前页 redo 栈；Laser、Cancelled、翻页、Resize 和 viewport 移动不清空。
- 正常 Redo 复用 Stored Stroke renderer 直接局部绘制，并重新捕获当前 L2 作为该项新的热撤回前像；不增加 redo 后像或新 GPU 资源。
- L2 不完全清晰时，先通过现有 composition cache/rebuild/ordered replay 恢复候选影响区域的隐藏态背景，再直接绘制候选。
- GPU 恢复、栅格、resolve 或 visibility 提交失败时，候选保持隐藏且仍可重试；可能被部分修改的 L2 必须恢复或进入权威刷新。
- Redo 不改变 viewport。屏外候选只更新运行时 visibility，后续视口恢复再显示。
- 旧 redo 分支只做逻辑丢弃，保留现有 append-only Stroke、RenderItem ID 和合成树索引；不修改持久格式或 HLSL。

## Acceptance Criteria

- [x] `A/B/C -> Undo C/B -> Redo B/C` 恢复原可见顺序，空 redo 明确 no-op。
- [x] Undo 后写入新 Stored Stroke 会清空旧 redo，随后按 `6` 不恢复旧分支。
- [x] 每页 redo 相互隔离；翻页、Resize、小数 viewport 和屏外笔画不会错误清空 redo。
- [x] 正常 redo 使用局部直接绘制并重新建立热撤回前像；不做 readback、GPU wait 或双向后像缓存。
- [x] 不可信 L2 使用合成树/局部顺序重放补齐背景；任何失败不提前提交 CPU visibility。
- [x] `[Redo]` 日志包含 page、item、背景恢复路径、直接绘制路径、热前像状态和剩余深度。
- [x] ARM64 Debug/Release 完整 solution 构建和两套无窗口测试通过；`git diff --check`、UTF-8 BOM/CRLF 检查通过。

## Out Of Scope

- 隐藏分支的物理压缩、UInk 导出和跨进程持久化。
- Ctrl+Y、UI 按钮、多图层命令历史和非 Stroke 编辑。
- 新 HLSL pass、新 GPU 资源或 redo postimage cache。
