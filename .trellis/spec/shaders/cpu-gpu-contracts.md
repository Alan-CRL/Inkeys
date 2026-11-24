# CPU/GPU Contracts

## 参数化图形扩展

2026-10-17起新增独立shape VS/PS，64-byte GpuPrimitive复用t0四槽、独立b4=64bytes；不改变本页旧ink/32-byte ShapePrimitive协议。完整字段、材质合成及生命周期见[参数化图形合同](../native-desktop/shapes.md)。

## Structured Buffers

### InkPoint

C++ `draw3::InkPoint` 与 HLSL `InkPoint` 都是：

```text
float2 position + float radius + float time = 16 bytes
```

C++ 用 `static_assert(sizeof(InkPoint) == 16)`，D3D buffer 的 `StructureByteStride` 使用 `sizeof(InkPoint)`，HLSL 在 `t0` 读取。

`t0 InkData` 保持结构化 SRV 布局。默认动态上传；当实际 Hardware adapter VendorId=0x1D17 且启动快照 EnableZhaoxinCompatibility=true 时，ink/highlighter 大资源为 DEFAULT+CPUAccessFlags0，逐批 offset0、按实际有效字节 UpdateSubresource，VS globalBufferOffset=0。非兆芯、开关关闭和 WARP 保留动态上传。`D3D11_MAP_WRITE_NO_OVERWRITE` 对这种缓冲区是 D3D11.1 可选设备能力，不能仅凭 FL11.0 或 Platform Update 假定可用；renderer 初始化时查询 `D3D11_FEATURE_D3D11_OPTIONS::MapNoOverwriteOnDynamicBufferSRV`，查询失败按不支持处理。支持时保持现有环形写入/越界 DISCARD；不支持时每批 `WRITE_DISCARD` 并把 `m_bufferHead` 与 VS `globalBufferOffset` 同步置零，普通笔和 Shape 都适用。荧光笔及 Laser 的现有 DISCARD 路径不因该门禁改动。Win7 SP1+仅 KB2670838 的 Hardware FL11.0/WARP 两格仍需真机验证，不得以 Win11 WARP 测试替代。

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
| `scatterColor` | `laserScatterColor`（由所选 Laser RGB 向白色混合） |
| `borderColor` | `laserBorderColor`（RGB 精确等于当前 pass 的 Laser 颜色） |
| `edgeColor` | `laserEdgeColor`（RGB 为同色系外缘高亮，alpha 为 RGB 混合强度） |
| `glowColor` | `laserGlowColor`（RGB 跟随所选 Laser 颜色，alpha 为实体边界处的漫反射峰值） |
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
| `t8` | 留空（B2 第四轮起粒子改用专用 `laserParticleVS.hlsl` 的 `t0`，见下文粒子一节） |
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
- `21`：瞬态笔形 Cursor，PS 调用 `GetPenIconDist`。
- `22`：瞬态箭头 Cursor，PS 调用 `GetArrowDist`。

主产品锚点：`Draw3/Assets/inkPixelShader.hlsl` 的 `type == 21/22` 分支；`0..22` 已占用，不得将白板纹理另编码为 21/22。

`globalOperatorKind`：

- `0` Draw：输出 premultiplied Add 和 `1-alpha` Retain。
- `1` Erase：Add 为零，Retain 为 `1-coverage`。

这些数值虽然部分未暴露在 enum 中，但已经是 CPU/GPU 协议；不要任意复用。

Cursor `InkData[0]` 使用 `pos=center, r=halfWidth, time=halfHeight`；`InkData[1].pos` 使用 `x=outlineWidth, y=fillAlpha`，`r=outlineOpacity+1`，`time=outerOutlineWidth`（仅高亮 Pen/Dot 为 1 物理像素，其它光标为 0）。VS 将后两者映射为 `uv.x/uv.y`；PS 依次合成外黑边、灰色/白色环和彩色填充，外黑边与高亮白环不透明。`globalColor` 保存 fill RGB 与整体 opacity，`globalPadding` 保存 outline RGB。Cursor shape 不写 operator texture，而是对 backbuffer 使用 `operatorResolveBlendState` 直接计算 `premultiplied Add + Retain * Destination`。

## Scenario: Whiteboard Pattern Under Pass (Draw3 后备归属)

### 1. Scope / Trigger

主产品白板点阵/网格的 Draw3 后备路径适用；默认归属仍是 Freeze（D2D/WARP，不占用画布 GPU）。修改参数、under 混合、dirty 裁剪或两个合成调用点时应用本节。默认归属及 Freeze 协议见 [whiteboard.md](../native-desktop/whiteboard.md#场景白板点阵网格背景的归属与视口跟随2026-11-06)。

### 2. Signatures / Anchors

- `Draw3.WhiteboardModel.h`（`Inkeys::Drawing::Draw3`）：`WhiteboardPatternOwner::{Freeze, Draw3}`、`kWhiteboardPatternOwner`、`WhiteboardPatternDrawnByDraw3`。
- `Draw3.WhiteboardPattern.h`（同命名空间）：`WhiteboardPatternParams`、`ResolveWhiteboardPatternForFrame`、`WhiteboardPatternPhase`、`EnumerateWhiteboardPatternPrimitives`、`WhiteboardPatternKey`、`MakeWhiteboardPatternKey`。
- `Draw3.RendererPattern.cpp`：`InkRenderer::EnsureWhiteboardPatternPipeline`、`InkRenderer::DrawWhiteboardPatternUnder(const WhiteboardPatternParams&, RECT dirty) noexcept -> bool`、`RunWhiteboardPatternValidation`。
- `Draw3.DrawingController.cpp`：`syncWhiteboardPattern`、`DrawingController::DrawWhiteboardPatternUnder`、`CompositeLayersToBackBuffer` 与 `Run` 的可信快照平移合成分支；PS：`Assets/inkPixelShader.hlsl` 的 shape 16 分支。

### 3. Contracts

- 门控为白板工作区、非 `WhiteboardPattern::Blank` 且 owner 为 `WhiteboardPatternOwner::Draw3`。控制器 `syncWhiteboardPattern` 单点应用归属门控；renderer 只消费参数。默认 Freeze 时参数未启用、appearance key 为空、under pass 惰性；显式探针仍可直接传启用参数验证后备管线。
- 两个合成位置都先恢复 dirty 范围的内容基底，再 `ApplyOperatorLayers` → `DrawDebugPlateUnder` → `DrawWhiteboardPatternUnder`：普通路径在 `CompositeLayersToBackBuffer`，平移路径在可信快照重投影之后。图案在墨迹、快照与调试板之下，擦除只显露图案。
- 预乘 under：RGB/Alpha 都用 `SrcBlend=INV_DEST_ALPHA, DestBlend=ONE, BlendOp=ADD`，即 `out = base + (1-base.a)*pattern`。它**不幂等**：不恢复基底就重画会让 0.18 的 alpha 约变成 0.33。必须 scissor 到本帧 dirty 与画布的交集；仅限制 CPU 图元枚举不够，边缘图元仍会越界。缺少 scissor raster 或建管线失败返回 false；控制器仅记录一次失败并呈现无图案的墨迹帧。
- 复用 `ShapePrimitiveKind::SolidLine`（shape 16）、现有 `t0` 两槽布局，无新增 HLSL shape/资源：点为零长度线、半线宽=半径 1 DIP；网格为贯穿整幅 canvas 的长线、线宽 1 DIP（半线宽 0.5 DIP），端帽外延至画布之外。同序号网格线端点不依赖 dirty，保证分块与整帧插值一致；枚举保留 AA 边距，结束恢复普通 raster state。
- 视觉常量来自 `Draw3.WhiteboardPattern.h`：间距 24 DIP、alpha 0.18；`IsLightWhiteboardBackground` 按 `299R+587G+114B > 128000` 选黑，否则白。CPU 传未预乘的 shade/alpha；PS 以 `outAlpha=color.a*coverage` 输出 `(color.rgb*outAlpha, outAlpha)`，不得在 CPU 再预乘一次。
- `stepPx=24*dpiScale`；`phaseX=fmod(W/2-viewport.x, stepPx)`、Y 同理，负余数补 step，舍入到 step 时归零。必须用 **double 精度的 fmod**；格点按 `phase+k*step` 直算，不逐点累加。视口是后备缓冲像素，屏幕=画布−视口；有效 DPI 为 [0.5,8]，Blank/非法主题、尺寸或 DPI 返回未启用参数。
- 仅写 backbuffer：不进入 L2/L1/L0、history、trusted snapshots、hot preimages 或 composition cache，不从带纹理的 backbuffer 回灌内容。白板空页仍绘制图案；桌面/PPT 保持真透明。
- appearance key 为 `WhiteboardPatternKey{kind, shade, stepPx}`；`syncWhiteboardPattern` 与 `appliedWhiteboardPatternKey` 比较，变化设置 `forceFullPresent=true`，包括关闭纹理/离开白板以清旧像素。同明暗类 RGB 变化不改图案键，纯色由 Freeze 更新；phase 不入键，因为视口平移已有整屏重合成。命令处理后、idle 前及合成前均同步，不能只在已有墨迹 dirty 时检查。
- D2D 对照须区分已知结果与断言：任务提供的已知零视口差异为点阵 ≤10 LSB、200% 网格 ≤7 LSB；当前 `RunWhiteboardPatternValidation` 的 `phaseTolerance` 为点阵 10、100%/150% 网格 2、200% 网格 **8**，小数位移对照为 12。PS 的 `1.5*fwidth(d)` smoothstep 边缘与 D2D 面积覆盖不同，不能宣称仅有 ≤1 LSB 的量化差。后备零视口对照忽略 8 px 边缘（旧 D2D 循环遗漏部分边界图元），上述值不是任意相位的全屏承诺。

### 4. Validation & Error Matrix

下列命名断言锚定 `RunWhiteboardPatternValidation`（`--draw3-whiteboard-pattern-test`），不宣称本轮执行通过。

| 场景 | 断言 / 探针 |
| --- | --- |
| 默认 Freeze / 两种归属 | `owner switch is a single exclusive gate`；默认下后备像素用例仍可执行 |
| 桌面/PPT、Blank、非法 DPI | `desktop/presentation gating leaves the canvas truly transparent`、`blank pattern never draws`、`invalid DPI disables the pattern` |
| 深浅背景、多 DPI、零视口 | `dots match the legacy Freeze pattern within the AA-tail tolerance`、`wide grid matches the legacy Freeze pattern within the AA-tail tolerance`；按上述实际阈值核验 |
| 重复整帧、分块、重叠 dirty | `repeated frames never accumulate alpha (byte-identical)`、`tiled dirty rectangles equal one full frame byte-for-byte`、`overlapping partial recomposites stay byte-identical` |
| 不恢复基底重复 under（反证） | `a second under pass without recopy would accumulate alpha` 必须检测到增长 |
| 橡皮与透明/半透明/不透明墨迹 | `pattern is exactly under the ink: out = base + (1-a)*pattern`、`opaque ink pixels are never altered by the pattern` |
| L2、可信快照、平移分支 | `L2 contains no pattern pixels`、`trusted L2 snapshot contains no pattern pixels`、`pan branch: the pattern sits under both the L2 and the trusted snapshot` |
| 周期、小数、正负大视口 | `viewport shifted by whole periods leaves the pattern unchanged`、`1048576 px viewport follows the exact fmod phase`、`shifted pattern matches the anchor-shifted D2D reference` |
| 管线/scissor 失败、4K 成本 | 前者按代码返回 false 并降级装饰；4K 只记录成本，不代替真实 DComp/ULW 或 Win7 验证 |

### 5. Good / Base / Bad Cases

- Good：恢复 dirty 基底后，仅在 dirty scissor 内画一次 under；分块与整帧逐字节相同，换样式即整屏刷新。
- Base：默认 Freeze 归属时 Draw3 图案为空操作；主题预览仍用 `WhiteboardVisual::DrawThemePattern`。
- Bad：直接对旧 backbuffer 再叠 under；网格端点随 dirty 截短；把纹理烘进 L2 或快照；复用已占用的 shape 21/22。

### 6. Tests Required

- `InkeysHeadlessTests/draw3_canvas_pan_tests.cpp::TestWhiteboardPatternGeometry`：相位周期/负数/大坐标、参数门控、外观键、枚举数量与 dirty 无关的网格端点。
- `RunWhiteboardPatternValidation`：上述完整像素矩阵及反证必须保留；两种 owner 都保留可编译后备代码，不能因默认惰性删掉验证。
- history/hot preimages/composition cache 隔离还须静态核对调用和数据源，不能把 L2/可信快照读回断言扩大成全部缓存的运行覆盖。真实显示、调度、耗时与诊断边界见 whiteboard.md 的“未验证项”。

### 7. Wrong vs Correct / Common Mistakes

Wrong：每帧对旧 backbuffer 直接 `DrawWhiteboardPatternUnder(full)`；仅靠图元枚举范围限制写入。

Correct：恢复 dirty 基底 → `ApplyOperatorLayers` → `DrawDebugPlateUnder` → 图案 under（dirty∩canvas scissor）→ 后续瞬态视觉/Present。

Common Mistakes：将 under 当作幂等；把 1 DIP 网格宽度误作半宽；CPU/PS 双重预乘；漏掉可信快照平移分支或空页；把测得 ≤7 LSB 误写成现有测试已强制 ≤7。

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

普通圆笔的亚像素 AA 另见下方“Thin Round Ink Coverage”；不得用 SDF 导数下限或最小几何半径代替有限宽度覆盖。

- VS 为圆胶囊和荧光笔固定矩形 sweep 生成覆盖形状的 quad/AABB。
- PS 使用 signed distance 与 `fwidth`/`smoothstep` 做抗锯齿；高亮 sweep 的零等值线由 X/Y/线段法线半平面交集确定。
- CPU dirty bounds 必须至少覆盖 VS 生成范围；当前普遍预留 2px 几何扩展和 3px bounds padding。
- Shape `16/17` 的 VS 生成扩宽线段 OBB，PS 分别计算整段 capsule 或 analytic dashed capsules；零长度必须退化为半线宽圆点。Shape `18/19` 的 VS 先对任意方向端点取 `min/max`，PS 使用 rounded-box SDF；圆角把 `globalPadding.x` 钳制到短边一半，Outline 使用 `abs(boxDistance) - halfWidth`，Filled 直接使用 box distance。
- Shape dirty/history bounds 必须与上述几何同步：Line/Outline 扩展半线宽和 AA，Filled 只扩展 AA；Outline history 遍历四边而非整个内部，Filled 覆盖完整规范化矩形。
- Laser shape `7` 的 `InkPoint.r` 是彩色实体外半径。96 DPI 基准实体半径为 2.5px，白芯半径是实体半径的 `1/3`，漫反射在实体轮廓外固定扩展 5px；压力只改变实体/白芯/散射比例，不改变 5px 漫反射。PS 必须复用实体 signed distance，以平方曲线令 coverage 在实体边界为 1、5px 外缘为 0；同色系高光只混合 diffuse RGB，禁止通过额外 source-over 层抬高渐变 alpha。VS、PS、Hover/Touch `LaserDot.radius`、粒子外壳锚点和 CPU bounds 必须复用 `renderer.cppm` 的尺寸契约。
- 普通笔零长度或一端圆包含另一端时退化为较大端点圆；高亮零长度退化为固定竖直矩形。
- `InkPoint` 中出现 NaN 时 PS discard；CPU 仍应避免生成非有限输入。

## Scenario: Thin Round Ink Coverage

### 1. Scope / Trigger

仅适用于 `globalShapeType == 0` 的绘制 operator。调整薄线 AA 时必须同步两侧 PS 并进行实际着色器离屏回读；橡皮、Laser、解析形状和 cursor 保持各自覆盖规则。

### 2. Signatures

`InkPoint`/`PS_INPUT`/`GlobalShaderConstants` 布局不变。薄线计算输入为胶囊 SDF 距离 `d` 与最近生成圆半径 `r`，输出标量 coverage，继续进入既有预乘 Add/Retain。

### 3. Contracts

- 对 abs 距离使用 `fwidth(d)` 会在像素 quad 横跨细线中心时相消，不能用它作为薄线唯一像素尺度。
- `C(s)=1-smoothstep(-0.75,0.75,s)`；薄线 coverage=`saturate(C(d)-C(d+2*r))`。局部半径<=0.5px 使用该值，0.5–1px 平滑过渡，>=1px 保持原 AA。
- 薄线距离/最近生成圆使用 PS 的 `SV_Position.xy` 真实像素中心；OBB 的 `pixPos` 插值会受栅格顶点量化影响，不能让几何分段改变薄线取样位置。旧 AA 的坐标/导数规则保持不变。
- 半径由 uneven capsule 的实际最近生成圆确定，包含圆/零长先处理；侧边插值不得用不匹配的简单纵向投影代替，也不能在退化法线处除零。
- 几何半径不变；零宽透明。Add/MAX、Retain/MIN 与预乘输出不变，禁止叠加混合掩盖接缝。保留 VS 2px、CPU 3px padding。
- 恒宽长线连续积分覆盖宽度随2r变化；BGRA8量化、采样相位与圆帽距离滤波近似必须在报告中区分，不声称任意短胶囊的严格二维面积积分。

### 4. Validation & Error Matrix

| 输入 | 预期 |
|---|---|
| 零半径绘制 | coverage=0，不残留最低宽度灰线 |
| 浅斜细线跨 quad 中心 | 不因 SDF 导数归零而形成异常透明断口 |
| r=0.5/1px 附近 | 两种 AA 平滑交接，不出现宽度台阶 |
| 零长或端点圆包含 | 使用较大生成圆，避免除零/NaN |
| 同一几何重复绘制 | MAX/MIN 幂等，不重复加深 |
| 非绘制 type0 或其他 shape | 保持原覆盖和 operator 行为 |

### 5. Good / Base / Bad Cases

- Good：0.1px浅斜线以亚像素覆盖连续呈现，确认点半径和文档宽度不变。
- Base：常规粗笔保持原 AA，端帽按同一距离滤波近似。
- Bad：把半径钳到0.5px、只给fwidth加下限，或把MAX换成加法。

### 6. Tests Required

实际D3D绘制并回读BGRA8 Add/R16 Retain：直径0/.05/.1/.25/.5/1/2/4px、水平/浅斜/45/90度、XY 1/8px相位；检查主轴截面、有效积分宽度、分段/整段、重复、L0/L1拆分、变半径、圆帽、包含圆和零长。检查红色黑底/黑色白底预乘合成，误差预算应包含1/255量化而非随失败任意放宽。

半径<=0.5px 的等宽共线分段保持严格量化误差对照。过渡/粗线不能把旧导数 AA 自带的图元分段差异误判为新回归：用未改动的 Erase coverage 实测各图元旧 AA，再结合有限宽度计算与指定 blend 权重逐像素对照。L0/L1 合成必须与同一分段几何一次绘制一致；这不等同于断言旧 AA 对任意几何细分完全不变。

### 7. Wrong vs Correct

Wrong：`alpha = smoothstep(... max(fwidth(d), epsilon) ...)` 是薄线唯一覆盖；或 `r=max(r,0.5)`。

Correct：`thin=C(d)-C(d+2*r)`，以固定屏幕像素滤波同时计入两侧边缘，再在0.5–1px半径范围与既有AA平滑交接。

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

- Good：两支不同颜色的交叉笔分别 MAX 后，以各自 Down 锁定的 `LaserStyleConstants` 按 Down 顺序 resolve，上层彩色实体可覆盖下层白芯。
- Base：单支笔自交仍只解析一次 coverage，不在交叉处重复加深。
- Bad：先把所有 contact MAX 到同一 coverage；这会丢失笔身份，无法实现有序覆盖。

### 6. Tests Required

- 断言 96 DPI 的实体直径 5px、白芯直径约 1.67px、漫反射每侧 5px，且 DPI/压力换算符合固定漫反射契约。
- 静态核对 `LaserDiffuseCoverage(0)=1`、`LaserDiffuseCoverage(diffuseExtent)=0` 且区间内单调；`ResolveLaserMaterial` 在彩色实体外只能由单一 diffuse 层决定 alpha，同色系高光不得再次 source-over 抬高透明度。
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
## Scenario: D3D11 Laser Particle Pools and Clear Spawn（2026-11-24 阶段 C1.3 起）

### 1. Scope / Trigger

修改 `Inkeys.Drawing.Draw3.laser_particles`（`Draw3.LaserParticles*`、`Draw3.LaserParticleSystem.cpp`）、`laserParticleCommon.hlsli` / `laserParticleVS.hlsl` / `laserParticlePS.hlsl` / `laserParticleSpawnCS.hlsl`、粒子常量缓冲区、档位表、粒子脏区策略或清空飘散流程时，必须同步本节。

历史：B2 第四轮（2026-11-23）删除了每帧模拟用的 UpdateCS/EmitCS，改为“出生写一次、VS 按出生参数 + 时钟求值”；C1.3 重新引入一个**只在清空时运行一次**的生成计算着色器，并把单池拆为两池、容量改为按档位。

### 2. Signatures

- `LaserGpuParticle`：`64 bytes`（位置、motion、motionParameters、shapeParameters、RGBA8 直通色、寿命、释放时刻、`kindAndBatch`）。
- `LaserDriftParticle { LaserGpuParticle particle; uint32_t group; }`：CPU 直写记录，`68 bytes`；`particle.releaseMilliseconds` 存相对延迟，上传时加出生时刻并写批号。
- 档位：`LaserParticleLimit { Low, Medium, High, Extreme }`，`kLaserParticleLimitTiers = { {32768,16384}, {131072,65536}, {524288,262144}, {1048576,524288} }`（飘散池槽数, 单次上限）。行进池固定 `kLaserTrailPoolCapacity = 8192`。批次表固定 `kLaserParticleMaximumBatches = 8192`，暂存块 `kLaserParticleStagingChunk = 16384`。
- `LaserParticleSystem::Step(now, freq, trailSeeds, plainDrift, dissolveDrift, dipPerPixel, dissolveControl, const LaserClearSpawnRequest*) -> LaserParticleStepResult`
- `SetLimit / EffectiveLimit / MaximumSeeds / PrepareDriftPool / ReleaseIdleDriftPool / ReleaseIdleResources / PollClearSpawn / GpuClearSpawnAvailable`
- `LaserParticleActiveRing::Initialize(capacity) / Append -> LaserParticleBatchId / TrimTail(id, count)`
- `CanvasRuntimeHistory::VisibleContentBounds(InkPixelBounds region)`：给定页面区域内可见项范围（逐项求交后合并），C2 页面级空间索引将接管其实现。

### 3. Contracts

- **两池**：行进池（8192 槽，只放 Trail）与飘散池（档位决定槽数，放清空飘散普通批与激光飘散批）。共用 VS/PS、常量缓冲区、时钟与 1 MB 暂存块。`Draw(OverBody)` 只画行进池，`Draw(UnderBody)` 只画飘散池，各自只遍历自己的活跃区间。
- **显存上限只由档位决定**：飘散池 = 槽数 × 64 B（极致 64 MiB），另有行进池 512 KiB；任何事件叠加都不增长。单次上限 = 飘散池一半，一批满额清空 + 一批满额激光飘散可同时完整存在，第三个满额批起按先进先出淘汰最旧批。
- **容量与寻址**：容量必须是 2 的幂；VS 下标为 `(activeStart + instanceId) & capacityMask`，`capacityMask` 由 `LaserParticleDrawConstants` 偏移 52 下发（结构 64 字节）。槽位用池掩码，批次下标按批次上限取模。
- **懒创建与闲时归还**：池与着色器在第一次需要时创建；飘散池在环为空时每帧单独归还（含生成着色器、UAV、counters）；整个系统在无存活粒子、无待上传记录且未处于“选中激光笔并开着激光粒子”的预热状态时归还。`Prepare/WarmUp` 只建行进池预热共享着色器。
- **档位**：配置 `Drawing.Particles.Limit`（0–3，默认 1），经 `LaserParticleOptions` bit6–7 下发；控制器不经激光生命周期锁存，每帧帧首以及清空/激光飘散建池前读取原始选项并 `SetLimit`（同档为空操作，设备重建后由此恢复）。生效档变化时清空并归还飘散池、移除飘散脏区（加入本帧脏区）、丢弃按旧档位排队的飘散记录与 GPU 请求、激光飘散改走淡出退路；本帧已有入队飘散批时，换档推迟到下一帧帧首。
- **逐级降档**：飘散池缓冲区/SRV 创建失败时依次尝试更低档，成功档记为本设备会话上限并写一条 Warn 日志；低档也失败则飘散池不可用（行进池不受影响）。生成前先 `PrepareDriftPool()` 取实际单次上限，返回 0 时走无粒子退路。
- **CPU 直写**：激光飘散与 CPU 清空退路用 `MakeDriftParticle` 直接生成记录与批次包络，与旧“种子 + 打包”逐字段一致（`Reference*` 函数仅供自检对照）。
- **清空 GPU 生成**（`laserParticleSpawnCS.hlsl`，`cs_5_0`，8×8 线程组，单入口按 `mode` 分三遍：0 清槽、1 计数、2 生成）：
  - 输入为清空快照 `t0`（预乘 BGRA，`Load`），输出 `u0` 飘散池、`u1` 16 字节 counters（inked、written）。
  - 每物理像素一线程：α ≥ 48 计入墨迹；`wanted = min(maximumSeeds, inked × 0.5 / dpiScale²)`，阈值 `uint(min(1, wanted/inked) × 2^24)`，随机数 `Hash((x ^ y*0x9E3779B9) + seedBase)`；其余字段公式与 `MakeDriftParticle` 相同（公共函数在 `laserParticleCommon.hlsli`）；释放时刻按出生像素的碎裂遮罩。100% 缩放与 CPU 原算法逐粒一致，200% 缩放统计一致（CPU 按 2×2 代表像素抽样）。
  - 请求在清页成功后入队，在本帧 `Step` 中执行：环中追加 `reservedCount` 普通批（`slotGroups` 整段写 `kCanvasClearParticleGroup`）、派发前解除 VS SRV、三遍派发后解除 CS 的 UAV/SRV/常量缓冲区、counters 复制到 staging；`PollClearSpawn` 用 `DO_NOT_WAIT` 异步读回，只有设备代次与批次身份仍有效且仍是尾批时才 `TrimTail(min(written, reserved))`；written 为 0 时整批撤销并同步移除该批脏区。
  - 生成资源任一创建失败：本设备会话改走 CPU 退路（读回 + 抽样 + 直写）。
- **清空事务与范围**：先用历史查询得到屏幕范围（逐项与视口求交，四周放宽 2 px；栅格待恢复、视口待刷新或恢复、L2 未完整呈现、图形焦点编辑时用整张画布）；范围为空不建池、不捕获快照、不出粒子；范围非空才建池、捕获快照；碎裂范围 = 历史范围（CPU 退路同）。批次脏区 = 范围扩大最坏行程常量上界（`ConservativeCanvasClearParticleBounds`）。
- **脏区种类**：`LaserParticleDirtyTracker` 按 `Other`（行进）、`ClearDrift`、`LaserDissolve` 分开登记；只为 `Step` 实际接纳的批次登记。
- **内存**：待上传记录数组超过 16384 条时上传后归还容量；种子/记录生成以 `std::bad_alloc` 兜底，失败只丢本次粒子。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| feature level < 11_0 或字节码缺失 | `Initialize` 返回 false，粒子不可用，激光其余功能照常 |
| 共享着色器/常量缓冲区创建失败 | `Release()`，本设备会话粒子不可用 |
| 飘散池建池失败 | 逐级降档；全部失败则仅飘散不可用，激光飘散走淡出退路，清空只碎裂 |
| 行进池建池失败 | 仅行进粒子丢弃，飘散照常 |
| 生成着色器/UAV/counters 失败 | 记一次日志，清空走 CPU 退路 |
| 回读未就绪 | 保持预留长度与保守脏区，不阻塞 |
| 回读时批次已淘汰、非尾批、设备重建、换档、Reset、空闲归还 | 丢弃结果，不改环 |
| 快照捕获失败 | CPU 退路，粒子释放延迟清零 |
| 清页失败 | 释放快照，不入队 |

### 5. Good / Base / Bad Cases

- Good：极致档写满半屏后清空，清空当帧没有整屏读回，CPU 开销与粒子数无关；粒子与碎裂同帧开始。
- Base：未启用清空飘散或范围为空时不建池、不捕获快照。
- Bad：生成前不确定实际档位就按请求档生成；派发时 VS 仍绑定粒子 SRV；回读结果不校验批次身份就裁剪；把同一 D3D 上下文共享给测试夹具里另一个 `InkRenderer`（`Init` 会改视口）。

### 6. Tests Required

- `--draw3-laser-resources-test`（`RunLaserResourceLifecycleValidation`）：四档容量与 64 MiB 实际创建、拆池分流与分绘、满额双批、降档注入（飘散/行进分别）、`TrimTail` 代次、分块跨环上传、直写与参考实现逐字段对照、GPU/CPU 1x 逐粒（整数字段完全一致；浮点 burst.x/y、birthWind.x/y 相对 ≤1.7e-3 且绝对 ≤4e-5，其余 ≤1e-4）、2x 统计、触顶、零粒、回读作废、清槽、生成后绘制、生成资源失败注入。
- `--draw3-laser-raster-failure-test`（`RunLaserRasterFailureProductionProbe`）：激光直写对照、接纳退路、墨迹范围覆盖 L2（笔、荧光笔、图形、单点、擦除、平移、视口两侧与内外混合）、清空事务、零粒脏区撤销、范围为空不建池、切档与帧中切档（独立 WARP 设备）、待上传数组容量回收。
- `--config-boundary-test`：Limit 默认、四档往返、非法值回落。`--draw3-parked-desktop-exit-test`：四档经 Host 生产路径往返且不影响效果位。
- 每次修改上述合同后做变异验证（改坏一处确认用例失败）。

### 7. Wrong vs Correct

#### Wrong

```cpp
const uint32_t maximum = kLaserParticleLimitTiers[requested].maximumSeeds; // 首次建池可能降档
AppendLaserDissolveSeeds(pending.dissolveDrift, envelope, dpp, strokes, front, maximum, wind);
```

#### Correct

```cpp
const uint32_t maximum = renderer.PrepareLaserDriftPool(); // 先建池，取实际生效档的单次上限
if (maximum == 0) fallbackLaserDissolve(now);               // 飘散池不可用：淡出退路
else AppendLaserDissolveSeeds(pending.dissolveDrift, envelope, dpp, strokes, front, maximum, wind);
```

```

## 合成矩形独立上传（2026-10-15）

- ApplyOperatorLayers 使用专用256B DYNAMIC structured buffer/SRV，stride16，WRITE_DISCARD仅写两个InkPoint共32B，合成时绑定原t0，offset0；不更改shader或混合公式。
- 合成不得更新inkDataBuffer或重置m_bufferHead。所有成功的动态ink上传（含Laser/Cursor DISCARD）统一维护已用范围末端；DrawStroke/Shape必须先保存起始offset再上传，避免把更新后的head作为VS起始offset或重复推进。
- DEFAULT UpdateSubresource 返回void，更新后通过设备状态检查识别失败；不将其伪装成Map结果，设备失效沿用现有设备/呈现失败链。
- RunRendererUploadValidation 是明确无HWND/swapchain的产品上传/合成离屏读回验证；三种动态DISCARD/动态追加/DEFAULT结果、非同步update/Draw、工具交错与跨容量批次都应核验。它不能代替实机RTS/Present视觉验证。
