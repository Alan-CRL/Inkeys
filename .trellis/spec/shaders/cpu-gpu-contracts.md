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
float2 direction1
float2 direction2
float radius
float startExtension
float endExtension
uint type
```

总大小 `48 bytes`，HLSL 在 `t3` 读取。`HighlighterPrimitiveType` 当前值：

- `0` Body
- `1` RoundJoinSector
- `2` RoundJoinCircle
- `3` ShortMark

修改布局时必须同步 `.cppm`、buffer stride、`ink.hlsli`、VS 解包和 PS 分支。

## Constant Buffer

`GlobalShaderConstants` / `ScreenBuffer` 当前为 48 bytes：

| C++ | HLSL |
|---|---|
| `width`, `height` | `screenWidth`, `screenHeight` |
| `shapeType` | `globalShapeType` |
| `bufferOffset` | `globalBufferOffset` |
| `color` | `globalColor` |
| `operatorKind` | `globalOperatorKind` |
| `padding[3]` | `globalPadding` |

它绑定在 VS `b0` 与 PS `b0`。`operatorKind` 只在 pixel shader 使用，但仍属于同一共享常量缓冲区。

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
| `s0` | OperatorSampler |

`ApplyOperatorLayers` 绑定 PS `t1..t5` 时为 VS `t3` 留空槽。修改数组顺序前必须按寄存器表核对。

## Shape And Operator Modes

`globalShapeType` 当前同时承担几何和全屏合成选择：

- `0`：变半径圆胶囊。
- `1`：L1/L0 coverage union 合成矩形。
- `2`：L1 后再应用 L0 的 ordered 合成矩形。
- `3`：荧光笔 primitive。

`globalOperatorKind`：

- `0` Draw：输出 premultiplied Add 和 `1-alpha` Retain。
- `1` Erase：Add 为零，Retain 为 `1-coverage`。

这些数值虽然部分未暴露在 enum 中，但已经是 CPU/GPU 协议；不要任意复用。

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

- VS 为圆胶囊和荧光笔 primitive 生成覆盖形状的 quad/AABB。
- PS 使用 signed distance 与 `fwidth`/`smoothstep` 做抗锯齿。
- CPU dirty bounds 必须至少覆盖 VS 生成范围；当前普遍预留 2px 几何扩展和 3px bounds padding。
- 零长度或一端圆包含另一端时，pixel shader 退化为较大端点圆。
- `InkPoint` 中出现 NaN 时 PS discard；CPU 仍应避免生成非有限输入。
