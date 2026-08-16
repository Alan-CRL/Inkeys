# 墨迹文档、运行时撤回与页面恢复设计

## Existing Baseline

Commit `1cc746e` 已建立不可变 Stored Stroke、Page/Device Canvas、抬笔后 append-first 绘制和逐 Stroke L2 resolve。本扩展不改变 point/style 持久格式，也不把缓存数据加入 `InkCanvasCollection`。

```text
confirmed ActiveStroke
  -> FinalizeStoredStroke
  -> InkCanvas::AppendStroke
  -> CanvasRuntimeState::AppendRenderItem
  -> Rasterize Stored Stroke to L1
  -> Capture L2 preimage tiles
  -> Resolve L1 to L2
  -> Present
```

## Ownership And Modules

### CPU Runtime Sidecar

新增 renderer-independent `draw3.ink_history`：

- `RenderItemId { index, generation }`：会话内稳定句柄。
- `RenderItemState`：Stroke index、visible、pixel bounds、128/256 tile footprint、content generation。
- `CanvasRuntimeHistory`：按显示顺序保存 RenderItem，查找最后可见项并处理无 redo 的尾部撤回。
- `CompositionRangeTree`：32 项叶子、动态二叉范围节点、visibility/geometry/order generation 和 range decomposition。
- `UndoCachePolicy`、`CompositionCachePolicy`、tile/slot 预算与 LRU/pin 规划器。

`DrawingController` 按 Page/DeviceKey 拥有 sidecar；`InkStroke` 仍不可变。页面 vector 扩容后以 page index/GUID 重新查找，不保存易失裸指针。

### GPU Cache Backend

新增 `draw3.ink_history_gpu`，由绘制线程创建并持有：

- 只接收 `InkRenderer` 的 device/context，不暴露给窗口线程。
- 管理 Undo tile array、Composition Add/Retain array page、每 slice RTV、LRU slot 和 scratch slot。
- 提供 `CapturePreimage`、`RestorePreimage`、`BuildLeafTile`、`ComposeNodeTile`、`ApplyOperatorTile` 和 generation invalidation。
- 失败返回明确状态，由 controller 选择下一层路径；不改变 CPU history。

Stored Stroke 的通用栅格 helper 从 `drawing_controller.cpp` 匿名区移到现有 `draw3.ink_prediction` 几何实现，增加目标 operator 和 Canvas-to-tile offset 参数。全屏首次绘制与 tile cache 使用同一 helper、颜色和几何逻辑。

## Hot Undo Preimage

### Allocation

Undo page 使用 `D3D11_TEXTURE2DARRAY`：

```text
format             B8G8R8A8_UNORM
tile               128 x 128 x 4 bytes = 64 KiB
slices per page     256 = 16 MiB
default pages       4
default slots       1024 = 64 MiB
entry limit         20
```

Texture 仅用于 GPU copy，不创建 CPU staging。预算改变在绘制线程应用；最后一页可以按剩余槽数创建。降低预算先淘汰 FIFO entry，再释放空闲尾页；提高预算只允许未来捕获使用。

### Footprint

- Pen/Eraser 对每个 capsule segment 使用 tile-grid traversal，并按端点最大半径与 2px shader padding 扩展邻近 tile；单点按圆处理。
- Highlighter 对固定 nib sweep 使用相同 grid traversal，并按 `{1.25, 25}` half size 与 2px padding 扩展。
- footprint 与当前 L2 可见矩形相交，tile coordinate 仍使用有符号 Canvas 坐标，便于未来无限画布。
- 去重后的 tile 列表按稳定坐标顺序保存；细长对角线只捕获沿线 tile，不扩成完整 AABB。

### Capture And Restore

完成 Stroke 时先用最终 Stored Stroke 生成 L1 和 footprint，此时 L2 未改变：

```text
reserve slots (evict oldest until count/bytes fit)
-> unbind L2 RTV/SRV
-> CopySubresourceRegion L2 tile -> undo slice
-> resolve this Stroke L1 -> L2
-> publish UndoEntry
```

同帧多个 completed Stroke 逐项执行完整序列。若单项槽数大于总预算，不淘汰全部仍有用旧项，只给新 RenderItem 标记 `NoHotPreimage`。

撤回只接受与当前 Page/Canvas、RenderItem、L2 raster generation 和 expected history revision 匹配的 entry。命中后按 tile 复制回 L2并消费 entry；随后把 RenderItem 设为隐藏并使合成树路径失效。跨页 entry 可以保留，但返回页面后必须先恢复到匹配 revision 才允许命中。Resize、设备丢失或 raster key 改变会整体丢弃不兼容 entry。

## Composition Range Tree

### Logical Tree

RenderItem index 每 32 项形成一个叶子 Block。隐藏项不删除，只贡献单位 operator。树按显示顺序组合：

```text
Earlier = (Ae, Re)
Later   = (Al, Rl)

Combined.Add    = Al + Rl * Ae
Combined.Retain = Rl * Re
```

动态增长只增加右侧叶子/新根；尾部 append、visibility 更新和 future geometry update 递增叶子 generation，并只使祖先的相关 tile cache key 过期。Range query 把 `[begin,end)` 分解为保持显示顺序的 `O(log blockCount)` 节点；目标对象所在叶子最多局部重放 32 项。

CPU tree 只保存拓扑、范围、generation 和 sparse tile membership。未来非仿射项把节点标为 barrier，query 返回 `affine range -> ordered barrier -> affine range` 计划。

### GPU Allocation

Composition page 使用成对 texture arrays：

```text
tile                256 x 256
Add                 B8G8R8A8_UNORM = 256 KiB/slice
Retain              R16_FLOAT       = 128 KiB/slice
operator slot       384 KiB
slices per page     64
page                24 MiB
default budget      192 MiB = 8 pages = 512 slots
```

首个 page 按需创建，其余 page 在绘制线程懒扩容。缓存 key：

```text
PageGuid + DeviceKey + signed TileCoordinate + RasterKey
+ NodeRange + NodeContentGeneration
```

未固定 slot 使用全局 LRU；query/build 输入、当前 edit Below/Above 和三个构建 scratch slot在命令结束前 pin。若一个高分辨率页面超过可驻留槽数，按 tile 流式 build/apply/evict，不要求整页 operator 同时驻留。

### Raster And Compose Passes

HLSL 保留现有普通墨迹路径，增加独立 cache constant buffer 与 `Texture2DArray` SRV 槽：

- raster leaf：把 Stored point 减去 tile origin，使用 256x256 viewport 和 slice RTV；一条 Stroke 内继续 Add/MAX、Retain/MIN。
- leaf accumulation：每条 Stroke 先进入 item scratch，再以 ordered formula 合入 ping-pong accumulator，不能把不同 Stroke 做 coverage union。
- internal node：一次 pass 读取 Earlier/Later Add/Retain slice，写入目标 Add/Retain slice，blend disabled。
- apply：读取一个 cached operator slice，在对应 Canvas tile 上用现有 dual-source resolve blend 写入 L2。

所有 pass 显式解除参与资源的 SRV/RTV 绑定并恢复全屏 viewport/raster state。新增 CPU/HLSL 常量保持 16 字节对齐，使用未占用寄存器，不改变现有 t0-t9 契约。

### Build Policy

- 抬笔热路径只更新逻辑 tree generation 和 maintenance queue，不同步构建完整树。
- 无活动 contact 时，绘制线程用有界 maintenance budget 预建最近封闭 Block/可见 Tile；输入到来立即让路。
- Undo cache miss 和页面切换提升所需 tile 为同步高优先级，但仍只提交 GPU 命令，不等待 GPU readback。
- 节点构建失败或 barrier 无法合并时，对目标 256 tile 清透明并按空间索引顺序调用同一个 Stored Stroke renderer；这是最终正确性回退，不做全画布历史重放。

## Undo Decision And Diagnostics

按键 `5` 在 active contact 全部结束后消费。对当前页最后可见 RenderItem：

```text
matching preimage       -> hot_preimage
tree node already valid -> composition_cache
tree nodes can rebuild  -> composition_rebuild
cache/resource failure  -> ordered_tile_replay
```

每次操作后计算从新尾部开始连续匹配的热 entry 数量：

```text
[Undo] page=2 item=57 path=hot_preimage hot_remaining=6
[Undo] page=2 item=34 path=composition_rebuild hot_remaining=3
[Undo] page=1 result=noop reason=empty
```

`hot_remaining` 表示还可连续使用热前像的撤回次数；若更早已经没有可见历史，额外输出 `history_end=true`，避免把历史结束误报成即将降级。

## Canvas Command And Pages

`WindowController` 使用受 mutex 保护的低频 `CanvasCommand` 队列保存 `Undo/NextPage/PreviousPage` 到达顺序。窗口过程只 enqueue、发布 control wake，忽略 `lParam` 标记的自动重复；不操作文档或 D3D。

绘制线程在 active contact 清空后顺序执行：

- `NextPage`：若 `currentPageIndex + 1 < pageCount`，切换已有页；否则先成功创建 GUID、Page、默认 Canvas 和 sidecar，再切换。
- `PreviousPage`：index 大于 0 时减一，否则 no-op。
- 切换成功后清 L0/L1/L2/Laser transient，从目标页当前 visible tree root 恢复 L2；cache miss 构建，失败时 ordered tile replay；最后 full present。
- 页面操作输出 key/action/current index/page count/rebuild path。创建失败保持原页和画面。

启动 `ClearCanvas()` 不 enqueue command，也不改变 page index。页面切换不为每页保留全尺寸 L2；只保留 CPU history 和受预算约束的共享 tile cache。

## Configuration And Invalidation

公开运行时策略接口只发布设置并唤醒绘制线程：

```text
UndoCachePolicy { byteBudget=64 MiB, maxEntries=20 }
CompositionCachePolicy { byteBudget=192 MiB }
```

Tile size 与 Block size 是内部格式版本，不作为普通运行时设置。设置为 0 禁用对应 GPU cache，但 ordered tile replay 保留。

`RasterKey` 至少包含 DeviceKey、scale、target format、pipeline generation。以下事件处理：

- Resize/raster scale/format/pipeline generation：拒绝不匹配 Undo，Composition 进入新 namespace；旧节点可 LRU 淘汰。
- Page switch：切换 namespace，不复制整张 L2；匹配 revision 的跨页 entry 可继续存在。
- Device lost/Release：释放全部 GPU page，CPU tree/history 保留，恢复后按需重建。
- 纯 viewport 平移且 scale 不变：Canvas tile key 不变。

## Compatibility, Failure And Rollback

- 文档和 runtime RenderItem 必须先追加，随后才允许首次 raster/capture/resolve。Undo/Composition cache 资源失败只关闭对应热路径，首次 Stroke 仍可直接 resolve；首次 Stored Stroke raster 或 resolve 本身失败时保留 CPU Stroke/RenderItem、取消未提交前像并记录诊断，不伪造 L2 成功状态。
- 当前首次 raster/resolve 失败不会在同一次抬笔内再次恢复该 Stroke；其 CPU 真值会在之后的页面恢复、resize 或冷重建路径重新出现。这是本轮保留的残余失败语义，不得把它描述为已即时恢复。
- Undo preimage 是 BGRA8 原像，命中恢复按位一致。Composition 是运行时加速，不进入文件；持久重放仍以 Stored Stroke 顺序和现有 renderer 为权威。
- 新模块、HLSL 常量和工程项可以整体禁用；关闭两个 cache 后仍能通过 sidecar + ordered tile replay 提供撤回与页面导航。
- 不修改 `additional/`、`Vcpkg/` 或参考工程。
