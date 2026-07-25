# CPU/GPU Contracts

## Structured Buffers

### InkPoint

C++ `draw3::InkPoint` 与 HLSL `InkPoint` 都是：

```text
float2 position + float radius + float time = 16 bytes
```

C++ 用 `static_assert(sizeof(InkPoint) == 16)`，D3D buffer 的 `StructureByteStride` 使用 `sizeof(InkPoint)`，HLSL 在 `t0` 读取。

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
| `padding[3]` | `globalPadding`（cursor shape 时为 outline RGB） |

它绑定在 VS `b0` 与 PS `b0`。`operatorKind` 只在 pixel shader 使用，但仍属于同一共享常量缓冲区。

`LaserStyleConstants` 绑定到 VS/PS `b1`，总大小固定为 `112 bytes`：

| C++ | HLSL |
|---|---|
| `radii` | `laserRadii`（白芯、红边、外晕、散射带） |
| `coreColor` | `laserCoreColor` |
| `scatterColor` | `laserScatterColor` |
| `borderColor` | `laserBorderColor` |
| `edgeColor` | `laserEdgeColor`（红粉外缘高亮） |
| `glowColor` | `laserGlowColor` |
| `parameters.x/y/z/w` | `laserParameters.x/y/z/w`（组 opacity、DPI scale、外缘 glow 下/上阈值） |

新增字段必须保持 16 字节对齐，并同步 `renderer.cppm/.cpp`、`ink.hlsli` 与 shape `7/8/9/10` 的绑定。

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
| `t6` | LaserStableCoverage (`R8G8B8A8_UNORM`) |
| `t7` | LaserLiveCoverage (`R8G8B8A8_UNORM`) |
| `s0` | OperatorSampler |

`ApplyOperatorLayers` 绑定 PS `t1..t5` 时为 VS `t3` 留空槽。修改数组顺序前必须按寄存器表核对。

Laser coverage 写入只绑定单张 coverage RTV，并以 `D3D11_BLEND_OP_MAX` 增量累积四通道 `(core, scatter, border, glow)`；resolve 才绑定 `t6/t7`，完成后必须解除 SRV，随后才能清空或 resize 纹理。

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
- `8`：Laser stable/live coverage resolve。
- `9`：Laser Hover/Touch 笔尖。
- `10`：Laser 稀疏粒子。

`globalOperatorKind`：

- `0` Draw：输出 premultiplied Add 和 `1-alpha` Retain。
- `1` Erase：Add 为零，Retain 为 `1-coverage`。

这些数值虽然部分未暴露在 enum 中，但已经是 CPU/GPU 协议；不要任意复用。

Cursor `InkData[0]` 使用 `pos=center, r=halfWidth, time=halfHeight`；`InkData[1].pos` 使用 `x=outlineWidth, y=fillAlpha`。`globalColor` 保存 fill RGB 与整体 opacity，`globalPadding` 保存 outline RGB。Cursor shape 不写 operator texture，而是对 backbuffer 使用 `operatorResolveBlendState` 直接计算 `premultiplied Add + Retain * Destination`。

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

内部颜色是 premultiplied alpha。改变 blend state、PS 输出或清屏值时，必须把数学公式作为整体验证。

## Geometry And Bounds

- VS 为圆胶囊和荧光笔固定矩形 sweep 生成覆盖形状的 quad/AABB。
- PS 使用 signed distance 与 `fwidth`/`smoothstep` 做抗锯齿；高亮 sweep 的零等值线由 X/Y/线段法线半平面交集确定。
- CPU dirty bounds 必须至少覆盖 VS 生成范围；当前普遍预留 2px 几何扩展和 3px bounds padding。
- Laser shape `7` 的 `InkPoint.r` 是白芯半径；VS/PS 分别按 `14/2.5`、`7.5/2.5` 比例扩展光晕和红边，CPU bounds 必须使用同一最大光晕比例并逐点读取压力半径。
- 普通笔零长度或一端圆包含另一端时退化为较大端点圆；高亮零长度退化为固定竖直矩形。
- `InkPoint` 中出现 NaN 时 PS discard；CPU 仍应避免生成非有限输入。
