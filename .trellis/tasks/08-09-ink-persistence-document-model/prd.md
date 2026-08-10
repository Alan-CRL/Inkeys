# 墨迹文档、运行时撤回与页面恢复

## Goal

在已经完成的 `InkStroke -> InkCanvas -> InkPage -> InkCanvasCollection` CPU 文档模型上，增加不污染持久对象的运行时显示顺序、尾部撤回、4K 热前像缓存和分块仿射合成树。用户能够通过 `5` 撤回、`0` 前往或创建下一页、`8` 返回上一页；大量历史或缓存失效时仍保持正确显示，只降低到合成树或局部顺序重放。

## Background

- 基础文档结构、抬笔保存、Stored Stroke 首次 L2 绘制和“按 `0` 追加空白页”已经在 commit `1cc746e` 中完成。
- 当前 L2 为 `DXGI_FORMAT_B8G8R8A8_UNORM`；现有 operator 为 `BGRA8 Add + R16F Retain`，实际占用 `6 bytes/pixel`。
- 当前 `DrawStoredStroke` 先把最终 Stroke 栅格到 L1 operator，再 resolve 到 L2，因此可以在两者之间捕获尚未改变的 L2 前像。
- 仓库尚无 undo 请求、运行时 RenderItem、选择/旋转/隐藏命令和旧页重绘入口。

## Requirements

### Runtime History

- `InkStroke` 及现有持久对象继续只保存显示所需数据；可见性、history generation、RenderItem ID、栅格 bounds 和缓存状态保存在 Canvas 外的绘制线程 sidecar。
- RenderItem 按 Canvas Stroke 追加顺序稳定排列。尾部撤回把最后一个可见非 Laser RenderItem 设为隐藏；不删除 Stored Stroke，不提供 redo。
- 撤回后再绘制直接追加新 RenderItem；已经隐藏的分支不再对用户暴露，未来 UInk 导出只枚举当前可见项。

### Hot Undo Preimage

- 默认策略为 `64 MiB` 和最多 `20` 个撤回命令，两项均可通过运行时接口调整；`0` 表示关闭。降低限制时从最旧条目开始淘汰，提高限制不恢复已淘汰前像。
- 前像使用 `128 x 128` BGRA8 GPU tile。默认 64 MiB 对应 1024 个槽；4K 可见区域为 `30 x 17 = 510` 个 tile，能够容纳两份完整 4K 前像。
- 每条 Stroke 只捕获实际影响的可见 tile。普通笔、荧光笔和橡皮的 CPU footprint 必须包含半径/固定 nib、端帽和 AA padding，并避免让一条细长对角线仅因外接矩形而捕获整屏。
- 捕获发生在最终 Stored Stroke 已栅格到 L1、但尚未 resolve 到 L2 时。所有 copy 使用绘制线程上的 `CopySubresourceRegion`；禁止 Map、staging readback、`GetData`、CPU fence 等待或窗口线程 D3D 调用。
- 同帧多笔完成时严格执行 `Capture A -> Resolve A -> Capture B -> Resolve B`。撤回多笔按 LIFO 逐条恢复。
- 单条前像超过预算、资源创建失败、generation 不匹配或前像被淘汰时，撤回必须自动使用合成树；合成树不可用时再局部顺序重放，不能丢失撤回功能。

### Composition Tree

- 全 Canvas 共享稳定显示顺序树，每个叶子 Block 默认包含 32 个 RenderItem，内部节点按 `Later(Earlier(Below))` 保存有序范围。
- 当前 Pen、Highlighter、Eraser 复用 `F(Below) = Add + Retain * Below`。组合公式固定为 `Add = Later.Add + Later.Retain * Earlier.Add`、`Retain = Later.Retain * Earlier.Retain`；隐藏项是单位 operator。
- GPU cache 使用 `256 x 256` operator tile；每槽为 `BGRA8 Add + R16F Retain = 384 KiB`。每页 64 槽、24 MiB，默认预算 192 MiB（8 页/512 槽），预算可运行时调整。
- 树拓扑和 generation 始终保留在 CPU；GPU 节点是可淘汰缓存。缓存以 page/canvas、signed tile coordinate、raster scale、pipeline generation、node range 和 content generation 为键，采用全局 LRU；当前查询或编辑节点在操作完成前固定。
- Append、Undo、visibility 或未来 geometry/order 变化只失效相关 Tile 的叶子与祖先路径。树必须提供任意有序范围查询和 RenderItem 失效接口，虽然本轮不提供下层编辑 UI。
- 无法表示为仿射 operator 的未来内容作为局部顺序重放屏障，不得错误折叠。GPU 预算不足时允许边构建、边应用、边淘汰，分辨率高于 4K 仍保持正确性。

### Controls And Page Lifecycle

- 真实按下数字键/小键盘 `5` 发布一个 Undo 命令；长按自动重复忽略。存在活动 contact 时命令排队，全部 contact 结束后再处理。
- 每次 Undo 在控制台输出当前页、RenderItem、实际路径（`hot_preimage`、`composition_cache`、`composition_rebuild` 或 `ordered_tile_replay`）以及仍可连续热撤回多少笔；无内容时输出明确 no-op。
- 数字键/小键盘 `0` 表示下一页：存在下一页时切换过去；当前已是末页时追加空白页并切换。数字键/小键盘 `8` 返回上一页；第一页时 no-op。
- 页面命令同样忽略自动重复并等待活动 contact 结束。页面切换必须从当前可见 RenderItem 恢复 L2，不能丢失旧页 CPU 数据；每次操作输出页索引、页总数、切换/追加/no-op 和恢复路径。
- `DrawingController::ClearCanvas()` 仍只负责启动透明初始化，不产生页面或历史命令。

### Threading, Failure And Invalidation

- 窗口线程只按到达顺序发布低频 Canvas 命令；绘制线程独占 document/history/cache 和全部 D3D 操作，不新增 UI 等待。
- resize、device lost、页面/Canvas 不匹配、目标格式、raster scale、shader/AA/brush pipeline generation 变化必须拒绝陈旧缓存。相同 scale 的纯 viewport 平移不使 Canvas-local tile cache 失效。
- cache 创建或扩容失败只禁用对应热路径并输出一次诊断；CPU 文档、撤回、页面导航和现有绘制继续。
- 缓存不进入 `InkCanvasCollection` 或未来 UInk 文件。多页和未来多显示设备共享配置预算，不能按 Page 无界倍增。

## Acceptance Criteria

- [x] 现有文档模型、最终 Stroke、同帧逐笔提交和新页测试继续通过。
- [x] CPU 测试覆盖 RenderItem 稳定顺序、隐藏尾部、撤回后新分支、Page/Device 隔离和无 redo。
- [x] `64 MiB / 20` 默认策略、运行时增减、FIFO 淘汰、0 禁用、单条超预算和 4K 510/1024 tile 计算由测试覆盖。
- [x] footprint 测试覆盖 Pen/Highlighter/Eraser、单点、负坐标、画布裁剪、AA padding 和跨 4K 的稀疏对角线。
- [x] 静态核对确认 GPU 捕获位于 L1 栅格与 L2 resolve 之间、没有 readback，且同帧多 Up 保持逐笔 capture/resolve 顺序。
- [ ] 热撤回逐 tile 原样恢复 L2；缓存缺失时依次降级到合成树和局部有序重放，显示顺序不变。
- [x] 合成树测试覆盖 32 项 Block、动态增长、范围组合顺序、visibility identity、局部 generation 失效、LRU/pin 和非仿射屏障。
- [x] `192 MiB` composition 策略对应 8 个 24 MiB 页/512 槽；降低预算淘汰未固定节点，提高预算不阻塞窗口线程。
- [ ] `5`、`0`、`8` 忽略自动重复并在活动 contact 结束后按发布顺序执行；控制台输出包含规定路径和剩余热撤回深度。
- [ ] `0` 在已有下一页时切换、末页时追加；`8` 返回上一页且首页 no-op；旧页按当前 visibility 正确恢复。
- [ ] resize/cache generation/resource failure 不使用陈旧 tile，并能通过 CPU 数据恢复当前页。
- [x] `Debug|ARM64`、`Release|ARM64` 完整解决方案 Rebuild、两套测试可执行文件和 `git diff --check` 通过；静态审计确认 SRV/RTV slice 与解绑契约。受本轮禁止可见窗口/桌面操控的约束，D3D Debug Layer 运行验证明确不执行、不得声称通过。

## Out Of Scope

- redo、Palm 阈值转橡皮和 L1 candidate 取消流程。
- 下层对象选择、命中测试、旋转、移动、隐藏 UI；底层树只提供未来接入契约。
- UInk 编解码、第三方文件兼容和文件对话框。
- 多显示器窗口、跨屏手势、viewport 平移缩放 UI、无限画布显示和 DPI 实际变换。
- 多图层、Shape、Media、HDR 和 Laser 持久化。
