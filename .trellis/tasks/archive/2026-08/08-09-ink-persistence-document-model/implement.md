# 墨迹文档、运行时撤回与页面恢复执行计划

## 0. Existing Checkpoint

- [x] CPU 文档模型、Stored Stroke completion adapter、append-first 首次绘制和空白页追加已在 `1cc746e` 提交。
- [x] 原范围 Debug/Release ARM64 完整解决方案构建和两套测试已通过。
- [x] 新实现以 `1cc746e` 为干净 checkpoint；用户原有 `Vcpkg/` 和测试构建输出保持未跟踪，不得暂存或修改。

## 1. CPU History And Tile Planning

- [x] 新增 `draw3.ink_history` module：策略、signed tile、RenderItemId/state、per-Canvas sidecar 和无 redo 尾部 visibility history。
- [x] 实现 128/256 tile footprint；Pen/Eraser capsule 和 Highlighter nib sweep 使用保守 grid traversal，不用全笔 AABB 代替稀疏覆盖。
- [x] 实现 32 项 Block 的动态 composition range tree、范围分解、generation propagation、barrier plan、LRU slot/pin 和预算调整。
- [x] 增加 CPU 测试：4K 510/1024 tile、20 entry/FIFO、超预算、负坐标、稀疏对角线、顺序/visibility/分支、tree range 与局部失效。

Rollback point：本阶段只增加 renderer-independent module/tests，不接触 L2；失败时可独立移除。

## 2. Renderer Cache Contracts

- [x] 新增 `draw3.ink_history_gpu`，实现 128 BGRA8 Undo texture-array pages、256 paired operator-array pages、slice views、scratch slots 和释放/扩容失败回退。
- [x] 把 Stored Stroke 栅格 helper 移到现有几何 module，支持显式 operator target、tile-local offset 和 target size；全屏路径保持同一函数。
- [x] 扩展 renderer/HLSL：cache b2、未占用 SRV 槽、tile raster/ordered compose/apply pass；同步 C++ layout/static_assert。
- [x] 每个 pass 显式解绑 SRV/RTV，恢复 viewport、raster/blend state；Undo copy 前解除 L2 绑定。
- [ ] GPU backend 尚无自动化 allocate/capture/restore/build/apply 调用顺序测试；静态契约和用户人工功能验收已通过，未运行 WARP 或 D3D Debug Layer。

Rollback point：cache backend 初始化失败时 renderer 继续使用原 L0/L1/L2；不改变首次绘制结果。

## 3. Hot Undo Integration

- [x] DrawingController 初始化 per-page runtime sidecar 和默认 `64 MiB / 20` Undo policy。
- [x] completed Stroke 按 `Append -> Raster L1 -> Compute footprint -> Capture L2 -> Resolve L2` 接入；同帧多 Up 逐笔完成整个序列。
- [x] 实现全局 FIFO eviction、跨页 key/revision 校验、单项超预算和 resize/device generation 失效。
- [x] 实现 Undo decision：hot restore、visibility 更新、tree invalidation、L1/L0 rebuild、dirty present 和连续热深度计算。
- [x] CPU 测试和静态审查覆盖 Pen/Highlighter/Eraser footprint、同帧逐笔顺序、缓存边界与无 readback；真实 GPU 快速 Up->Undo 未执行。

## 4. Composition Tree GPU Path

- [x] 按 leaf item order 构建 operator tile；每 Stroke 内 coverage union，不同 Stroke ordered compose。
- [x] 实现 internal node combine、range query、root build/apply、LRU/pin、maintenance queue 和输入优先的有界 idle 预建。
- [x] Undo cache miss 只重建目标 256 tile；cache hit/rebuild 状态返回 controller。
- [x] GPU 节点/资源不足或 barrier 时执行 ordered tile replay，复用 Stored Stroke renderer 和空间 tile membership，不全画布扫描/重放。
- [x] CPU 测试覆盖隐藏项 identity、跨 Block、局部 cache invalidation、预算下降、LRU/pin 和资源规划；GPU 顺序/超 4K 流式路径仍缺自动集成测试。

Rollback point：通过 policy 将 composition budget 设为 0，可强制验证 ordered tile replay 仍正确。

## 5. Controls And Page Restore

- [x] WindowController 增加保持顺序的低频 `Undo/NextPage/PreviousPage` command queue；`5/0/8` 与小键盘对应键忽略自动重复并发布 control wake。
- [x] active contact 存在时保留命令，清空后按到达顺序执行。
- [x] `0` 切换已有下一页，末页才完整创建并追加空白页；`8` 返回上一页，首页 no-op。
- [x] 页面切换清理 transient GPU 状态，从目标页 visible composition root 恢复 L2；失败时 ordered tile replay；创建失败保持旧页/画面。
- [x] 输出稳定控制台诊断：Page action/path；Undo current path、hot_remaining、history_end/no-op。
- [ ] `0/8/5` 的真实窗口输入、页面 GPU 恢复和 active-contact defer 尚无自动集成测试；对应人工行为已由用户验收通过。

## 6. Validation And Spec

- [x] 更新 `.trellis/spec/native/runtime-and-rendering.md`：RenderItem sidecar、Undo tile cache、composition tree、`5/0/8` 和 page restore 契约。
- [x] 更新 `.trellis/spec/shaders/cpu-gpu-contracts.md`：cache formats、b2/SRV slots、operator compose 公式和解绑规则。
- [x] 核对所有修改文件保持原 UTF-8 BOM + CRLF；Trellis Markdown 保持 LF。
- [x] 使用 ARM64 `MSBuild.exe` 完整 Rebuild `inkStrokeModelerTest.sln` 的 `Debug|ARM64`；VS/PS/UpdateCS/EmitCS 均重新编译成功。
- [x] 运行 `.\ARM64\Debug\inkStrokeModelerTestTests.exe`，全部测试通过。
- [x] 使用 ARM64 `MSBuild.exe` 完整 Rebuild `inkStrokeModelerTest.sln` 的 `Release|ARM64`；VS/PS/UpdateCS/EmitCS 均重新编译成功。
- [x] 运行 `.\ARM64\Release\inkStrokeModelerTestTests.exe`，全部测试通过。
- [x] 用户已人工验收普通笔、荧光笔、橡皮、同帧多 contact、`5` 热/冷撤回、`0/8` 页面、resize、空页和控制台输出。
- [ ] D3D Debug Layer 运行验证未执行；本轮仅静态核对单 slice SRV/RTV/copy 与解绑规则，不能声明 Debug Layer 已通过。
- [x] 执行 `git diff --check`、scope/spec review，确认 `Vcpkg/` 未修改且未暂存。

## Risky Files And Review Gates

- `inkStrokeModelerTest/draw3/drawing_controller.cpp/.cppm`：completed Stroke、active defer、page lifecycle 和日志顺序。
- `inkStrokeModelerTest/draw3/renderer.cpp/.cppm`、`ink.hlsli`、VS/PS：格式、寄存器、viewport 和 SRV/RTV hazard。
- `inkStrokeModelerTest/draw3/ink_prediction.cppm`、`stroke_geometry.cpp`：全屏与 tile raster 必须共享 Stored Stroke 语义。
- `inkStrokeModelerTest/draw3/window_control.cpp/.cppm`：窗口线程只发布命令，不接触 D3D。
- 两个 `.vcxproj/.filters`：新 module 必须同时进入应用与真实源码测试项目。

每完成 CPU history、GPU cache、runtime integration 三个阶段之一，先运行对应单元测试和 `git diff --check`；发现首次 L2 视觉语义漂移时回退到上一个 checkpoint，不继续叠加页面功能。
