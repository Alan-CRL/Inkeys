# 笔迹文档结构与抬笔接入

## Goal

建立 `InkStroke -> InkCanvas -> InkPage -> InkCanvasCollection` 的 CPU 文档真值，并让 Pen、Highlighter、Eraser 在抬笔后先形成最终 Stroke，再从同一 Stroke 完成首次 L2 绘制。现有按键 `0` 的清屏操作改为保留当前页、追加并切换到空白页。

## Requirements

- `StoredInkPoint` 只保存 Canvas-local `float32 x/y/width`；`width` 是完整直径。坐标以当前 Canvas 物理像素为单位，允许负数和视口外有限值，不保存固定画布宽高。
- `StoredInkStyle` 保存 Pen/Highlighter/Eraser 类型、基础 RGB、opacity 和 texture。Laser、time、pressure、velocity、tilt、prediction 来源、shader、blend、AA 与 brush primitive 不进入模型。
- `InkStroke` 加入 Canvas 后只读；Canvas 按首次合成顺序拥有 Stroke。首版没有 Stroke UUID、history slot、active 标记或多图层。
- `InkPage` 使用稳定 16-byte GUID，并按 opaque Device key 保存相互独立的 Canvas；当前运行时只创建默认设备 Canvas。
- `InkCanvasCollection` 保存 Workspace GUID 和有序 Page；vector 位置即 `pageIndex`，当前页选择是运行时状态。
- Canvas 保存默认 `{0,0,1}` viewport 供未来无限画布显示使用，本轮不执行平移、缩放或 DPI 变换。
- 非取消、非 Laser contact 在 Up 后只从已确认真实输入生成 Stroke。Pen 把稳定前缀与真实 taper 尾段合并，去掉连接点重复并把半径转为直径；Highlighter/Eraser 保存真实中心线；单击保留一个点。
- prediction 不得进入永久 Stroke；首次 L2 提交也使用最终 Stroke，保证当前显示与未来重放共享语义。
- 同帧完成的 Stroke 逐笔按 Canvas 追加顺序合成；不得把同帧 coverage union 当成文档语义。
- 按键 `0` 请求在活动 contact 全部结束后追加并切换空白页，再清空当前 GPU 工作层。启动防闪烁清屏只初始化透明表面，不创建额外页面。
- 每次新页请求都追加一页；创建 GUID/Page 失败时保留当前页和画面。

## Acceptance Criteria

- [ ] Workspace/Page GUID、Page 顺序、Device Canvas 隔离、默认 viewport 和 Stroke 追加顺序由纯 CPU 测试覆盖。
- [ ] 负坐标、远离 viewport 的坐标以及 `float32 x/y/width` 不被裁剪或 DPI 换算。
- [ ] Pen 的最终 taper 已烘入 width，连接处无重复点，prediction/time 不进入 Stored Stroke。
- [ ] Highlighter、Eraser 与单点 Stroke 正确生成；Cancelled 和 Laser 不写入文档。
- [ ] Stroke 先进入当前 Canvas，再由 `DrawStoredStroke(const InkStroke&)` 完成首次绘制。
- [ ] 同帧完成的多个 Stroke 严格逐笔合成，Canvas 顺序与 L2 首次合成顺序一致。
- [ ] 按键 `0` 后新页为空、旧页 CPU 数据完整；连续请求产生连续空白页。
- [ ] `Debug|ARM64`、`Release|ARM64` 完整解决方案构建和对应测试通过。

## Out Of Scope

- UInk 编解码、第三方文件兼容、文件对话框。
- undo/redo、Palm 转橡皮历史操作、dirty replay。
- 页间导航和旧页重新渲染。
- 多显示器窗口、跨屏手势、DPI/viewport 实际变换。
- 无限画布显示、多图层、Shape、Media、HDR、Laser 持久化。
