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
| `radii` | `laserRadii`（基准白芯半径、基准实体外半径、固定漫反射宽度、基准散射半宽） |
| `coreColor` | `laserCoreColor` |
| `scatterColor` | `laserScatterColor` |
| `borderColor` | `laserBorderColor` |
| `edgeColor` | `laserEdgeColor`（RGB 为红粉外缘高亮，alpha 为 RGB 混合强度） |
| `glowColor` | `laserGlowColor`（alpha 为实体边界处的漫反射峰值） |
| `parameters.x/y/z/w` | `laserParameters.x/y/z/w`（组 opacity、DPI scale、外缘 glow 下/上阈值） |

新增字段必须保持 16 字节对齐，并同步 `renderer.cppm/.cpp`、`ink.hlsli` 与 shape `7/8/9/10/11/12` 的绑定。

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
| `s0` | OperatorSampler |

`ApplyOperatorLayers` 绑定 PS `t1..t5` 时为 VS `t3` 留空槽。修改数组顺序前必须按寄存器表核对。

Laser coverage 写入只绑定 `t7` RTV，并以 `D3D11_BLEND_OP_MAX` 累积单支笔的四通道 `(core, scatter, solid shell, diffuse)`。shape `8` 把 `t7` 解析为材质，按 Down 顺序 source-over 到 backbuffer 或 `t6`；shape `11` 再把 `t6` 以整组 opacity 叠到 backbuffer。每次 resolve 后必须解除 `t6/t7` SRV，随后才能把对应纹理切回 RTV。

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
- `12`：关闭混合后，以矩形覆盖写零局部清理 `t7`。

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
- Laser shape `7` 的 `InkPoint.r` 是红色实体外半径。96 DPI 基准实体半径为 2.5px，白芯半径是实体半径的 `1/3`，漫反射在实体轮廓外固定扩展 5px；压力只改变实体/白芯/散射比例，不改变 5px 漫反射。PS 必须复用实体 signed distance，以平方曲线令 coverage 在实体边界为 1、5px 外缘为 0；红粉高光只混合 diffuse RGB，禁止通过额外 source-over 层抬高渐变 alpha。VS、PS、Hover/Touch `LaserDot.radius`、粒子红边锚点和 CPU bounds 必须复用 `renderer.cppm` 的尺寸契约。
- 普通笔零长度或一端圆包含另一端时退化为较大端点圆；高亮零长度退化为固定竖直矩形。
- `InkPoint` 中出现 NaN 时 PS discard；CPU 仍应避免生成非有限输入。

## Scenario: Ordered Multi-Laser Composition

### 1. Scope / Trigger

- 修改 Laser 尺寸、coverage 通道、资源寄存器、shape 编号、批次生命周期或多 contact 层级时，必须按本节同步 CPU/HLSL。

### 2. Signatures

- `DrawLaserCoverage(points)`：把一支笔写入当前 `t7` RTV。
- `ClearLaserCoverageRect(rect)`：shape `12` 局部覆盖清零 `t7`。
- `ResolveLaserStrokeCoverage(dst, rect, opacity)`：shape `8` 将 `t7` source-over 到 `dst`。
- `ResolveLaserCompositedColor(dst, rect, opacity)`：shape `11` 将 `t6` source-over 到 `dst`。

### 3. Contracts

- 同一支笔的所有真实点和 prediction 先在 `t7` 以 MAX 取 coverage 并集；不同笔禁止在同一次 MAX 中合并。
- 未烘干层按 Down 顺序逐支解析，后 Down 的整支笔永远位于上层；较早结束的 contact 必须保留最终 CPU 几何，直到同批最后一个 contact 抬起。
- 最后 Up 后按同一顺序烘入 `t6`；resize 只复制 `t6` 交集，未烘干层从 CPU 几何重建。Laser 永不写入 L2。

### 4. Validation & Error Matrix

- 空点集或空矩形 → 跳过绘制，不改变目标。
- SRV/RTV 缺失或常量映射失败 → 当前 pass 返回，不绑定冲突资源。
- Cancelled contact → 标记旧 bounds 为脏，不解析或烘干该层。
- Fade 完成/clear → 同步清空 `t6`、`t7`、未烘干层和全部新旧 bounds。

### 5. Good/Base/Bad Cases

- Good：两支交叉笔分别 MAX 后按 Down 顺序 resolve，上层红色实体可覆盖下层白芯。
- Base：单支笔自交仍只解析一次 coverage，不在交叉处重复加深。
- Bad：先把所有 contact MAX 到同一 coverage；这会丢失笔身份，无法实现有序覆盖。

### 6. Tests Required

- 断言 96 DPI 的实体直径 5px、白芯直径约 1.67px、漫反射每侧 5px，且 DPI/压力换算符合固定漫反射契约。
- 静态核对 `LaserDiffuseCoverage(0)=1`、`LaserDiffuseCoverage(diffuseExtent)=0` 且区间内单调；`ResolveLaserMaterial` 在红色实体外只能由单一 diffuse 层决定 alpha，红粉高光不得再次 source-over 抬高透明度。
- 断言 `LaserDot.radius`、固定宽度轨迹半径和 Touch/Hover 尺寸一致，`LaserStyleConstants == 112 bytes`。
- 断言 CPU dirty bounds 覆盖实体半径、固定漫反射和 AA padding；人工验证反向 Down 顺序、较老笔继续书写、取消、resize、Hold/Fade 与静态 Hold 零 Present。

### 7. Wrong vs Correct

#### Wrong

```cpp
// 不同 contact 直接丢进同一张 MAX coverage，层级信息永久丢失。
for (const auto& contact : contacts) DrawLaserCoverage(contact.points);
ResolveLaserStrokeCoverage(backBuffer, dirty);
```

#### Correct

```cpp
for (const auto& layer : layersInDownOrder) {
    ClearLaserCoverageRect(layer.bounds);
    DrawLaserCoverage(layer.points);
    ResolveLaserStrokeCoverage(backBuffer, layer.bounds);
}
```

## Scenario: D3D11 Laser Particle Compute Pipeline

### 1. Scope / Trigger

修改 `draw3.laser_particles`、粒子镜像布局、CS 常量、资源寄存器、shape `10` 或粒子 dirty 策略时，必须同步本节。

### 2. Signatures

- `LaserGpuParticle`，固定 `128 bytes`
- `LaserParticleEmissionRequest { positionX, positionY, tangentX, tangentY, entityRadius, count, seedBase }`
- `LaserParticleSystem::Initialize/Configure/Simulate/Emit/Reset`
- `InkRenderer::DrawLaserParticles()` 固定调用 `DrawInstanced(6, 2048, 0, 0)`

### 3. Contracts

- 容量是编译期契约：粒子固定 `2048` 槽；粒子缓冲、SRV/UAV、两个 CS 和两个常量缓冲只在 renderer Init/Release 创建销毁，Resize 不重建。
- CS 资源表：

| Stage | Register | Resource |
|---|---|---|
| UpdateCS/EmitCS | `u0` | `RWStructuredBuffer<LaserGpuParticle>` |
| VS shape 10 | `t8` | 同一粒子缓冲的 SRV |

- Update 常量为 `32 bytes`：`float4(wallDeltaSeconds, motionDeltaSeconds, shrinkStartTravelRatio, endRadiusScale)`、`uint2(resetAll, particleCapacity)` 和 `float2` padding。Emit 常量为 `96 bytes`：`uint4 + 5*float4`，依次承载循环槽/数量/容量/seed、出生位置/切线、实体半径/核心比例/寿命范围、速度/偏转/半径范围、亮度/呼吸振幅及呼吸频率/渐入。CPU 结构必须 `static_assert(size % 16 == 0)` 并与 HLSL 字段顺序一致。
- `LaserGpuParticle` 只保存屏幕位置、固定速度、年龄/寿命、累计/最大行程、出生/当前半径、Alpha、基础/当前亮度、呼吸参数、seed/alive 和 padding；CPU/HLSL 字段偏移必须一致并保持 `128 bytes`。
- 每次 Compute 前先 `VSSetShaderResources(8, null)`；Dispatch 后解除 `u0`、`b0` 和 CS。绘制结束再解除 VS `t8`。禁止依赖 D3D11 自动冲突修复，也禁止重新引入 CS `t0/t1` 路径 SRV。
- EmitCS 使用 CPU 管理的循环槽覆盖最旧粒子，不使用 Append/Consume、计数器、间接绘制或回读。每粒以 50/50 概率选择正/负法线，在所选法线附近均匀偏转 `±25°`，并独立均匀采样 `28–64 DIP/s` 初速度和 `0.7–1.0s` 寿命；画笔前向速度不得进入粒子速度。
- Emit 请求的屏幕锚点直接使用本帧 `l0DrawPoints.back()`，切线来自最后一个非退化 L0 段；重复点沿用上一有效切线，首次真实移动前没有有效方向时不发射。prediction 可以改变新粒子的锚点/切线，但不能参与发射密度或存量状态。
- UpdateCS 用实际 wall time 累计年龄，用最多 `1/30s` 的 motion dt 推进；固定速度和 Alpha 都乘 `1-smoothstep(0,1,age/lifetime)`。粒子按实际累计行程计算尺寸，前 10% 保持出生半径，之后 smoothstep 到 20%；达到自身寿命立即死亡。
- 粒子出生后 UpdateCS 不再读取路径、画笔、prediction 或 Up 状态；Up/Cancel 只停止 CPU 新请求。禁止 path slot/generation、弧长、segment cursor、端点阻塞淡出、路径追赶和 prediction correction。
- 每粒写入 `0.68–1.0` 基础亮度、`0.8–1.4Hz` 呼吸频率、随机相位和 `0.12` 振幅；VS 通过 `SV_InstanceID` 取槽，死亡槽生成屏幕外退化图元。呼吸亮度只乘核心/辉光 RGB，不乘 Alpha。
- shape `10` 的核心为中性白，3 DIP 辉光为 `(1.0, 0.32, 0.40)`、峰值 Alpha `0.34`，保留二次距离衰减并继续输出预乘 Alpha operator-resolve。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| feature level < 11_0 | `Initialize` 返回 false，renderer 继续创建 VS/PS 和主体资源 |
| CS/buffer/SRV/UAV 任一创建失败 | 释放已建粒子资源、诊断一次、`IsAvailable == false` |
| 无有效 L0 切线 | 不发射并清空该 contact 的小数余量，不形成 Down 补发 |
| 非有限请求位置/切线或退化切线 | Emit 忽略该请求，不移动循环槽 |
| particle pool wrap | 新发射覆盖循环槽，不分配、不回读 |
| UAV/SRV 切换 | 显式解绑后再切换 stage；Debug Layer 不应报告同资源读写冲突 |
| Up/Cancel/prediction 跳变 | 既有粒子的速度、位置和寿命不被控制器修正 |

### 5. Good / Base / Bad Cases

- Good：新粒子从当前 L0 笔尖向两侧法线随机喷射；prediction 尾替换、回缩、急弯和 Up 都不改变既有粒子的屏幕空间轨迹。
- Base：没有活粒子仍固定绘制 2048 实例，死亡槽退化；设备不支持 CS 时仅无粒子。
- Bad：每帧重建粒子 buffer、让存量粒子重新采样 L0、用 `CopyResource/Map` 回读位置，或同时绑定 `u0` 与 `t8`。

### 6. Tests Required

- 静态断言 `LaserGpuParticle == 128 bytes`，并断言 Update `32 bytes`、Emit `96 bytes` 且均 16 字节对齐。
- 单元测试 Down 零爆发、静止 6/s、移动 4 DIP 密度、48/s 与全局 96 预算、无整数积压、双侧法线与 `±25°` 偏转、`28–64 DIP/s` 和 `0.7–1.0s` 采样范围、速度/Alpha 单调性、按行程缩至 20%、呼吸与 Alpha 分离、Up/prediction 无修正、批次 dirty 到期和 resize 后重裁剪。
- 静态搜索确认 UpdateCS/EmitCS 仅声明 `u0/b0`，VS 仅在 `t8` 读取，并且路径结构、generation、弧长、segment cursor、端点淡出和 prediction correction 不在粒子源码中。
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
context->VSSetShaderResources(8, 1, &nullSrv);
context->CSSetUnorderedAccessViews(0, 1, &particleUav, nullptr);
context->Dispatch(32, 1, 1);
context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
```
