# 运行时重做与分支丢弃技术设计

## Runtime History

`CanvasRuntimeHistory` 增加 per-Canvas LIFO redo ID 栈。`UndoLastVisible` 只在 composition visibility 已成功切为隐藏后压入 ID；`RedoLastUndone(expected)` 校验栈顶、当前 visible tail 和候选的 `previousVisibleIndex`，成功后恢复 composition visibility、可见 Tile 引用、`lastVisibleIndex_`、generation 与 revision，最后弹栈。

`DiscardRedoBranch()` 只清空 redo 可达性，不删除 append-only item。Controller 在 `InkCanvas::AppendStroke` 成功后立即调用它，因此后续 footprint、RenderItem 或 GPU 失败也不能复活旧分支。页面、Resize 和 viewport 只影响显示缓存，不修改 redo 栈。

## Redo Transaction

Controller 只在无活动 contact 时处理 Redo，并要求 `rasterState == beforeStates[item.index]`。若当前 L2 不完全清晰，先用候选的 composition tiles 和当前隐藏 history 恢复受影响背景；该步骤不会临时改变 visibility。

正常提交顺序固定为：

```text
DrawStoredStroke -> L1
CapturePreimage(current L2, beforeState -> afterState)
ApplyOperatorLayers(L1 -> L2)
RedoLastUndone(expected)
rasterState = afterStates[item.index]
CommitPreimage
```

热前像仍只保存笔画之前的像素；Redo 不读取旧前像，而是在直接绘制前重新捕获，供下一次 Undo 使用。背景清晰时不查询合成树；背景不可信时，合成树只恢复候选下面的权威 L2。

屏外候选没有可见 dirty rect 时直接提交 visibility/state，热前像捕获允许不可用。若 raster/resolve/visibility 失败，取消未提交 ticket，保持候选在 redo 栈；若 L2 可能已写入，则以当前隐藏 history 恢复候选 tiles，失败时设置 viewport 权威恢复请求。

## Compatibility

- `CanvasCommandType` 仅在末尾追加 `Redo`；窗口线程继续只发布低频命令。
- `InkStroke`、`InkCanvas`、RenderItem ID、before/after state 数组、GPU cache 格式和 HLSL 保持不变。
- 日志使用 `base=trusted_l2|composition_*|ordered_tile_replay|empty`、`path=direct_draw`、`hot_rearmed=true|false` 和 `redo_remaining`。
- 不启动可见窗口；真实键盘和 D3D Debug Layer 作为未人工验证项报告。
