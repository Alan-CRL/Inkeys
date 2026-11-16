# Inkeys3 / Draw3：4K 大橡皮与白板漫游性能调查

> **用途**：供后续修改 Draw3 渲染管线、白板漫游／惯性、缓存与窗口架构时参考。记录本次对话中的代码调查、已确认问题、优化方向及验收标准。  
> **记录日期**：2026-11-16  
> **源码基准**：`Alan-CRL/Inkeys` 的 `dev` 分支，提交 [`cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f`](https://github.com/Alan-CRL/Inkeys/commit/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f)（2026-11-15 合并 PR #272，形状功能）。  
> **性质**：对本次调查**已有发现**的归档，非后续提交的自动同步文档；修改前须按以上 commit 定位，再核对当前代码差异。  
> **重点环境**：4K（3840×2160）、120 FPS（8.33 ms／帧）；低功耗或共享内存 GPU（如 Intel UHD 630）；Win7 SP1 + KB2670838 至 Win11 的兼容要求。

---

## 0. 执行摘要：结论与风险级别

1. **当前漫游不是“每帧重画全历史笔迹”，但确实存在“每次视口移动，重建整个屏幕尺寸 L2”的工作。** 视口变化会清空 L2，重新规划并恢复可见世界 tile，随后最终整屏合成／呈现。已命中的 tile 可以直接从历史合成缓存读取，不一定重新栅格化原笔迹。
2. **纯平移本身并不使世界 tile 缓存失效。** 缓存键不包含视口偏移。主要可优化的是“缓存如何被显示”：目前有效 tile 仍需逐块应用到新的屏幕 L2，而不是直接移动／采样世界缓存。
3. **大橡皮除了正常的像素覆盖成本，存在可静态确认的额外工作：** 纯橡皮活动帧反复清空没有内容的 L0；最终合成照常采样无效／恒等的操作层；大尺寸游标会扩大 `frameDirty`，让本来无需重新处理的大范围底图也参与合成。
4. **全缓存不等于零 GPU 开销。** 4K 的全屏复制、采样、混合与输出仍耗带宽和时间；然而目前有多次可合并或省去的整屏操作，不能直接据此认定“4K／120 FPS 无法实现”。
5. **现有背景同帧绘制代码不是默认运行路径。** 白板图案归属的开关目前仍为 `Freeze`；背景主要通过 D2D／WARP 的冻结表面路径，约束为 30／60 FPS，与 HARDWARE 墨迹漫游存在步调差异。同窗口／同相机的计划有意义，但不会自动解决 L2 的重复恢复。
6. **优先目标**：热缓存、无活动笔迹的纯漫游每帧不重放历史、不新建 tile、不整屏复制快照、不清空／合成空的 L0/L1；仅保留有效内容的变换、采样与呈现。

**重要：** 上述第 1～3、5 项是代码路径级别的调查发现；“它们各自造成多少毫秒卡顿”尚**没有 GPU timestamp 实测**，不能把它们的贡献按严重程度定量排序。

## 1. 关键概念与“全量刷新”的分层

“全量”至少分四层，不能混为一谈：

| 层级 | 当前 dev 情况 | 对性能的含义 |
|---|---|---|
| 全历史笔迹几何重放 | **漫游缓存命中时一般不需要** | 不应将每帧成本直接按总笔迹条数估算 |
| 当前可见世界 tile 的恢复 | **视口变化后会重新排队并逐块应用到屏幕 L2** | 即使命中缓存，也有 tile 采样／绘制／状态切换 |
| 屏幕 L2 的清空与最终合成 | **漫游会走整屏路径** | 即使历史完全缓存，也有按屏幕分辨率增长的固定像素工作 |
| DXGI／DWM 显示呈现 | **漫游走 `forceFullPresent`** | 不等于应用内部的所有阶段必须全屏重做；`Present1` 脏区也无法补救此前已发生的绘制工作 |

**普通书写／橡皮帧**：通常使用合并的 `frameDirty` 矩形局部复制与合成，但大橡皮或大游标可能将其扩大至接近全屏。**漫游帧**：会触发新的 L2 视口恢复，`forceFullPresent = true`。

## 2. 已有世界坐标缓存：基础可复用

### 2.1 结构与参数

- 世界 tile：**256 × 256 px**。
- 缓存存储合成算子 **`Add`** 与 **`Retain`**：`Add` 为 BGRA8（每像素 4 字节），`Retain` 为 R16F（每像素 2 字节）。
- 历史树：每个叶节点约 **32 个 item**；内部节点将绘制和擦除按操作符合成。
- 默认合成缓存上限约 **192 MiB**。
- Key 包括画布、栅格配置、tile 坐标、历史节点和 tile generation，**不包括相机／视口 x、y 位移**。
- 每个完整 `Add+Retain` node-tile 名义为 `256×256×6 = 393216 bytes = 384 KiB`；192 MiB 理论上相当于 512 个 node-tile，**但这不是可用的最终视图 tile 数量**：历史子节点、预取内容、多画布、其他资源都会争用预算。4K 视口仅自身就大约跨越 **135～160** 个 256 px tile（取决于对齐），尚未计入预取。

相关源码：

- [历史缓存参数／叶大小](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistory.cppm#L18)
- [缓存键／tile 条目](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp#L222)
- [历史范围划分](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistory.cpp#L904)

### 2.2 缓存命中路径

- `GetOrBuildNode()` 在命中可用 node-tile 时直接返回对应纹理，而非递归遍历并重新绘制其所有子节点。
- `ApplyOperator()` 再把缓存算子应用到屏幕 L2；实现涉及资源绑定、常量更新与 `Draw(6)` 等操作。
- 对于完整历史区间，无额外 barrier、节点有效时，有机会使用最终根节点缓存。
- 这证明“**历史可重放数据**”和“**画面实际显示**”已经部分解耦，但目前还没有彻底解耦。

相关源码：

- [GPU 缓存命中／构建入口](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp#L924)
- [将算子应用到 L2](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp#L662)

## 3. 当前白板漫游的实际处理路径

```text
触摸／鼠标／惯性更新 Camera 或 Viewport
        ↓
viewportRefreshPending = true
        ↓
清空屏幕尺寸的 L2
        ↓
重置 viewportTilePlan、恢复索引、可见区／预取区任务
        ↓
按预算逐块 RestoreComposition()
    ├─ 热 tile → 复用缓存结果 → ApplyOperator 到 L2
    └─ 冷 tile → 构建历史合成节点 → ApplyOperator 到 L2
        ↓
若尚未恢复完整：可能用先前可信 L2 快照填补
若恢复完整且视口变化：可能 CopyResource 刷新可信快照
        ↓
复制 L2 → 合成 L1 / L0 / 游标 → 全屏 Present
```

主要源码：

- [视口刷新触发](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L18488)
- [清空 L2、恢复规划](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L21644)
- [按预算恢复](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L21702)
- [导航预热／预测 tile](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L21741)
- [快照与最终整屏合成](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L23238)

### 3.1 为什么热缓存仍可能卡顿

纯平移不要求笔迹重新栅格化，但当前每次有效视口移动仍有：

1. **L2 全屏清空**。
2. **逐个可见 tile 恢复**：存在纹理采样／Draw call 等工作，即便 tile 在世界缓存中有效。
3. **完整快照维护**：当视口已恢复清晰时，可能把整张 L2 再复制到可信快照。
4. **底图复制＋L0/L1 合成**：可能对整屏执行。
5. **Present**：漫游走整帧呈现路径。

例如 3840 px 宽的屏幕水平只移动 10 px，新露出的面积约占屏幕的 **0.26%**。但当前实现仍重组整屏 L2。这里的 0.26% **指重新暴露、需要新内容的面积**，**不是**最终输出可以只处理 0.26% 像素。

### 3.2 快照与运动模糊的潜在成本

- `RefreshTrustedL2Snapshot()` 使用 `ID3D11DeviceContext::CopyResource()`，复制整张 L2 到可信快照纹理。
- 未完成视口恢复的兜底绘制可能使用旧快照，并根据移动情况执行 **9-tap** 模糊；没有模糊时则为单次采样。
- 旧快照是**视觉连续性兜底**，不天然等于最便宜的 GPU 显示方法。

相关源码：

- [可信快照复制](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererSnapshot.cpp#L145)
- [快照模糊 shader](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl#L500)

### 3.3 预算并非 GPU 硬限制

恢复预算根据 CPU 侧上一帧耗时估计，通常在 **0～4 ms** 范围，并使用单 tile 耗时滑动平均计算恢复数量。但这并不保证：

- 具体 GPU 命令一定在预算内执行完。
- 首次构建一个复杂冷 tile 时，内部递归构建一定可以在任意时刻让出。
- CPU 计时能精确代表 GPU 执行耗时。

建议引入 GPU 时间戳测量、冷 tile 构建的任务分解与带宽管理。

相关源码：[预算计算](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp#L1539)、[恢复计时／执行](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L21702)。

## 4. 大橡皮：已确认的热点与区别

### 4.1 擦除轨迹本身已经是增量提交

`CommitEraserRealPointsToL1()` 主要提交尚未提交的真实点，并保留一个连接端点；持续擦除并非每帧重播全部轨迹。主要把擦除算子落到 L1；纯橡皮没有普通画笔那样的 L0 预测几何。

但每个新胶囊线段覆盖较大面积，粗橡皮相邻段严重重叠，仍然导致重复像素着色、混合／写入。**增量处理轨迹点数 ≠ 增量处理新增覆盖面积。**

- [橡皮增量提交](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp#L2609)
- [胶囊图元绘制](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp#L58)
- [胶囊几何 vertex shader](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Assets/inkVertexShader.hlsl#L308)

**后续改进候选**：重叠胶囊冗余剔除、稳定扫过区域跳过、几何归并；但必须验证半径变化、急转弯、抗锯齿边缘、Retain/擦除算子正确性，不能只凭大圆覆盖比例粗暴裁剪。

### 4.2 已确认的 L0 无效整层清除（优先修复）

控制流要点：

```cpp
ShouldRebuildSharedL0(
    hasActiveShape,
    shapeLayerNeedsRebuild,
    otherLiveLayerNeedsRebuild)
// 实现：
return !hasActiveShape || shapeGeometryChanged || otherLiveGeometryChanged;
```

纯橡皮活动帧 `hasActiveShape == false`，触发 L0 重建分支；随后清空 L0，但实时几何绘制跳过 Eraser。因此，即使 L0 本来就为空，仍可能反复运行 `ClearOperatorLayer(layerL0)`。

`ClearOperatorLayer` 对 `Add` 和 `Retain` 分别调用 `ClearRenderTargetView`，是**整个资源视图**的清除，**不受 scissor／viewport 限制**。驱动可能有快速清除优化，实际成本需要测量，但冗余逻辑明确存在。

**正确改法**：增加 L0 空／非空或内容代次状态；从上一工具进入纯橡皮时需要执行必要的清理；后续纯橡皮连续帧不再反复清理恒等 L0。不要无条件删除清空调用，以免遗留先前画笔的预测内容。

- [L0 重建控制流](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L23082)
- [`ShouldRebuildSharedL0` 实现](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm#L596)
- [`ClearOperatorLayer` 实现](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp#L260)
- [Microsoft: ClearRenderTargetView](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearrendertargetview)

### 4.3 最终合成对空层缺少快路径

普通合成大致为：

1. 把 L2 的 dirty rect `CopySubresourceRegion` 到 backbuffer。
2. shader 读取 `L1.Add`、`L1.Retain`、`L0.Add`、`L0.Retain`，与 backbuffer 合成。

这里 `InkRenderer::CopyResource(..., rect)` **是项目封装名**，内部并非 D3D11 的全纹理 `CopyResource`，而是按 rectangle 调用 **`CopySubresourceRegion`**。不能误判普通帧全屏复制。

即使纯橡皮 L0 为空、L1.Add 为零，通用 shader 仍可读取四张算子纹理，缺少空层跳过。建议按明确状态走专用 shader 或合成路径：

| 场景 | 可行优化 |
|---|---|
| L0/L1 均为恒等 | 直接显示稳定内容；跳过无效操作层 |
| 纯橡皮，L1.Add=0、L0 恒等 | 只读取 `L2` 与 `L1.Retain`；理论关系 `Cink = CL2 × R_L1` |
| 单活动操作层 | 仅绑定该层的有效 Add/Retain |
| 普通复杂情况 | 保留完整双层算子合成 |

可以把 L2 的复制和 resolve 融合成一次输出 backbuffer 的 shader 绘制，**但**要验证 alpha／premultiplied alpha、blend state、擦除后透明区、其他活动效果，不可让橡皮作用于 PDF、网格或纸张背景。

- [局部复制封装](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp#L135)
- [实际合成与资源绑定](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp#L147)
- [通用合成 shader](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl#L531)

### 4.4 大游标可能是“未真正擦除也卡”的原因

游标直接绘制到 backbuffer。为了清理上一次游标位置，当前帧把**旧游标 rect** 和 **新游标 rect** 一同并入 `frameDirty`；底图在这块合并区域重新复制、合成，之后再画新游标。

对于巨型游标，或旧新位置距离较大的情况：

- dirty rect 可能接近全屏；
- 多个脏区合成一个包围矩形，会包含大量实际未变化的像素；
- 原本很轻的游标位移因此牵连大面积底图合成。

**注意：** 游标图案纹理已有缓存。持续卡顿不能直接归咎为每帧 CPU 重新生成图案；首次超过缓存尺寸档位时的图案重建应另行测量。

- [游标脏区合并](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L23158)
- [游标纹理缓存](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp#L264)

**诊断优先项**：同样尺寸下比较“只移动游标（不擦）”与“持续擦除”。如果前者已经严重掉帧，先解决游标／底图合成，而不是先重构历史擦除算法。

### 4.5 松手／完成帧与持续帧应分开看

完成态可能重新构建完成笔迹并落入 L2，与持续擦除时的 L1 增量绘制不同。基准测试应分别统计持续帧、抬笔帧及抬笔后首次稳定帧，避免平均值掩盖尖峰。

- [完成态绘制分支](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp#L22884)

## 5. 4K／120 FPS 的理论像素成本（非实测）

- 3840 × 2160 = **8,294,400 像素**。
- BGRA8 全屏单张纹理 ≈ **31.64 MiB**。
- 120 FPS 帧周期 ≈ **8.33 ms**。

以下仅是简单的 GPU 读写流量**名义模型**，用于比较不同算法的量级；不是实际显存带宽需求、下界或性能预测。图形缓存、压缩、快速清除、混合读写、优化的 GPU 实现等都会改变真实数据。

| 示意路径 | 估算字节／像素 | 4K × 120 FPS 的名义量级 |
|---|---:|---:|
| 全屏 BGRA8 复制（读 4 + 写 4） | 8 B | 7.96 GB/s |
| L2 复制 + 通用四纹理 resolve | ~28 B | 27.87 GB/s |
| 合并复制与通用 resolve | ~20 B | 19.91 GB/s |
| 专用的纯橡皮融合路径（L2 + R + 写出） | ~10 B | 9.95 GB/s |

这些数值仅为简化比较，不能据此推断具体 GPU 的 FPS。**关键不是全缓存是否“完全不读写像素”，而是每帧要读／写和混合多少次，以及能否避免不必要的整屏阶段。**

## 6. 长期推荐架构：热缓存直接显示，重建任务独立

### 6.1 把两个概念彻底分开

**A. 显示路径（每帧）：**

- 相机变化后，计算可见的世界 tile；
- 已有效的 tile 直接采样或经显示层 transform 绘制；
- 仅位置变动时不重新将全部 tile Apply 到屏幕 L2；
- 动态墨迹和游标单独叠加；
- 不因视口平移而刷新整屏可信快照。

**B. 生成／失效路径（后台式预算任务，但仍在正常渲染调度内）：**

- 只把缺失、generation 变化或内容被修改的 tile 进入构建队列；
- 复用现有 `GetOrBuildNode`、历史树、Add/Retain、预热等能力；
- 按可见优先、相机运动预测、邻接预取、缓存压力顺序调度；
- 缺失 tile 使用稳定的局部回退，避免让整屏进入恢复状态。

这里所谓“后台式预算”**不是必须另开后台线程，也不是异步接口承诺**；它只是与每帧纯显示分离的、可打断的增量 GPU 工作。

### 6.2 根节点 Add 可能直接成为显示纹理

现有操作符的逻辑可表示为：

```text
Cout = Add + Retain × Cbelow
```

当根节点已覆盖**完整稳定历史**，底色是透明（`Cbelow = 0`），且节点内容完整有效时：

```text
Cink = Root.Add
```

**设计推论**：最终根 `Add` 已包含稳定透明墨迹，可以直接绘制或参与合成；`Retain` 仍保留在历史系统中用于增量组合、局部更新、撤销恢复等。这样可跳过“根节点恢复到屏幕 L2，再把 L2 拷贝到 backbuffer”的重复工作。

**适用前提／必须验证**：完整历史根、特殊 barrier、笔迹与形状编辑层、未完成轨迹、混合与透明度语义、擦除影响边界、多画布／快照状态。不可对所有中间节点的 `Add` 不加区分地作为最终可见墨迹。

- [历史合成公式 shader](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl#L480)

### 6.3 可选的短期过渡：屏幕 L2 滚动复用

如果短期不方便改为 tile 直接显示，可以保留原屏幕 L2 重叠部分，仅恢复平移后露出的窄条带：

1. 独立保存上次视口的有效 L2（或前后乒乓缓冲）。
2. 整数像素平移时从上一稳定资源复制重叠区域。
3. 补齐新露出条带及真正失效区域。
4. 分数位移只在最终采样时处理，避免对逐帧已插值结果继续插值而产生累计模糊。

**D3D11 安全性**：不要对同一 subresource 使用非法的重叠区自拷贝；需要独立源目标或其他等效正确策略。此方案仍要搬运大面积像素，是过渡方案，不是长期最优。

- [Microsoft: CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)

### 6.4 快照回退应只服务于缺失区域

- 对**完整热缓存漫游**跳过每帧整张 L2 可信快照复制。
- 只有局部 tile 暂缺时才采用对应区域回退；不能以 alpha 为零判定“缺失”，因为擦除后的透明就是正确图像。
- 优先用稳定的世界 tile／上次有效结果填补；必要时快照按缺失矩形采样。
- 性能受限时先用单 tap 兜底；9 tap 模糊作为可选画质特性。
- LRU 需优先保证可见根 node-tile 的驻留，再分配历史中间节点和预测预取的预算；192 MiB 是否足够需结合实际命中统计验证。

## 7. 与白板同窗口／HARDWARE／DComp 计划的关系

### 7.1 目前默认背景尚未真正切换

源码仍选择：

```cpp
kWhiteboardPatternOwner = WhiteboardPatternOwner::Freeze;
```

所以虽已有 Draw3 的背景 under pass 代码，当前默认路径仍以 Freeze 的 D2D／WARP 背景绘制和 ULW 表面提交为主。背景相位变化时会使场景失效，经过 `GetDC(COPY)`、`SubmitFreezeSurface()` 等路径；背景调度支持 30／60 FPS，而 Draw3 墨迹目标是 120 FPS。**“实现中有某种路径”不代表“默认已经使用”。**

- [背景归属开关](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.WhiteboardModel.h#L23)
- [背景刷新与提交](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp#L497)
- [30／60 FPS 调度](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp#L609)

### 7.2 建议层次与平台后端

| 逻辑层 | 建议 | 关键约束 |
|---|---|---|
| 稳定墨迹 | 世界缓存 tile 直接显示、相机变换 | 保持透明像素、局部失效和精确复原 |
| PDF／图片／网格 | 自己的 GPU 缓存与纹理，同步共享相机变换 | 不让橡皮擦除背景；不同 DPI／缩放精度要一致 |
| L0／L1 动态层 | 保留局部更新和正确 Add/Retain 操作 | 不能因为分层而造成一帧相位不一致 |
| 大橡皮／激光／其他游标 | 独立局部显示或 overlay | 避免每次游标移动重组大面积底图 |
| Win8+ DComp 后端 | 采用受支持的 composition surface／swapchain + visual transform | DComp 不能直接任意绑定普通 D3D11 纹理；要验证 Present／同步行为 |
| Win7 SP1 + KB2670838 后端 | 同样世界缓存与相机，普通硬件 HWND 自行合成 | 不依赖 DComp，保留旧系统兼容性 |

DComp 的价值是复用已经栅格化的表面、改变 visual 的 transform（包括平移、缩放）。**同一个 HWND 不等于必须把所有逻辑层提前合为一张每帧重绘的纹理。**

- [Microsoft: DComp bitmap surfaces](https://learn.microsoft.com/en-us/windows/win32/directcomp/bitmap-surfaces)
- [Microsoft: DComp transforms](https://learn.microsoft.com/en-us/windows/win32/directcomp/transforms)
- [Microsoft: IDCompositionVisual::SetContent](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-setcontent)
- [Windows 7 Platform Update 能力范围](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)

### 7.3 关于 Present1 与脏区

当前 `PresentSwapChain()` 只传一个 dirty rect，未使用 scroll rect；漫游状态走整屏 present。后续可以研究多脏矩形或合法的 scroll metadata，但：

- Present1 参数不能取消 GPU 已经做完的 L2 清空、快照复制与混合；
- 必须保证 dirty rect 内像素完整、正确；
- swap chain 的 backbuffer 可能轮换，不得假定当前缓冲已经含有上一帧内容；
- scroll offset 是整数，不能直接处理一般的亚像素／缩放／旋转；
- Win7／Win8+ 的呈现特性需要按实际后端验证。

故顺序应是**优化应用内部像素工作量，再优化 DXGI／DWM 增量呈现**。

- [Present1 参数代码](https://github.com/Alan-CRL/Inkeys/blob/cf97bfb9ba74eaacb2b3902b1e8e53106f936f6f/Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp#L1144)
- [Microsoft: DXGI 1.2 Presentation Improvements](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-2-presentation-improvements)
- [Microsoft: DXGI_PRESENT_PARAMETERS](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_present_parameters)

## 8. 实施顺序：给后续开发 Agent 的任务清单

### P0 — 低风险、优先验证的局部优化

- [ ] **纯橡皮 L0 清理状态化**：避免每活动帧无效清空两张全尺寸操作纹理；工具切换仍能正确清理残留。
- [ ] **记录有效层状态**：L0/L1 无内容或某通道恒等时，选择不采样它的合成路径。
- [ ] **优化大游标合成范围**：分离游标覆盖与底图 dirty，避免旧新位置仅因合并包围矩形而重新合成大片无变化区域；考虑独立表面／Overlay。
- [ ] **测量，而不是仅看 CPU 帧时间**：增加 GPU timestamp、draw 数量、处理像素／dirty 面积、缓存命中／冷构建、Clear/Copy 次数。
- [ ] **连续擦除与抬笔单独验收**：检查完成帧尖峰。

### P1 — 漫游核心优化

- [ ] **热根 tile 直接显示 PoC**：相机更新只影响可见 tile 的位置与采样参数，不再清全屏 L2 并逐块恢复。
- [ ] **缓存显示与历史构建分离**：可见根优先，其次预测／预取；只有内容失效才新建 tile。
- [ ] **完整热缓存取消全屏可信快照更新**：兜底只覆盖缺失区域，并在低档设备可以关闭模糊。
- [ ] **合成融合／专用 shader**：验证可省去的一次复制、空通道采样与状态切换。
- [ ] **内存预算与驻留策略**：记录根 tile 驻留比例、node-tree 对预算的竞争、预取时的驱逐抖动。

### P2 — 白板同窗口与跨平台呈现

- [ ] 正式切换背景到硬件同帧／同相机路径，检查 UI、网格、墨迹和 PDF 是否完全同步。
- [ ] Win8+ 评估 DComp transform + composition surface 的持久显示面；Win7 使用等价的纯硬件自合成后端。
- [ ] 判断全窗口 Present 何时是必要的；测试多 dirty rect／scroll 的收益与正确性。
- [ ] 处理整数／非整数平移、缩放、DPI、屏幕切换和窗口大小改变时的缓存一致性。

### P3 — 复杂场景与极限优化

- [ ] 大橡皮相邻胶囊段的重复覆盖最小化，但保留精确抗锯齿语义。
- [ ] 冷 tile 递归构建细粒度分段、限制 GPU 峰值占用。
- [ ] 对低端 GPU 提供可控的图像质量／模糊回退，但不改变墨迹持久数据的正确性。

## 9. 实测设计与验收标准

### 9.1 必测对照

| 测试 | 观察点 |
|---|---|
| 巨型橡皮**只悬停移动** vs 同尺寸**真正擦除** | 分离游标合成成本与橡皮像素栅格化成本 |
| 小、中、大、超大橡皮；相同路径／速度 | 查看脏区面积、overdraw、GPU 时间如何随面积增长 |
| 连续擦除 vs 抬笔／完成帧 | 确认是否存在结束时 L2 重放尖峰 |
| 无笔迹白板 vs 稀疏墨迹 vs 密集墨迹 | 分离固定屏幕复制成本与内容量因素 |
| 反复漫游**已预热的同一区域** vs 首次进入新区域 | 热缓存显示成本 vs 冷缓存构建成本 |
| 纯色背景 vs 网格背景／PDF 底图 | 分离 Freeze 背景、背景内容本身和合成同步的成本 |
| 整数位移 vs 0.25／0.5 px 亚像素位移 | 验证视觉稳定性与复用正确性 |
| Win11 HARDWARE vs Win7 兼容后端 | 保证优化不引入平台回归 |

重点设备：已报告过大橡皮卡顿的 Intel UHD 630 / 4K 教室一体机；同时使用性能更强设备作对照，避免把驱动或特定机型问题推广为普遍规律。

### 9.2 必收集指标

1. CPU 帧准备／输入／历史恢复／绘制提交耗时；GPU 分段 timestamp（包括 `D3D11_QUERY_TIMESTAMP_DISJOINT`）。
2. 各渲染阶段 GPU 时间：Clear L0/L1/L2、tile restore、快照复制、橡皮 draw、最终合成、游标、背景／PDF。
3. 每帧真实绘制、Clear、Copy、Draw call 数；各脏区总面积／包围矩形面积／实际合成面积。
4. 可见根 tile 命中数、冷构建数、重复排队数、被驱逐数、重建耗时／分位数；LRU 内存占用。
5. 帧时间 p50/p95/p99、丢帧、惯性期间的持续稳定 FPS；Present 的排队等待与 GPU 耗时分开。

查询 GPU timestamp 时**不要同步忙等 `GetData()`**；使用延迟读取，未就绪则跳过或读取更早一帧，避免探针本身造成卡顿。

- [Microsoft: D3D11_QUERY](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query)
- [Microsoft: D3D11_ASYNC_GETDATA_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag)

### 9.3 建议验收标准（设计目标，非当前达成状态）

- **热缓存／无活动笔迹／纯漫游**：历史笔迹重放 = 0；新建缓存 tile = 0；整屏可信快照复制 = 0；不清除或合成空的 L0/L1；仅视口变换、有效缓存采样、必要的后端输出。
- **有活动大橡皮**：对不变内容不重复工作；L0 恒等时不整层清除；shader 跳过无效纹理采样；游标位置变化不扩大墨迹重建范围。
- **冷缓存／高速惯性**：优先显示已有有效数据，缺失区域可局部降级；GPU 建 tile 的峰值工作量有界、可测量。
- **兼容正确性**：笔迹颜色、透明混合、部分擦除、撤销恢复、形状编辑、画布切换、白板背景和多显示器不得因缓存快路径而出现旧内容泄漏或显示错位。
- **最终 FPS 目标**：4K 120 FPS 意味着单帧预算 8.33 ms；先达到“无冗余工作”的结构性指标，再用目标 GPU 的 p95/p99 帧时间评估是否稳定满足。

## 10. 后续修改需防止的误区

1. **不要把缓存命中误认为零 GPU 工作**：还可能有逐 tile 绘制、复制、快照、混合。
2. **不要把 `InkRenderer::CopyResource(..., rect)` 当成整纹理 CopyResource**：它在普通合成中是封装后的 `CopySubresourceRegion`；只有可信快照等其他路径确有整张复制。
3. **不要把 Present1 的 dirty rect 当成应用内部绘制脏区优化**：它不能撤销此前的整屏 Clear、Copy、Draw。
4. **不要认为漫游一定重新画全部历史**：实际历史缓存树有命中快路径。
5. **不要假设同窗口自动达到 120 FPS**：需同时解决后台背景更新频率、共享相机、L2 恢复、最终合成与 Present。
6. **不要让橡皮擦掉背景**：背景内容与透明墨迹应在正确的层次关系中合成。
7. **不要把旧快照模糊当成免费**：特别是接近全屏的 9-tap 采样。
8. **不要只拿平均 FPS 判定效果**：极端大橡皮和冷缓存可能主要影响 p95/p99、抬笔帧和帧尾延迟。
9. **不要在纯平移中无差别失效世界 tile**：当前缓存键没有视口偏移，此设计应保留。
10. **不要仅因纹理预算为 192 MiB 就认定当前视口始终完整驻留**：中间历史节点及预取会消耗缓存。

## 11. 文件入口索引

| 工作内容 | 优先查看文件 |
|---|---|
| 视口／恢复任务／漫游 / Frame Dirty | `Draw3.DrawingController.cpp` |
| 惯性与恢复预算 | `Draw3.CanvasNavigation.cpp` |
| 历史合成树／叶节点 | `Draw3.InkHistory.cppm` |
| GPU tile 缓存／恢复 | `Draw3.InkHistoryGpu.cpp` |
| 墨迹段／橡皮提交 | `Draw3.StrokeGeometry.cpp`、`Draw3.RendererPrimitives.cpp` |
| 清层／复制／resolve | `Draw3.Renderer.cpp` |
| shader／混合公式／快照 blur | `Assets/inkPixelShader.hlsl`、`Assets/inkVertexShader.hlsl` |
| L0 清理条件 | `Draw3.InkPrediction.cppm` |
| 可信快照 | `Draw3.RendererSnapshot.cpp` |
| 背景归属与冻结表面 | `Draw3.WhiteboardModel.h`、`UI/Whiteboard/Whiteboard.cpp` |
| 背景调度 | `UI/RenderPipeline/RenderPipeline.cpp` |
| Present1／交换链 | `Draw3.TransparentPresentation.cpp` |

> **最后的建议**：实施时先做“橡皮 P0 局部优化 + GPU 分段诊断”，再对“热缓存直显 vs 旧方案”的惯性漫游做同机 A/B。最有价值的架构改进并不是单纯扩大缓存，而是让缓存**成为显示层直接可用的内容**，让相机移动不再触发整个屏幕 L2 的恢复与快照维护。
