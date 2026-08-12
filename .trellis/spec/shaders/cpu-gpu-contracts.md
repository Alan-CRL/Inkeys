# CPU/GPU Contracts

## Structured Buffers

### InkPoint

C++ `draw3::InkPoint` 与 HLSL `InkPoint` 都是：

```text
float2 position + float radius + float time = 16 bytes
```

C++ 用 `static_assert(sizeof(InkPoint) == 16)`，D3D buffer 的 `StructureByteStride` 使用 `sizeof(InkPoint)`，HLSL 在 `t0` 读取。

### ShapePrimitive

Shape 不新增 buffer、texture 或 SRV；每项直接占用 `t0 InkData` 中两个连续 `InkPoint` 槽：

```text
start = float2 起点 + float 半线宽 + 0
end   = float2 终点 + 0 + 0
总大小 = 32 bytes
```

C++ 必须保持 `static_assert(sizeof(ShapePrimitive) == 32)`。`globalBufferOffset` 仍以 `InkPoint` 槽计数，所以 VS 用 `offset + itemIndex * 2` 解包；renderer 一批同类 primitive 上传连续 32-byte 项并调用 `Draw(6 * count)`。禁止为虚线增加 CPU 短划数组或第二套 structured buffer。

### HighlighterPrimitive

字段顺序必须保持：

```text
float2 p1
float2 p2
float2 halfSize
```

总大小 `24 bytes`，HLSL 在 `t3` 读取。`halfSize` 当前固定为 `(1.25, 25)`；`p1 == p2` 表示单点矩形，否则表示固定矩形沿 `p1→p2` 的 sweep。

修改布局时必须同步 `.cppm`、buffer stride、`ink.hlsli`、VS 解包和 PS sweep 分支。

## Constant Buffer

`GlobalShaderConstants` / `ScreenBuffer` 当前为 48 bytes：

| C++ | HLSL |
|---|---|
| `width`, `height` | `screenWidth`, `screenHeight` |
| `shapeType` | `globalShapeType` |
| `bufferOffset` | `globalBufferOffset` |
| `color` | `globalColor` |
| `operatorKind` | `globalOperatorKind` |
| `padding[3]` | `globalPadding`（cursor shape 时为 outline RGB；Shape `16..19` 时 `x` 为 `4 DIP * dpiScale` 圆角像素值） |

它绑定在 VS `b0` 与 PS `b0`。`operatorKind` 只在 pixel shader 使用，但仍属于同一共享常量缓冲区。

`LaserStyleConstants` 绑定到 VS/PS `b1`，总大小固定为 `112 bytes`：

| C++ | HLSL |
|---|---|
| `radii` | `laserRadii`（基准白芯半径、基准实体外半径、固定漫反射宽度、基准散射半宽） |
| `coreColor` | `laserCoreColor` |
| `scatterColor` | `laserScatterColor` |
| `borderColor` | `laserBorderColor` |
| `edgeColor` | `laserEdgeColor`（RGB 为红粉外缘高亮，alpha 为 RGB 混合强度） |
| `glowColor` | `laserGlowColor`（alpha 为实体边界处的漫反射峰值） |
| `parameters.x/y/z/w` | `laserParameters.x/y/z/w`（组 opacity、DPI scale、外缘 glow 下/上阈值） |

新增字段必须保持 16 字节对齐，并同步 `renderer.cppm/.cpp`、`ink.hlsli` 与 shape `7/8/9/10/11/12/13` 的绑定。

`HistoryCacheConstants` 绑定到 VS/PS `b2`，总大小固定为 `48 bytes`：两个
`float4` 分别保存目标矩形和采样 UV，尾部四个 `uint` 保存 Earlier/Later/Source
slice 与 padding。普通墨迹和 Laser pass 不读取 `b2`；history pass 结束时必须从
VS/PS 显式解绑。

`TrustedL2SnapshotShaderConstants` 绑定到 VS/PS `b3`，总大小固定为 `48 bytes`：
`targetRect` 保存 backbuffer 目标矩形，`sourceUvRect` 保存可信快照真实交集的 UV，
`blurUv` 保存沿内容运动方向的 UV 模糊步长和采样参数。C++ 必须保持
`static_assert(sizeof(TrustedL2SnapshotShaderConstants) == 48)` 和 16 字节对齐；
snapshot pass 的全部出口都必须解绑 VS/PS `b3`。

## Resource Registers

| Register | Resource |
|---|---|
| `b0` | Screen/global constants |
| `t0` | InkData |
| `t1` | StableOperatorAdd |
| `t2` | StableOperatorRetain |
| `t3` | HighlighterData |
| `t4` | LiveOperatorAdd |
| `t5` | LiveOperatorRetain |
| `t6` | LaserCompositedColor，已完成轨迹的预乘颜色 (`R8G8B8A8_UNORM`) |
| `t7` | LaserStrokeCoverage，可复用的单笔 coverage scratch (`R8G8B8A8_UNORM`) |
| `t8` | LaserParticleData，固定 2048 槽、每槽 80 bytes 的粒子池 SRV |
| `t9` | LaserLiveCoverage，单 contact 当前 L0/prediction coverage (`R8G8B8A8_UNORM`) |
| `t10` | HistoryEarlierAdd，单 slice `Texture2DArray` SRV (`B8G8R8A8_UNORM`) |
| `t11` | HistoryEarlierRetain，单 slice `Texture2DArray` SRV (`R16_FLOAT`) |
| `t12` | HistoryLaterAdd，单 slice `Texture2DArray` SRV (`B8G8R8A8_UNORM`) |
| `t13` | HistoryLaterRetain，单 slice `Texture2DArray` SRV (`R16_FLOAT`) |
| `t14` | TrustedL2Snapshot，最后一次完整清晰视口的 `B8G8R8A8_UNORM` 视觉快照 |
| `s0` | OperatorSampler |

`ApplyOperatorLayers` 绑定 PS `t1..t5` 时为 VS `t3` 留空槽。修改数组顺序前必须按寄存器表核对。

Laser coverage 写入绑定当前 coverage RTV（稳定前缀为 `t7`、实时尾部为 `t9`），并以 `D3D11_BLEND_OP_MAX` 累积单支笔的四通道 `(core, scatter, solid shell, diffuse)`。shape `8` 把 `t7` 解析为材质；shape `13` 同时采样 `t7/t9`，逐通道取 `max` 后只解析一次材质；两者均按 Down 顺序 source-over 到 backbuffer 或 `t6`。shape `11` 再把 `t6` 以整组 opacity 叠到 backbuffer。每次 resolve、Resize 或切换 RTV 前必须解除 `t6..t9` SRV，随后才能把对应纹理切回 RTV。

Laser 矩形 shape `8/11/12/13` 不读取 `InkData`：CPU 把 `(left, top, right, bottom)` 写入这些 shape 未使用的 `globalColor`，VS 按 `SV_VertexID` 生成 quad。矩形 pass 不得 Map/Unmap `inkDataBuffer`，也不得绑定 VS `t0`；shape `0..7/9/10` 对 `globalColor` 的既有含义保持不变。

## Shape And Operator Modes

`globalShapeType` 当前同时承担几何和全屏合成选择：

- `0`：变半径圆胶囊。
- `1`：L1/L0 coverage union 合成矩形。
- `2`：L1 后再应用 L0 的 ordered 合成矩形。
- `3`：荧光笔 primitive。
- `4`：瞬态 Cursor Circle。
- `5`：瞬态 Cursor Rectangle。
- `6`：瞬态 EraserGripCircle。
- `7`：Laser 可变端点压力 coverage 胶囊写入。
- `8`：解析 `t7` 单笔 coverage。
- `9`：Laser Hover/Touch 笔尖。
- `10`：Laser 稀疏粒子。
- `11`：解析 `t6` 稳定预乘颜色和整组 opacity。
- `12`：关闭混合后，以矩形覆盖写零局部清理当前 coverage（`t7` 或 `t9`）。
- `13`：逐通道 `max(t7, t9)` 后单次解析 Laser 材质，用于单 contact 增量快路。
- `14`：按 `Later(Earlier(Below))` 组合两个 history operator tile。
- `15`：把一个 history operator tile 应用到 L2 的目标 Canvas 矩形。
- `16`：固定宽度圆头实线胶囊。
- `17`：固定宽度圆头虚线；中心线实线段 `4 * width`、中心线空隙 `6 * width`，圆头侵占后可见线段与可见空隙接近 `1:1`，周期在 PS 中解析。
- `18`：边界居中的圆角矩形边框。
- `19`：无额外边框的圆角填充矩形。
- `20`：把 `t14` 的可信 L2 快照按 `b3` 目标/UV 矩形重投影到 backbuffer，并可沿内容运动方向模糊。

`globalOperatorKind`：

- `0` Draw：输出 premultiplied Add 和 `1-alpha` Retain。
- `1` Erase：Add 为零，Retain 为 `1-coverage`。

这些数值虽然部分未暴露在 enum 中，但已经是 CPU/GPU 协议；不要任意复用。

Cursor `InkData[0]` 使用 `pos=center, r=halfWidth, time=halfHeight`；`InkData[1].pos` 使用 `x=outlineWidth, y=fillAlpha`。`globalColor` 保存 fill RGB 与整体 opacity，`globalPadding` 保存 outline RGB。Cursor shape 不写 operator texture，而是对 backbuffer 使用 `operatorResolveBlendState` 直接计算 `premultiplied Add + Retain * Destination`。

## Scenario: Trusted L2 Snapshot Fallback

### 1. Scope / Trigger

修改 shape `20`、可信快照资源、`b3/t14`、平移时 backbuffer 合成、模糊采样或 snapshot 生命周期时，必须同步本节与 [Runtime and Rendering](../native/runtime-and-rendering.md)。

### 2. Signatures

- `TrustedL2SnapshotShaderConstants { float4 targetRect; float4 sourceUvRect; float4 blurUv; }`
- `TrustedL2SnapshotCompositeRequest { currentViewportX/Y, contentMotionX/Y, blurDip, dpiScale, RECT rect }`
- `InkRenderer::RefreshTrustedL2Snapshot(viewportX, viewportY) -> bool`
- `InkRenderer::CompositeTrustedL2SnapshotToBackBuffer(request) -> bool`
- `InkRenderer::InvalidateTrustedL2Snapshot()`

### 3. Contracts

- 快照只在当前页全部可见 composition tile 已清晰恢复后，从 L2 `CopyResource` 刷新，并记录该时刻 viewport origin；不得包含 L1、L0、Laser、粒子或 cursor。
- 合成前先求 snapshot 世界视口与 current 世界视口的真实交集，再由 `b3` 同时限定目标矩形和 `t14` UV。透明 border sampler 用于模糊采样；禁止 clamp/stretch 快照边缘来填充未知区域。
- shape `20` 以专用 under blend 把快照画在已恢复清晰 tile 下方，清晰 L2 随后覆盖。`blurDip` 在 CPU 钳制到 `[0,12]`，乘 DPI 后只沿 `contentMotion` 方向形成 UV 步长；零运动或低速可为零模糊。
- pass 只写 backbuffer。其结果永不 Copy/Resolve 到 L2、operator layer、history preimage、composition texture 或文档。
- VS/PS 使用 `b3`，PS 使用 `t14/s0`；绘制完成和所有失败出口显式解绑 `t14`、VS/PS `b3`、sampler，并恢复普通 blend/raster/viewport 状态。释放 snapshot 资源前也先解绑 `t14`。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| snapshot 无效、资源/CB/sampler/blend 缺失 | 返回 false，不改变权威层 |
| viewport 或请求字段非有限、dpiScale 非正 | 返回 false，不 Map/Draw |
| 真实覆盖交集为空 | 返回 false，不拉伸或伪造墨迹 |
| CB Map 失败 | 不 Draw，并清理可能绑定的 snapshot 状态 |
| blurDip < 0 或 > 12 | 钳制到 0 或 12 DIP；方向仍来自有限 contentMotion |
| Resize/page/Undo | invalidate；只有新的完整清晰 L2 才能再次 refresh |

### 5. Good / Base / Bad Cases

- Good：高速平移时交集区先显示有方向模糊的旧清晰像素，随后清晰 tile 覆盖；新暴露的世界区域保持透明。
- Base：慢速平移使用零模糊重投影；静止且可见 tile 全清晰后刷新一次快照。
- Bad：使用 wrap/clamp 拉满 backbuffer、把 L0/Laser 一起捕获，或把 snapshot draw 当成已恢复 L2。

### 6. Tests Required

- 静态断言 CB 为 48 bytes 且 16 字节对齐，HLSL/CPU 字段顺序一致，shape `20`、`b3`、`t14` 没有与现有 pass 冲突。
- 单元测试覆盖世界交集、源/目标矩形、零/双方向运动、300 DIP/s 清晰阈值、12 DIP 上限、无交集透明和失效签名。
- 静态检查每个 exit 的 `t14/b3/sampler` 解绑和 blend/raster/viewport 恢复；人工 D3D Debug Layer 验证无 SRV/RTV 冲突，视觉检查边缘不拉伸且清晰 tile 位于兜底上层。

### 7. Wrong vs Correct

Wrong：`snapshot 全屏 clamp 采样 -> 模糊结果 Copy 到 L2 -> 标记 viewport 已清晰。`

Correct：`真实世界交集 -> shape 20 只画 backbuffer 下层 -> Canvas-local tile 恢复 L2 -> 全部可见清晰后再刷新 snapshot。`

## Affine Operator Layers

每个临时层表示：

```text
Result = Add + Retain * Destination
```

- stroke 写入 Add target 使用 `D3D11_BLEND_OP_MAX`。
- stroke 写入 Retain target 使用 `D3D11_BLEND_OP_MIN`。
- resolve 使用 dual-source blend：source Add + source1 Retain × destination。
- coverage union 取 `max(stableAdd, liveAdd)` 与 `min(stableRetain, liveRetain)`。
- ordered 模式计算 `liveAdd + liveRetain * stableAdd` 和 `liveRetain * stableRetain`。
- history tree 的组合固定为 `Add = Later.Add + Later.Retain * Earlier.Add`、
  `Retain = Later.Retain * Earlier.Retain`。

History operator array 的 RTV 和 SRV 都必须限制为单个 array slice。即使输入和输出
使用同一 texture array，只要 slice 不同也不能绑定覆盖整个 array 的 SRV，否则 D3D11
会把它判定为与输出 RTV 重叠。每个 history pass 的全部出口都必须解绑 `t10..t13`、
`b2` 和 RTV，并恢复全画布 viewport/raster state。

内部颜色是 premultiplied alpha。改变 blend state、PS 输出或清屏值时，必须把数学公式作为整体验证。

## Geometry And Bounds

- VS 为圆胶囊和荧光笔固定矩形 sweep 生成覆盖形状的 quad/AABB。
- PS 使用 signed distance 与 `fwidth`/`smoothstep` 做抗锯齿；高亮 sweep 的零等值线由 X/Y/线段法线半平面交集确定。
- CPU dirty bounds 必须至少覆盖 VS 生成范围；当前普遍预留 2px 几何扩展和 3px bounds padding。
- Shape `16/17` 的 VS 生成扩宽线段 OBB，PS 分别计算整段 capsule 或 analytic dashed capsules；零长度必须退化为半线宽圆点。Shape `18/19` 的 VS 先对任意方向端点取 `min/max`，PS 使用 rounded-box SDF；圆角把 `globalPadding.x` 钳制到短边一半，Outline 使用 `abs(boxDistance) - halfWidth`，Filled 直接使用 box distance。
- Shape dirty/history bounds 必须与上述几何同步：Line/Outline 扩展半线宽和 AA，Filled 只扩展 AA；Outline history 遍历四边而非整个内部，Filled 覆盖完整规范化矩形。
- Laser shape `7` 的 `InkPoint.r` 是红色实体外半径。96 DPI 基准实体半径为 2.5px，白芯半径是实体半径的 `1/3`，漫反射在实体轮廓外固定扩展 5px；压力只改变实体/白芯/散射比例，不改变 5px 漫反射。PS 必须复用实体 signed distance，以平方曲线令 coverage 在实体边界为 1、5px 外缘为 0；红粉高光只混合 diffuse RGB，禁止通过额外 source-over 层抬高渐变 alpha。VS、PS、Hover/Touch `LaserDot.radius`、粒子红边锚点和 CPU bounds 必须复用 `renderer.cppm` 的尺寸契约。
- 普通笔零长度或一端圆包含另一端时退化为较大端点圆；高亮零长度退化为固定竖直矩形。
- `InkPoint` 中出现 NaN 时 PS discard；CPU 仍应避免生成非有限输入。

## Scenario: Ordered Multi-Laser Composition

### 1. Scope / Trigger

- 修改 Laser 尺寸、coverage 通道、资源寄存器、shape 编号、批次生命周期或多 contact 层级时，必须按本节同步 CPU/HLSL。

### 2. Signatures

- `DrawLaserCoverage(points)`：把一支笔写入当前 coverage RTV（`t7` 或 `t9`）。
- `ClearLaserCoverageRect(rect)` / `bool ClearLaserLiveCoverageRect(rect)`：shape `12` 局部覆盖清零 `t7` / `t9`；增量 live clear 失败返回 `false`。
- `ResolveLaserStrokeCoverage(dst, rect, opacity)`：shape `8` 将 `t7` source-over 到 `dst`。
- `bool ResolveLaserIncrementalCoverage(dst, rect, opacity)`：shape `13` 对 `t7/t9` 取逐通道 MAX 后 source-over 到 `dst`；资源或 pass 失败返回 `false`。
- `ResolveLaserCompositedColor(dst, rect, opacity)`：shape `11` 将 `t6` source-over 到 `dst`。
- `EnsureLaserIncrementalCoverageResources()`：只在绘制线程按需创建 `t9`，失败后本会话禁用增量快路。

### 3. Contracts

- 单 contact 的稳定真实前缀写入 `t7`，当前真实尾部和 prediction 写入独立 `t9`；shape `13` 对两张 coverage 逐通道取 MAX 后只解析一次材质。同一笔的自交和 L1/L0 重叠因此幂等，prediction 回缩不能污染 `t7`。
- 多 contact 或资源不可用时回退现有逐笔 `t7` scratch；不同笔禁止在同一次 MAX 中合并，当前批次一旦回退便保持到最后 Bake。
- 多 contact fallback 为每层保存稳定累计 bounds、稳定 delta、旧 live 与新 live bounds；`frameDirty` 只并入稳定 delta 和旧/新 live。绘制时先求 `layer.bounds ∩ frameDirty`，只清理该交集，并在 scissor 下上传/绘制该层完整几何；scissor 只能减少像素工作，不能裁掉几何输入或改变 Down 顺序。
- 未烘干层按 Down 顺序逐支解析，后 Down 的整支笔永远位于上层；较早结束的 contact 必须保留最终 CPU 几何，直到同批最后一个 contact 抬起。
- `frameDirty` 必须在基础层合成前闭合旧/新 live、稳定 delta、粒子、cursor 和 fade 区域；粒子或 cursor dirty 与稳定 Laser 相交时，shape `13` 在同一 dirty 区域重放。
- 最后 Up 后按同一顺序烘入 `t6`；单 contact 快路把 coverage pair 一次 resolve，多 contact 继续完整 Bake。resize 只复制 `t6` 交集，`t7/t9` 为空并由 CPU 几何重建。Laser 永不写入 L2。

### 4. Validation & Error Matrix

- 空点集或空矩形 → 跳过绘制，不改变目标。
- SRV/RTV 缺失或常量映射失败 → 当前 pass 返回，不绑定冲突资源。
- Cancelled contact → 标记旧 bounds 为脏，不解析或烘干该层。
- Fade 完成/clear → 同步清空 `t6`、`t7`、未烘干层和全部新旧 bounds。
- `t9` 创建/Resize 失败 → 释放部分资源、记录一次诊断、当前会话锁定完整重绘；其他工具和粒子池继续。
- 增量 clear/upload/resolve 返回失败 → 不推进稳定游标，清空 `t7/t9`，记录 `coverage_submission_failed` 或 `coverage_resolve_failed`，并在同帧切到完整重绘。
- multi-contact 层与最终 `frameDirty` 无交集 → 跳过该层；有交集 → clear/draw/resolve 都限制在同一交集，draw 结束必须恢复非 scissor rasterizer state。
- Resize → `t7/t9` 重新创建为空，活动 CPU 几何下次帧重建；Present failure → 保留仍有效的 coverage，发布 full-present 请求并在下一帧重新解析旧/新 dirty bounds。

### 5. Good/Base/Bad Cases

- Good：两支交叉笔分别 MAX 后按 Down 顺序 resolve，上层红色实体可覆盖下层白芯。
- Base：单支笔自交仍只解析一次 coverage，不在交叉处重复加深。
- Bad：先把所有 contact MAX 到同一 coverage；这会丢失笔身份，无法实现有序覆盖。

### 6. Tests Required

- 断言 96 DPI 的实体直径 5px、白芯直径约 1.67px、漫反射每侧 5px，且 DPI/压力换算符合固定漫反射契约。
- 静态核对 `LaserDiffuseCoverage(0)=1`、`LaserDiffuseCoverage(diffuseExtent)=0` 且区间内单调；`ResolveLaserMaterial` 在红色实体外只能由单一 diffuse 层决定 alpha，红粉高光不得再次 source-over 抬高透明度。
- 断言 `LaserDot.radius`、固定宽度轨迹半径和 Touch/Hover 尺寸一致，`LaserStyleConstants == 112 bytes`。
- 断言 CPU dirty bounds 覆盖实体半径、固定漫反射和 AA padding；人工验证反向 Down 顺序、较老笔继续书写、取消、resize、Hold/Fade 与静态 Hold 零 Present。
- 增量状态测试必须断言时间保护边界单调推进、L1/L0 共享一个连接点、prediction 回缩不后退稳定游标、自交 dirty union 不丢失、第二 contact 锁定 fallback、Resize/Clear/resource failure 重建，以及 `max(t7,t9)` 与完整 coverage union 的 CPU 等价性；静态核对 t9、shape `13` 和 t6-t9 解绑契约。

### 7. Wrong vs Correct

#### Wrong

```cpp
// 把 stable/live 分开 source-over，会让交界处的同一段材质重复叠加。
ResolveLaserStrokeCoverage(backBuffer, stableRect);
ResolveLaserStrokeCoverage(backBuffer, liveRect);
```

#### Correct

```cpp
if (singleContact && liveCoverageAvailable) {
    DrawLaserCoverage(stableDelta);       // t7，MAX 持久化
    ClearLaserLiveCoverageRect(oldLive);  // t9，只清瞬态
    DrawLaserCoverage(liveAndPrediction); // t9
    ResolveLaserIncrementalCoverage(backBuffer, dirty); // max(t7, t9) 一次材质解析
} else {
    for (const auto& layer : layersInDownOrder) {
        ClearLaserCoverageRect(layer.bounds);
        DrawLaserCoverage(layer.points);
        ResolveLaserStrokeCoverage(backBuffer, layer.bounds);
    }
}
```

## Scenario: D3D11 Laser Particle Compute Pipeline

### 1. Scope / Trigger

修改 `draw3.laser_particles`、粒子镜像布局、CS 常量、资源寄存器、shape `10` 或粒子 dirty 策略时，必须同步本节。

### 2. Signatures

- `LaserGpuParticle`，固定 `80 bytes`
- `LaserParticleEmissionRequest { positionX, positionY, tangentX, tangentY, entityRadius, count, seedBase }`
- `LaserParticleDirtyTracker::Snapshot(nowQpc) -> { activeBounds, hasActive, expiredAny }`
- `LaserParticleSystem::Initialize/Configure/Step/Simulate/Emit/Reset`
- `LaserParticleSystem::Step(wallDt, motionDt, simulateExisting, emissionRequests)`
- `InkRenderer::DrawLaserParticles()` 固定调用 `DrawInstanced(6, 2048, 0, 0)`

### 3. Contracts

- 容量是编译期契约：粒子固定 `2048` 槽，每槽 `80 bytes`，结构化缓冲总计 `163840 bytes`；粒子缓冲、SRV/UAV、两个 CS 和两个常量缓冲只在 renderer Init/Release 创建销毁，Resize 不重建。buffer 创建不提供 CPU 零数组，资源就绪后必须用 UpdateCS 的 `resetAll` 在 GPU 初始化；reset 失败则释放资源并标记不可用。
- CS 资源表：

| Stage | Register | Resource |
|---|---|---|
| UpdateCS/EmitCS | `u0` | `RWStructuredBuffer<LaserGpuParticle>` |
| VS shape 10 | `t8` | 同一粒子缓冲的 SRV |

- Update 常量为 `32 bytes`：`float4(wallDeltaSeconds, motionDeltaSeconds, shrinkStartTravelRatio, endRadiusScale)`、`uint2(resetAll, particleCapacity)` 和 `float2` padding。Emit 常量为 `112 bytes`：`uint4 + 6*float4`，依次承载循环槽/数量/容量/seed、出生位置/切线、实体半径/核心比例/寿命范围、速度/偏转/半径范围、亮度/呼吸振幅、呼吸频率/渐入/尺寸分布指数，以及尺寸-亮度和尺寸-射程相关度。CPU 结构必须 `static_assert(size % 16 == 0)` 并与 HLSL 字段顺序一致。
- `LaserGpuParticle` 只保存屏幕位置、固定速度、年龄/寿命、累计/最大行程、出生/当前半径、Alpha、基础/当前亮度、呼吸参数、seed/alive 和一个尾部 padding；CPU 必须用 `sizeof/offsetof`、HLSL 必须用字段顺序共同锁定 `80 bytes`。
- 一帧粒子工作先取得一次 CPU dirty snapshot；controller 的 idle/timer、simulation、draw 和 dirty 决策共同复用该结果。无 active batch、无刚到期批次且无 emission request 时不得 Dispatch 或 `DrawInstanced`；active 刚到期必须再提交一次 update 清理 GPU alive 槽，并用上一帧 bounds 清一次基础层，但不再提交粒子 draw。
- `Step` 前先 `VSSetShaderResources(8, null)`，一次绑定 `u0`，随后按原顺序可选 Dispatch update、逐 request Map emit `b0` 并 Dispatch，最后统一解除 `u0`、`b0` 和 CS。绘制结束再解除 VS `t8`。请求顺序、seed、spawn cursor 和 dispatch 顺序不得改变；禁止依赖 D3D11 自动冲突修复，也禁止重新引入 CS `t0/t1` 路径 SRV。
- EmitCS 使用 CPU 管理的循环槽覆盖最旧粒子，不使用 Append/Consume、计数器、间接绘制或回读。每粒以 50/50 概率选择正/负法线，在所选法线附近均匀偏转 `±25°`；尺寸使用 `pow(random, 2.8)` 偏小分布映射到 `0.28–1.15 DIP`，亮度以 72% 尺寸层级和 28% 独立样本映射到 `0.42–1.0`，速度以 70% 独立样本和 30% 反向尺寸层级映射到 `10–17 DIP/s`，寿命为 `0.7–1.0s`。画笔前向速度不得进入粒子速度。
- Emit 请求的屏幕锚点直接使用本帧 `l0DrawPoints.back()`，切线来自最后一个非退化 L0 段；重复点沿用上一有效切线，首次真实移动前没有有效方向时不发射。prediction 可以改变新粒子的锚点/切线，但不能参与发射密度或存量状态。
- UpdateCS 用实际 wall time 累计年龄，用最多 `1/30s` 的 motion dt 推进；固定速度和 Alpha 都乘 `1-smoothstep(0,1,age/lifetime)`。粒子按实际累计行程计算尺寸，前 10% 保持出生半径，之后 smoothstep 到 20%；达到自身寿命立即死亡。
- 粒子出生后 UpdateCS 不再读取路径、画笔、prediction 或 Up 状态；Up/Cancel 只停止 CPU 新请求。禁止 path slot/generation、弧长、segment cursor、端点阻塞淡出、路径追赶和 prediction correction。
- 每粒写入 `0.42–1.0` 基础亮度、`0.8–1.4Hz` 呼吸频率、随机相位和 `0.12` 振幅；VS 通过 `SV_InstanceID` 取槽，死亡槽生成屏幕外退化图元。呼吸亮度只乘核心/辉光 RGB，不乘 Alpha。
- shape `10` 的 VS 以 `currentRadius * glowRadiusScale + 2 * dpiScale` 得到逐粒辉光范围，默认 `glowRadiusScale=2.0`，并把出生基础亮度通过现有 `p2` 传给 PS；PS 核心直接使用 `borderColor=(1.0, 11/255, 30/255)`，只乘生命周期/呼吸亮度，不再向白混合。辉光使用 `(1.0, 0.32, 0.40)`、峰值 Alpha `0.18` 和 `pow(1.6)` 距离衰减，继续输出预乘 Alpha operator-resolve。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| feature level < 11_0 | `Initialize` 返回 false，renderer 继续创建 VS/PS 和主体资源 |
| CS/buffer/SRV/UAV 任一创建失败 | 释放已建粒子资源、诊断一次、`IsAvailable == false` |
| GPU 初始 reset 失败 | 释放粒子资源并令 `IsAvailable == false`，不得读取未初始化槽 |
| snapshot 无 active 且 request 为空 | 无到期事件时 update、emit 和 draw 全部跳过；若刚到期，执行最后一次 update，仅旧 bounds 进入基础重建且不 draw |
| 无有效 L0 切线 | 不发射并清空该 contact 的小数余量，不形成 Down 补发 |
| 非有限请求位置/切线或退化切线 | Emit 忽略该请求，不移动循环槽 |
| update/emit 常量 Map 失败 | update 失败时解绑并释放粒子系统，避免旧 alive 槽重现；emit 失败跳过该请求且不得移动循环槽，批次末仍统一解绑 Compute 状态 |
| particle pool wrap | 新发射覆盖循环槽，不分配、不回读 |
| UAV/SRV 切换 | 显式解绑后再切换 stage；Debug Layer 不应报告同资源读写冲突 |
| Up/Cancel/prediction 跳变 | 既有粒子的速度、位置和寿命不被控制器修正 |

### 5. Good / Base / Bad Cases

- Good：新粒子从当前 L0 笔尖向两侧法线随机喷射；prediction 尾替换、回缩、急弯和 Up 都不改变既有粒子的屏幕空间轨迹。
- Base：没有活粒子且没有新请求时不提交 compute/draw；设备不支持 CS 时仅无粒子。
- Bad：每帧重建粒子 buffer、让存量粒子重新采样 L0、用 `CopyResource/Map` 回读位置，或同时绑定 `u0` 与 `t8`。

### 6. Tests Required

- 静态断言 `LaserGpuParticle == 80 bytes` 及关键 `offsetof`，解析 HLSL 字段顺序，并断言 Update `32 bytes`、Emit `112 bytes` 且均 16 字节对齐。
- 单元测试 Down 零爆发、静止 6/s、移动 2.5 DIP 密度、72/s 与全局 96 预算、无整数积压、双侧法线与 `±25°` 偏转、偏小尺寸分布、尺寸/亮度和尺寸/射程相关度、`10–17 DIP/s`、`0.7–1.0s` 与 8.5 DIP 行程上限、速度/Alpha 单调性、按行程缩至 20%、比例辉光与核心色相、呼吸与 Alpha 分离、Up/prediction 无修正、批次 dirty 到期和 resize 后重裁剪。
- 静态搜索确认 UpdateCS/EmitCS 仅声明 `u0/b0`，VS 仅在 `t8` 读取，初始化使用空 initial data + GPU reset，controller 通过单个 batched `Step` 提交，并且路径结构、generation、弧长、segment cursor、端点淡出和 prediction correction 不在粒子源码中。
- 完整 ARM64 解决方案构建必须让 FXC 成功编译 VS、PS、UpdateCS、EmitCS；人工启用 D3D11 Debug Layer 检查 SRV/UAV 冲突。

### 7. Wrong vs Correct

#### Wrong

```cpp
context->CSSetUnorderedAccessViews(0, 1, &particleUav, nullptr);
// particle SRV 仍绑定在 VS t8。
context->Dispatch(32, 1, 1);
```

#### Correct

```cpp
const auto snapshot = dirtyTracker.Snapshot(nowQpc); // 每帧只 prune/扫描一次
if (snapshot.hasActive || snapshot.expiredAny || !requests.empty()) {
    particleSystem.Step(wallDt, motionDt,
        snapshot.hasActive || snapshot.expiredAny, requests);
}
if (snapshot.hasActive || !requests.empty()) particleSystem.Draw();
// Step 内只绑定一次 UAV：update -> emit(request 0..N) -> 统一解绑。
```
