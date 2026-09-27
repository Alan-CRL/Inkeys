# Research: UI3 动态光影缓存、分片提交与持续退化路径

- Query: 光影是否存在坏缓存键、反复 Gaussian 创建、状态不收敛、悬空资源，足以解释主栏偶发慢动作和跳变；复核前序“缓存有容量上限，所以不是光影缓存问题”的推断。
- Scope: mixed，当前工作区 `Bar.Rendering.cpp/.cppm`，必要追踪 Bar 绘制入口、布局与分页客户端；外部资料仅查 Microsoft Direct2D API 合同。
- Date: 2026-09-22
- 授权边界: 本轮仅研究，所有新增文件位于本任务 `research/`；未修改产品、spec、测试或工程，未启动 GUI，未编译主程序，未运行 Git 操作。

## Findings

### 结论及证据等级

1. **确定缺陷：关闭绘制属性面板时，普通颜色块描边在同一布局帧先被 `SetTar(1.0)`，又被 `SetTar(compactScale)`。** 这会破坏面板尺寸与描边的等比契约，并使归一后的遮罩描边键变化。主会话已通过真实 Animation module 验证异常数值；这里进一步确认其下游进入 `GetRoundedRectDiffuseMask` 的 `strokeWidthQuarter`。11 个普通色块共用同一几何键，不能说成每帧 11 次 Gaussian。详见后文。
2. **确定的额外绘制量：动态光照到属性面板后，15 个常见控件仅柔光就会提交 135 或 375 次 `FillOpacityMask`。** 该数值已通过原样提取的两个生产函数及 fake context 计数验证。此时它们共用一个父遮罩，属于缓存命中后的绘制成本，不能与 Gaussian cache miss 混为一谈。
3. **确定的加速路径失配：整图 exact mask 拒绝所有非零平移，主栏常态的 capacityOrigin 映射使它不可达；PageControl 也有平移且没有推进 exact 帧序号。** 因此不能用历史整图缓存命中率或历史性能结果说明当前主栏正在用整图缓存。
4. **没有证实本故障现场的唯一瓶颈。** 上述缺陷与“关闭动态光影改善、运行中时快时慢”相容，但无实际故障帧的耗时、present 返回值和资源快照，不能声称已经复现用户报告的严重卡顿。历史大 target、永久失败 latch 都不能单独涵盖新的“自行快慢变化”描述。

### Files found

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` | 光源更新、gradient、A8 父遮罩、exact 整图、Gaussian、Shape/Superellipse 绘制 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm` | 缓存键、COM 所有权、失败 latch 和帧状态 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 属性布局的重复描边目标、动画几何归一化、capacityOrigin 变换及完整场景绘制 |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | 颜色块的共享几何和第三光专属设置 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | 展开面板高 185 DIP、笔型按钮尺寸与位置常量 |
| `Inkeys/Inkeys/UI/Bar/Bar.Layout.cppm` | 展开面板宽 370 DIP |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp` | 共享按钮绘制、按压变换、PageControl scene 平移 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` | PageControl target 创建与 BeginDraw/scene.Render 流程 |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp` | 独立橡皮面板按 panel/menu pose 归一化遮罩键 |

### 1. 一帧两个描边目标，会进入光影缓存键

代码链：

- `Bar.RenderLoop.cpp:3122-3132` 对 ColorSelect1 的两个分支都执行 `ft.SetTar(1.0)`；ColorSelect2 至 ColorSelect11 重复该模式，例如 `3152-3161`、`3413-3422`。
- 同一布局函数稍后在 `Bar.RenderLoop.cpp:4187-4196` 对上述 11 个对象执行 `ft.SetTar(drawAttributeLayoutScale)`；收起时它是紧凑缩放，非 `1.0`。
- `Bar.RenderLoop.cpp:4238-4253` 又同步几何/描边动画时长，因此不能仅用普通固定时长插值推测结果，应与真实 `SyncValueDuration`、动画批次的后半程规则联合分析。
- 最终渲染用 `Bar.RenderLoop.cpp:9872-9879` 的 `panelGeometryScale = panel.w.val / 370.0`，设置 `frameDiffuseMaskGeometryScale = 1.0 / panelGeometryScale`。
- `Bar.Rendering.cpp:2443-2448` 先把 `ft.val` 乘 `frameZoom`；`2313-2324` 再乘上述反缩放；`1464` 将其按四分之一像素量化为 `strokeWidthQuarter`。

因此 key 中实际描边来自：

```text
strokeWidthQuarter = max(1, lround(float(ft.val * zoom) * float(1 / panelScale) * 4))
```

等比动画本应使 `ft.val / panelScale` 恒为完整几何描边 `1.0`。双目标打断后比值漂移，会创建不同描边宽度的父遮罩；`1528-1529` 的遮罩 padding 也跟随变化。若光标仍照到正在收起的颜色块，这条错误直接变成额外的 A8 bitmap / brush 创建和 Gaussian 提交。

独立动画调查提供 `input-animation-ft-model.py` / `input-animation-ft-trace.csv`。主会话进一步把当前 `Metrics.cppm`、`Animation.cppm/.cpp` 原样编译为 module，使用 `animation_target_probe.cpp` import 真实模块：`animation-target-probe-results.txt` 的第 23 帧 `ft/panelScale = 4.117780523`，而单一目标对照误差仅约 `5.55e-16`；第 24 帧精确收敛。该验证执行真实动画模块，但没有运行完整 RenderLoop 或 GUI。

本研究读取其逐帧 CSV，按实际 FLOAT 转换、反缩放与 `lround(*4)` 计算父 key。输入为 60 Hz、0.4 秒关闭动画、正常半径保持等比、只把完整稳态 key 作为已预热条目：

| zoom / 控制 | 关闭期间 strokeWidthQuarter 独立值 | 首次新增父 mask | 同样时序第二次新增 |
| --- | --- | ---: | ---: |
| 1.0，双目标 | 4,5,6,7,9,12,16 | 6 | 0 |
| 1.0，唯一目标 | 4 | 0 | 0 |
| 1.3，双目标 | 5,6,7,8,9,12,15,21 | 7 | 0 |
| 1.3，唯一目标 | 5 | 0 | 0 |

这里明确模拟了父 FIFO 容量 24、两次动作之间不换 zoom、不重建资源、没有其他 key 淘汰。已有历史条目可让第一次更少创建；其他场景淘汰、帧时序改变可让下一次又出现新 key。**固定动作的缓存暖起来后会复用，所以不能称它为每次收起必然持续 Gaussian 击穿。** 所有数目已把 11 色块合并为共用 key；输出在 `lighting-cache-model-results.txt`。

**不能夸大之处：** 11 个普通色块当前几何、描边、归一比例相同；它们通常共享一个变化的父 key。不能把 11 个对象的 draw 次数当作 11 个独立 cache miss。该面板动画最终可收敛，也不能仅凭它解释无限期全局卡顿。

**最小修复候选：** 删除这 11 个色块前面重复、无条件设置为 `1.0` 的描边目标，保留后面唯一包含最终紧凑比例的 `SetTar`。确认后应验证整个关闭过程 `ft/panelScale` 恒定、父 mask key 稳定，并保留已有中文关键注释；不要把关闭动画、降低光影质量作为替代。

### 2. 光标空间位置决定大量分片调用，缓存命中也会慢

`DrawPointLightFrame` 会在 `Bar.Rendering.cpp:2244-2270` 将控件边界外扩 `(strokeWidth + 6 * zoom)`，再测第一/第三光椭圆与边界的交集。第三光半径为 `240 * zoom`（`Bar.Rendering.cppm:28`、`Bar.Rendering.cpp:448`）。相交时才启用该控件的 diffuse/hard-light。

具有具体产品尺寸的例子：

- 展开的绘制属性面板宽 370 DIP（`Bar.Layout.cppm:37`），高 185 DIP（`Bar.Main.cpp:50`）。光标位于面板中心 `(185, 92.5)` 时，240 DIP 半径能照到整个面板。
- 11 个普通色块均为 `30 × 30 / radius 4 / stroke 1`（`Bar.Initialization.cpp:364-484`）；它们禁用第一光（`555-565`）。
- 四个启用笔型按钮均为 `115 × 30 / radius 4 / stroke 1`，位于 `x=250`、`y=40/75/110/145`（`Bar.Main.cpp:64-68`，`Bar.RenderLoop.cpp:3586-3613,3626-3652`），同样禁用第一光。
- 它们的 frameLightPct / ObjectPct 在展开稳态允许第三光，无需指针分别 hover 每个对象。

`Bar.Rendering.cpp:1977-2014` 对目标圆角与缓存量化圆角做严格 `0.001F` 比较：

| 当前状态 | 同一形状的路径 | 上述 15 个对象每帧柔光 API 调用 |
| --- | --- | ---: |
| `zoom=1.0`，目标圆角 `4.0` 等于缓存 `4.0` | 3×3 分片 | 135 |
| `zoom=1.3`，目标圆角约 `5.2`，缓存圆角 `5.25` | 5×5 分片 | 375 |
| 光标远离这些对象，或第三光未启用 | 第三光被裁掉 | 0 |

以上只是 15 个控件的柔光调用，不包含主栏、主按钮、自定义圆形色块、其他面板、hard-light 或基础边框。11 个色块与 4 个按钮的父 mask key 相同，所以两个 zoom 场景各只需要 **1 个父 key**；这是调用放大，不是 135/375 次高斯重算。每个被照中的一般圆角形状若同时绘制第一、第三光，会分别执行这组分片。

圈形等退化中段几何会减少实际片数。`FillRoundedRectDiffuseMaskSlices` 会跳过宽或高不大于零的片（`1629-1630`），因此不能对所有形状无条件按 9/25 计数。

此外，`frameDirtyClipRect` 只在 `PushFrameDirtyClip` 保存，Shape/PointLight 不读取它进行 CPU 层预裁剪。D2D dirty clip 会限制真正触碰的像素，但完整场景的控件判断、brush 设置和 mask draw 调用依然发生。此处只是潜在 CPU/驱动命令成本，不能从 API 调用次数换算成毫秒。

### 3. 整图 exact mask 常态不可达

`ResolveRoundedRectExactMask` 的约束包括：

- `Bar.Rendering.cpp:1738-1742` 要求 geometryScale 接近 1 且没有全局 exact failure latch。
- `1744-1751` 要求目标圆角等于父缓存量化圆角。
- `1753-1757` 要求 context DPI 恰为 96。
- `1759-1764` 要求整个 world matrix 恰为 Identity，**连纯整数平移也禁止**。
- `1770-1776` 要求目的边界像素对齐；候选连续看到 8 个实际绘制帧才晋升（`1849-1864`）。

主栏 `Bar.RenderLoop.cpp:9750-9769` 的 base/body transform 叠加 `-capacityOrigin`；刚性浮层、抓手同样叠加。`Shape/DrawPointLightFrame` 保留调用者变换；`DrawBarButtonVisual` 仅在按压时乘局部 scale（`Bar.Scene.cpp:440-457`），没有为 mask 临时 reset Identity。因此常态 capacityOrigin 非零时完全走分片路径。

PageControl 虽然在 `PageControl.cpp:955-959` 先设置 Identity，但 `Bar.Scene.cpp:2337-2338` 会再设置 `Translation(outset,outset)`。该客户端也不调用 `PushFrameDirtyClip` 推进 `frameDiffuseMaskFrameSerial`；没有现成 ready item 时 `Bar.Rendering.cpp:1811` 直接 fallback。其 exact 缓存同样不能按主栏历史设计假设视为有效。

**修复方向应拆开：** 首先可以审查是否放开保持像素对齐的纯整数平移；必须继续拒绝 scale/shear/rotation 与非有限变换，并保留正确的 local/device pixel 对齐关系。分数 zoom 下目标半径与父 key 不同是另一条件，不能简单去掉 geometryScaled 检查，因为当前整图烘焙只实现九宫格。这是独立性能修复候选，还没有本次故障的耗时证据。

### 4. exact 永久降级确实存在，但不能在当前主路径上误判根因

`Bar.Rendering.cpp:1867-1875` 的 exact 创建只要失败，就设置 renderer 全局 `frameDiffuseExactMaskUnavailable=true`；由于该标记在 `1738-1742`、现有 ready 查找 `1804-1809` 之前判断，连已经创建成功的整图也全部停用。

具体失败来源是 `CreateRoundedRectExactMask` 的 `CreateBitmap`（`1659-1661`）、首次白 brush 创建（`1662-1665`）或烘焙 `EndDraw`（`1714-1723`）。尺寸/字节限制被正常拒绝时只是 fallback，不会设置失败 latch。创建失败日志只有 HRESULT，没有 key、尺寸和设备 generation。

该标记只在 `DiscardDeviceDependentCaches` 的 `191` 清掉；由 target 尺寸变化或 epoch 重建触发 `RecreateDeviceResources -> DiscardDeviceResources`（`84-129,133-145`）。不需要重启才能恢复。主 EndDraw 失败则清掉 exact 候选/成品（`234-240`）；若本帧新建过任何遮罩，还会停用整个 diffuse mask（`241-248`）。

如果未来恢复 exact 主路径，合理的小改动是：查已有 ready 后再阻止 promotion；可选新建失败不能让已有稳定条目无故消失。仍需按 key 或 generation 记录失败、避免逐帧重试；不能直接清掉 unavailable 造成忙重试。**当前常态 exact 原本不可达，所以该 latch 不足以解释当前主栏从快变慢。**

### 5. 缓存上限、资源寿命与未发现的问题

| 对象 | 当前 key / 上限 | 审计结论 |
| --- | --- | --- |
| Gradient | RGB + Primary/Cursor；32 个，FIFO（`930-985`） | 鼠标坐标/半径只更新 brush 属性；返回 ComPtr，两个光源之间发生 erase 也不会悬空。颜色过渡可持续产生新 RGB，但稳定颜色恢复命中。 |
| Rounded parent mask | quarter(radiusX, radiusY, stroke, stddev)；24 个 FIFO（`1458-1474,1606-1613`） | 不包含位置、宽高、颜色；稳定一般形状不会每移动一次鼠标就 Gaussian。所有同 key 对象共享同一父 mask。 |
| Geometry parent mask | quarter(width,height,variant,stroke,stddev)；24 个 FIFO（`2036-2052,2180-2184`） | 主超椭圆尺寸/n 的动画可能生成多个变体；稳定几何会命中。原几何 cache 单槽，但当前主绘制只有主按钮使用它。 |
| Exact mask | 每父 6 成品/6 候选；全局最多 48 成品、4 MiB；每项 512 KiB（`1795-1802,1889-1922`） | 有总 byte budget；不是父 A8 mask 的 byte budget。 |

父圆角 mask 与几何 mask 的 **24 是个数上限，非内存字节上限**。圆角 mask 尺寸由圆角+padding 决定（`1528-1548`）；几何 mask 为几何 bounds+padding（`2106-2126`）。访问过较大 zoom/几何后，仍可能保留部分大 A8 bitmap，直到 FIFO 淘汰或资源重建。但圆角父 mask 通常只是小角块，并非整个属性面板；实际内存量需记录后判断，不能仅凭 24 个称为显著内存压力。

在当前稳定访问的 key 数不超过容量时，FIFO 会有限收敛；历史 key 多本身不会造成永久 miss。检查的 11 色块 + 4 笔型按钮工作集只有一个共同 key，并没有证明典型稳态几何工作集超过 24。若要把 FIFO 击穿定为主因，必须捕获每帧实际 key 序列、miss 原因、淘汰对象及资源重建次数。

专用遮罩 context 使用两组 BeginDraw/EndDraw，先画 source，再解绑 source/绑定 output 执行 Gaussian，结束后解绑 target 和 effect input（`1568-1589,2140-2163`）。未发现同 draw span 把仍绑定的 target 当 input、无限叠加 effect input 或已证明的悬空 mask 指针。父 vector 指针拿到后，exact 晋升只改嵌套 exact vector，不改父 vector；绘制结束前没有再次获取父 mask 导致该指针失效。

光源时间轴在 `PrepareFrameLighting` 对 dt、speed、duration 做有限检查；颜色、主锚点、第三光强度均有完成状态（`568-573,622-647,724-733,793-806,887-919`）。未发现鼠标移动本身不断重启 fade 的路径：它仅更新 cursor serial/位置（`881-885`），显隐变化才重启 fade。

### 6. 本轮无 GUI 验证

1. `lighting_cache_model.py`：Python 同构标量模型，验证实际产品尺寸的照明范围、量化 key、分片数量、Identity 条件和 FIFO 收敛条件；还读取独立动画调查 CSV 计算额外 mask key 与重复动作缓存复用。`python -B .../lighting_cache_model.py` exit 0，输出在 `lighting-cache-model-results.txt`。
2. `lighting_slices_probe.cpp`：**原样提取生产** `FillRoundedRectDiffuseMaskSlices` 与 `DrawRoundedRectDiffuseMask` 的函数体；其余上下文为 fake D2D 类型，仅统计调用并断言 AA 恢复与正面积。
3. 用主会话已发现的 ARM64 原生 MSVC 14.44.35207、Windows SDK 10.0.26100.0，`/std:c++20 /EHsc /utf-8 /W4` 编译该独立研究程序；编译 exit 0、无 warning，运行 exit 0。结果保存在 `lighting-slices-probe-build.txt` 和 `lighting-slices-probe-results.txt`。
4. 提取的两个生产函数体按 LF、UTF-8 的 SHA-256 为 `c0bfe72478de461d236f11c2655c2a08ab41157a2770811e4ca462d6ff6de763`，也写入研究 cpp。源文件位置由函数签名边界提取，而不是重写函数逻辑。
5. 运行输出：`zoom=1 -> 135`；`zoom=1.3 -> 375`。生成的 obj/exe 在成功后删除，只保留研究源码和结果；没有 HWND、COM、GPU 创建或真实绘制。这是调用数验证，**不是故障复现或真实 D2D 耗时测量**。

### 7. 现场数据应怎样证实或推翻这条链

建议在批准实施后，与主呈现计时共用低开销计数采集：

- 每帧 parent mask hit/miss/new bitmap/pixels、Geometry new mask、exact fallback reason（transform、quantized radius、alignment、promotion/latch 等）；新建原因要区分 key miss 与整个缓存被资源重建清空。
- 按光源统计实际 `FillOpacityMask` 次数，以及 dt/成功 present 间隔、Draw/GetDC/ULW/ReleaseDC/EndDraw 耗时；不能把大部分延迟出现在 GetDC 自动解释成 GDI copy，因为它可能正在等待此前 draw。
- 关闭/展开属性面板期间采样 `panelScale`、一个普通色块 `ft.val`、归一 `strokeWidthQuarter`；相同操作重复执行，观察 cache miss 是否来自前述同帧双目标。
- 若慢帧的 parent miss 为 0 且已有缓存稳定，但 mask 调用数随鼠标位置从少量跃为数百，应转向绘制提交/执行量；若慢帧始终在输入或其他共享客户端，则降低光影绘制主因优先级。
- 若修正唯一描边目标后 key 稳定，但故障仍在布局静止、非属性面板场景发生，应明确该修复覆盖的是一个已确认缺陷，继续用数据定位剩余问题。

### Related specs

- `.trellis/workflow.md`：本轮保持 planning，发现持久化到 task research，用户批准前不实施。
- `.trellis/spec/native-desktop/index.md`：区分 UI3 主程序与 draw3、共享 epoch 及客户端资源所有权。
- `.trellis/spec/native-desktop/rendering-and-ui.md:924`：共享设备、串行帧与光影缓存；特别是 `949-953` 的几何缓存、动画归一、稳态不反复创建与裁剪要求。
- 同文 `963-966`：遮罩失败降级、防逐帧重试、EndDraw 延迟错误处理。
- 同文 `1349-1353`：同一属性先计算最终目标再 SetTar 一次；禁止同帧基础目标与带 offset/scale 最终目标来回重启。

### External references

- Microsoft Learn，Direct2D 1.1 / `ID2D1DeviceContext::FillOpacityMask`：使用 alpha mask 绘制，要求 ALIASED。<https://learn.microsoft.com/en-us/windows/win32/direct2d/id2d1devicecontext-fillopacitymask-overload>
- Microsoft Learn，`ID2D1RenderTarget::FillOpacityMask`：draw 不同步返回 HRESULT，错误由 EndDraw/Flush 报告。<https://learn.microsoft.com/en-us/windows/win32/direct2d/id2d1rendertarget-fillopacitymask>
- Microsoft Learn，Resources Overview：同一个 ID2D1Device 创建的 context 可共享资源；设备丢失需重建依赖资源。<https://learn.microsoft.com/en-us/windows/win32/direct2d/resources-and-resource-domains>
- 检索时间 2026-09-22。这些资料证明 API 合同，不能证明本机每个 mask call 或同步点固定耗时。

## Caveats / Not Found

- 尚无报告故障用户的 CPU/WARP/驱动/zoom/present 数据；用户描述后来补充为可能自行时快时慢，因此不能坚持永久 latch 或历史容量是唯一解释。
- 本研究当前代码与用户引用的历史 commit 未做 Git 比较；历史性能结论不能替代当前代码验证。
- 只做研究模型和独立无 GUI 计数验证，没有主程序 build、实际 D2D 光影 benchmark、ULW 或 GUI 复现；这符合本轮授权。
- 还没有枚举所有用户自定义按钮/所有交错面板动画的最坏 key 序列，不能断言 24 容量永不击穿；但也不能只凭缓存容量有限或色块很多推断已发生击穿。
