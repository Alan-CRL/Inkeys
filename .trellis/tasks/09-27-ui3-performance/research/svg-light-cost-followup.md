# UI3 SVG、路径与动态光影成本续查（2026-09-28）

范围：当前发布工作区的生产 `Bar.UI`、`Bar.Rendering`、`Bar.RenderLoop`、`Bar.Scene` 与现有无 HWND 离屏入口。只读复核；本轮未构建、运行采样或 GUI，未修改产品/测试/父账本。沿用父任务 `performance.md` 的 Release 同环境、至少三轮、噪声和像素门槛。`research/exact-mask-optimization.md` 已记录整数平移 exact A8 虽减少分片但产生 451 个像素差异并撤销；本报告不重新推荐该方案。

## 实际工作链和失效边界

| 资源/阶段 | 命中、未命中、创建、驱逐与失效 | 成本位置与现有证据 |
| --- | --- | --- |
| 内嵌 SVG 内容 | `Bar.Initialization.cpp:265-277,364-495` 初始化主 Logo/11 个颜色勾号等；资源登记 `Inkeys.rc:322`。`Bar.UI.cpp:213-239,393-399,525-533` 首次/直接切换先解析内在宽高；内容时间线 `:329-369` 在中点解析目标，按 generation 提交并清位图。主题深浅主 Logo 由 `Bar.RenderLoop.cpp:1294-1302` 状态门切资源。 | 当前调用者是内嵌 `UI` 资源；没有发现主 Logo 每帧加载资源。中点宽高解析与下一次彩色 bitmap 解析是两次不同解析，但只在内容切换时发生，不能称为稳态热路径。 |
| SVG 单实例 raster/上传 | `Bar.Rendering.cpp:2723-2786` 先检查可见/尺寸/内容比例；bitmap 缺失、颜色改变、稳态尺寸改变或动画放大超过 1.35 倍才进入 `Bar.UI.cpp:400-489` 的 UTF16→UTF8、占位符替换、lunasvg parse、CPU `renderToBitmap`、D2D BGRA premultiplied `CreateBitmap`。新 bitmap 成功后才更新 `cacheBitmap,cW,cH,cColor1,cColor2`；最终 `Bar.Rendering.cpp:2790-2817` `DrawBitmap`。 | 透明度、平移、旋转、内容倍率通常只改变最终绘制矩形/transform；`logoInk` 当前画笔色动画可每次颜色值变化触发一次 raster（`Bar.RenderLoop.cpp:1323-1326,5622-5625`），具体次数和毫秒未测。尺寸 key 含 `frameZoom × w/h`，故 DPI/zoom 或真实宽高变化会刷新；颜色/主题进入颜色值或内容重设。SVG 没有现成 parse/raster/upload/draw 分段生产计时、hit/miss/evict 计数或跨实例缓存总字节预算。单实例位图只保留一张，失败不写半成品。 |
| 设备资源与 SVG | `Bar.Rendering.cpp:106-152` 在 generation/target/context/GDI 任何一项变化时先成功创建三项新资源，再 `DiscardDeviceResources`；`:167-219` 清 Bar 的 `svgMap/pngMap/button` 上传位图、路径和光影缓存。`Bar.Scene.cpp` 的独立 Widget 图标所有权缺口已在 `research/scene-device-cache-fix.md` 按正确性修复，不能计为性能收益。 | 一次 epoch/target resize 会让下一帧相关位图冷创建；普通位置移动在 target 大小不变时不会触发该失效。失败后保持旧资源，不能因提前清缓存制造额外创建或错设备引用。独立 Scene 不可借 Bar 自己的 bitmap 指针跨 device 共享。 |
| 主按钮超椭圆路径 | `Bar.Rendering.cpp:2533-2631` 按精确 `width,height,n,segments` 命中单个局部路径；未命中计算最多 128 分段的三角函数、`powf`、Bezier 并创建 `ID2D1PathGeometry`，位置变化则创建 `ID2D1TransformedGeometry`。`Bar.RenderLoop.cpp:11527-11538` 当前只画一个 MainButton；`:2633-2721` 再填充/描边及可选点光。 | 静止的主按钮不会持续重建局部路径；展开/收起的宽高/n 动画可使每帧 path miss，拖动可使每帧 transform miss。没有 path build/transform create/DrawGeometry 独立计时；单槽缓存本身不足以证明反复驱逐。缩减 segments 或量化 n 会改变轮廓，不能直接采用。 |
| 光影 gradient brush | `Bar.Rendering.cpp:952-1019` key 为 RGB 和 Primary/Cursor source，最多 32 项，FIFO；命中仍每次调用 `SetCenter`、`SetGradientOriginOffset(0)`、`SetRadiusX/Y` 四个 COM setter；颜色过渡可产生新 key，鼠标位置/半径仅更新 brush 属性。创建失败同设备 epoch 禁止反复创建。`DrawPointLightFrame` 只在光源与扩张后的形状区域相交时取 brush（`:2269-2338`）。 | 同一个光源/颜色在同一几何组内照到多个控件时，连续调用可能提交相同的四个属性。不同局部变换/颜色必须更新；仅凭代码无法给“同值 setter”真实耗时。现有 `LightingDiagnostics`（`RenderPipeline.cppm:99-109`）只有父遮罩、exact 和 slices，没有 gradient hit/miss/create/evict 或 setter 次数。 |
| 父 A8 mask、exact 与分片 | 圆角父 key 为四分之一像素的半径 X/Y、stroke、Gaussian stddev，24 个 FIFO（`Bar.Rendering.cpp:1469-1503,1639-1647`）；几何父 key 含尺寸、variant、stroke、stddev，24 个 FIFO（`:2066-2106,2237-2243`）。exact 每父最多 6、全局 48/4 MiB、每项 512 KiB、8 个实际绘制帧预热（`:1764-1979`），但在平移下仍走 fallback。`FillRoundedRectDiffuseMaskSlices` 数真实 `FillOpacityMask` 提交（`:1650-1679`）。 | 父 hit 仅省 mask/Gaussian 创建，不省 9/25 片的 draw；09-22 原函数计数曾给 15 个受光对象 135/375 片，本轮真实单控件 WARP 离屏基线在 Identity 为 1、整数/分数平移为 9 片。后者完整像素比较否决了放宽 exact gate，不能按 9→1 估算可交付收益。父 mask 只有数量上限，需实际记录每项尺寸、create/evict/长期内存，不能把数量有限说成总字节有界。 |
| dirty、draw 与成功呈现 | `Bar.RenderLoop.cpp:7812-7924` 光照状态变化分别标记 Primary/Cursor key；`:8687-8755,9095-9126` 用受光边界和抗锯齿留量计算影响，dirty tracker 保留旧/新；`:9744-9785` mapping/alpha 全窗情况强制 full，正常 dirty 限于 candidate viewport；`:9880-9888` 在 `BeginDraw` 后用 D2D clip 限像素。`Bar.Scene.cpp:1899-1924` 只唤醒本次光照确实贡献 damage 的订阅者。 | D2D clip 限实际写入像素，但当前 `Shape/Svg/Word` 仍可为 clip 外对象提交 CPU/D2D 命令；不能未经变换/旧范围分析就跳过。`GetDC` 可包含此前 D2D/GPU flush；Bar 在 GetDC/ULW/ReleaseDC/EndDraw 全部成功后才提交脏区和 viewport 快照（`RenderLoop.cpp:12618-12724`）。无 HWND 离屏测量不能称为 ULW 成功帧。 |

## 可实施前量化的最小候选：相同光源几何的 brush 属性不重复写入

**假设。** 命中 `GetFrameGradientBrush` 的同色/同 lightSource brush 时，代码目前无条件写四个属性。生产 `DrawPointLightFrame` 在同一组受光控件中重复取同一 brush；09-22 的 15 个颜色/笔型控件场景能形成这种工作集，但实际每帧共享多少个 RGB/局部坐标组仍需离屏计数。缓存命中并非资源创建，新增判断只试图减少没有视觉变化的 COM setter，**不影响** mask 创建、分片数、SVG raster、动画时钟或帧率。

**最小实现边界。** 只在 `FrameGradientBrushCacheClass`（`Bar.Rendering.cppm:169-174`）记创建时的 center/radius 和当前固定零 origin；命中时以 FLOAT **精确相等** 比较本次 center/radius，全部相同时直接返回原 brush。任一不同仍按原顺序写 center、零 origin、X/Y radius 并更新记录；非有限数不视为相同。源码中对该 brush 的其他调用仅 `DrawPointLightFrame` 设置 opacity/绘制，没有其他 `SetCenter/SetRadius/SetGradientOriginOffset`（全 Bar 搜索），故同值 no-op 保持 brush 状态。设备 epoch/target 重建已有 `frameGradientBrushCache.clear()`，不会跨 device 保留属性状态；不能缓存或跳过 `SetOpacity`，因为每对象强度可能不同。该候选只涉及现有 renderer 两个文件，不扩到 Scene/Draw3 架构。

**收益上界。** 若一个缓存项在同一帧被 K 个形状以完全相同 center/radius 使用，首次更新仍需 4 次 setter，最多省 `4×(K−1)` 次；如全部 15 对象确属同一个相同键/几何组，理论上最多 56 次。更多颜色、分组变换或每个对象不同局部坐标会降低该数，甚至为零；实际待观测。它无法消除真正占主导时的 `FillOpacityMask` 9/25 分片或 GPU 同步，若 CPU `Shape`/`EndDraw` 时间改善未越过三轮噪声则不保留。

**无 HWND 验证办法。** 复用 `Inkeys.exe --bar-eraser-offscreen-test` 的生产 WARP/D2D `BarUIRendering::Shape` 入口（`Bar.EraserAttribute.Test.cpp:73-75,635-727`），在同一 target/96 DPI/zoom/完整光效下固定一组受鼠标光覆盖的真实尺寸控件，按相同 cursor 轨迹分别测冷创建、预热稳态、逐帧移动和颜色渐变。临时只在测试作用域/离线探针数同键调用、实际相同属性命中与 setter；不得用“15×4”静态值替代。至少三轮 Release，同轮固定 warm-up、重复块和原始 `Shape`/`EndDraw` 毫秒、mask hit/miss/create/slices、目标位图字节、资源趋势；性能采样单独串行，不能和 Draw3/构建同时跑。`EndDraw` 单列，因为它可能承担实际 D2D 同步；11 块均值不是 64 帧逐帧 P99。

**正确性门。** 候选前后每个轨迹关键帧的完整 premultiplied BGRA 像素逐字节相等（含光晕边缘、AA、透明背景与旧光擦除）；覆盖相同键的重复对象、不同局部 transform、颜色/主题变化、尺寸/DPI、epoch 重建与失败回退。继续执行生产 `EndDraw` 并检查 HRESULT；同键但不同 opacity 的对象必须各保留原强度。任何像素差异立即撤销，如本轮 exact-mask 候选那样处理。离屏等价仍不能替代 ULW/硬件/Win7 真机验收。

## 有证据的低优先级与不应提前做的事

- `colorSelect` 在 `Bar.Initialization.cpp:364-495` 建了 11 个各自持有 bitmap 的相同 640-byte 内嵌 SVG，`:9924-10012` 顺序画它们；但 `Svg()` 在 `pct=0` 时直接跳过，所以当前选中状态通常只有对应图标冷创建，不能说首次展开必然有 11 次同步解析/上传。即使共享完全相同内容/颜色/像素尺寸的同设备 bitmap，最大只省后续不同选中色号的 10 次冷解析/上传和约 9 KiB 的 15×15 BGRA 逻辑像素，几乎没有稳态收益。若测出首次换色尾延迟异常，再考虑严格按内容、两色、实际 rasterW/H、device epoch 复用；此轮不建议先引入跨实例缓存。
- `logoInk` 的颜色动画会导致每次实际颜色变化重新 parse/raster/upload，具备高于静态图标的重复工作可能性；但它是可见像素变化，不能简单保留旧颜色、降低 raster 尺寸或按粗色阶量化。若实测主因，应先把 parse、CPU raster、upload、DrawBitmap 分别计时，再设计保持完全像素等价的资源复用，不能凭“缓存已存在”结束调查。
- 主按钮路径动画的 `powf`/Bezier/COM 创建也在真实生产热段，但只针对单个可见对象，仍缺建路径与 `DrawGeometry` 分段成本；无需因单槽路径缓存或历史 57 值扫描先扩张架构。其他 shader CPU/GPU 布局是 Draw3 专用合同，当前 UI3 WARP/D2D 方案不修改 HLSL register/InkPoint 等定义。

## 尚未验证

本轮没有新的耗时/像素原始数据；候选仍是可重复验证的假设，不能报告 UI3 性能已改善。当前 `UI3Diag` 可分 callback、Draw、GetDC、ULW、EndDraw、成功 commit 与 A8 mask 计数，但没有 SVG parse/raster/upload、gradient setter 或路径构建的独立计时，也没有真机 HC/H2 同轨迹数据。正式优化决定需满足父任务噪声门槛；整帧、ULW、Win7 SP1+KB2670838 和用户主观体验仍须分别验收。
