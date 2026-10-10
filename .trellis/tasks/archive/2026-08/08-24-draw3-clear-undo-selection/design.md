# Design: Draw3 清空撤回与选择语义

## History Model

- 为 `RenderItemState` 增加 `RenderItemKind { Stroke, Clear }`；Stroke 保持现有 `strokeIndex`，Clear 不引用 Stroke。
- `CanvasRuntimeHistory::AppendClear()` 在 history 内聚合上一个有效 Clear 之后全部可见 Stroke 的 composition/undo Tile，并追加可见 Clear barrier；空内容拒绝追加。
- Clear 作为覆盖所有先前内容的透明 operator：每个覆盖 Tile 的 Add 为 0、Retain 为 0。range tree、composition cache 与 ordered replay 因而可以用同一有序 operator 合成处理 Clear。
- `HasVisibleContent()` 从尾部可见链判断最新有效 Clear 之后是否存在可见 Stroke；`LastVisibleItem()` 仍表示可撤回的最后操作，不能再直接当内容布尔值。
- Undo/Redo 继续只切换尾部 item visibility。Clear 隐藏时重新暴露之前的 Stroke；Clear 可见时遮掉它之前的内容；Clear 后 Stroke 仍位于其上方。

## Clear Transaction

- Clear 命令仅在无 active contact 时处理。先构造覆盖 footprint、预留 RenderItem/raster state，再用整页透明呈现清理 GPU 瞬态；CPU Stroke 和 history 不删除。
- Clear 的 `beforeState` 是当前 raster token，`afterState` 是新 token。成功后提交 Clear item、state vectors 和当前 `rasterState`；任一步失败不得留下半提交 history。
- Undo Clear 走 composition restore：排除 Clear 后重放其下方内容；Redo Clear 直接清理覆盖区域并提交 visibility/state，不尝试 `DrawStoredStroke`。
- 页面恢复、resize、cache rebuild 和 ordered replay 对 Clear 使用透明 operator；Clear 覆盖外没有旧内容引用，因此恢复范围仍可保持稀疏。
- Clear 成功后丢弃不兼容热前像与 composition GPU cache，重置 raster generation、trusted L2、恢复计划、Laser/粒子/光标和瞬态层；CPU history/redo/viewport/其他页保留。

## Bar Interaction

- 新增可测试的 Clear 点击决策 helper，输入当前是否选择、当前内容快照、是否为双击 continuation、上次是否尝试 Clear 及其发布结果，输出 `PublishClear`、`EnterSelection` 或 no-op。
- 首击在有内容时发布 Clear，并只在 `Accepted` 时锁存“已接受 Clear，允许紧随的 double-click continuation 进入选择”。
- 空内容的普通点击直接进入选择；已锁存成功 Clear 的第二击直接进入选择，不等待异步 `currentPageHasContent=false`。
- 非 `Accepted` 首击不锁存；第二击按当时状态重试 Clear。锁存只供紧随的 continuation 消费，普通后续点击不复用。
- Whiteboard 路径不改变选择/拖动布局判定；Clear command 本身仍可撤回。

## Compatibility And Rollback

- 不改变 bridge `CommandType::Clear` wire 值、窗口线程/D3D 所有权或 `.uink` 格式。
- standalone/product history 与 GPU 文件只保留既有 module/namespace 命名差异，算法修改成对同步。
- 回滚可分别恢复 history 类型、controller Clear 分支和 Bar helper；没有数据迁移或持久化兼容负担。
