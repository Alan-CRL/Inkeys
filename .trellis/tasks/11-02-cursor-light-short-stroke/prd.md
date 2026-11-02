# 短笔画落笔时第三鼠标光未休眠

## 背景

`NotifyCanvasDrawingStarted` 在 activity count 0→1 时向 Bar 投递 `BarCanvasDrawingActivityMessage`。Bar 窗口线程在 `HandleCanvasDrawingActivity` 中用当前 count 复核，count 已归零就直接返回。Draw3 在同帧末尾发布 1→0，因此点按、点号、快速短划等一两帧内结束的笔画几乎必然被丢弃：第三光源停留在 Grace，继续跟随光标最多 5 秒。

## 需求

- 每一次真实落笔（含已经结束的短笔画）都让第三光源进入 Dormant，并沿用 wait-for-leave 门禁；同时执行既有的 Recall 清理与次级面板收起。
- 多 contact、重复投递的消息只处理一次。
- Ended 仍只平衡 activity count，不恢复光影。

## 方案

以单调递增的落笔序号取代 count 复核：`NotifyCanvasDrawingStarted` 在 0→1 时先递增序号再投递消息；窗口线程只在序号相对上次处理值发生变化时执行，并记录已处理序号。

## 验收

- [x] 点按/短划落笔后第三光源立即淡出，不再等待 5 秒宽限。
- [x] 连续多 contact 落笔不重复收起/休眠。
- [x] spec `rendering-and-ui.md` 第三鼠标光休眠契约同步更新。
- [x] 完整解决方案 Debug|x64 构建通过，headless 测试无新增失败。
