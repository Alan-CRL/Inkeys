# UI3 SVG 与路径资源只读审查（2026-09-28）

范围：当前发布工作区的生产 `Bar.UI.cpp/.cppm`、`Bar.Rendering.cpp/.cppm`、`Bar.RenderLoop.cpp`，并追到 `Bar.Scene.cpp`/PageControl 的共享调用者。依据本任务 PRD、design、implement、`native-desktop/rendering-and-ui.md`、`ui3-render-diagnostics.md` 与 08-09、09-22 历史记录。只读源码；未运行构建、HWND、离屏 D2D、性能采样或像素比较。以下“可达”是源码调用链，不代表已测得整帧耗时。

## 资源到像素的真实链路

| 阶段 | 生产证据 | 频率/边界 |
| --- | --- | --- |
| 资源与内容 | `BarUiSVGClass::InitializationFromResource/SetTarFromResource/TransitionToResource` 在 `Bar.UI.cpp:219-274` 从内嵌 `UI` 资源取 UTF-8，再转 UTF-16。主栏初始化见 `Bar.Initialization.cpp:264-277,653-668`；主题切换的主 Logo 有 `Bar.RenderLoop.cpp:1294-1302` 的深浅色变化门。 | 初始化、真实资源/主题切换；未找到每帧读取资源的主 Logo 路径。当前调用者使用内嵌资源，不能把任意外部 SVG 输入当成已确认攻击面。 |
| 内容尺寸 | `InitializationFromString` 和 `SetTarFromString` 经 `CalcWH()` 解析目标文本，`Bar.UI.cpp:213-239,393-399,525-533`；Animated 内容在中点由 `AdvanceContentTransition` 解析快照目标、校验 timeline generation、提交并清缓存，`:329-369`。 | 首次创建与内容切换；中点解析和下一次 `CacheBitmap` 解析是两次独立调用。解析在 timeline 锁外，但仍在调用它的 render callback 中；切换瞬间的峰值需要实测。 |
| 重新着色与 CPU raster | `CacheBitmap` 在 `Bar.UI.cpp:400-489` 将当前 UTF-16 文本转 UTF-8，以 `rgba(10,0,7,0)`/`rgba(9,0,2,0)` 替换为当前 RGB，调用 `lunasvg::Document::loadFromData`、`renderToBitmap(width,height)`。 | 只在上层 cache miss 时发生。`Bar.RenderLoop.cpp:1323-1326,5622-5625` 证实 Logo Ink 的颜色可动画，颜色每次实际改变时会使该 SVG 下一次绘制重新解析和 raster；发生多少帧、耗时多大尚未测。 |
| 上传与绘制 | `CacheBitmap` 用 premultiplied BGRA `CreateBitmap` 成功后才更新单实例 `cacheBitmap/cW/cH/cColor1/cColor2`，`Bar.UI.cpp:467-489`；`BarUIRendering::Svg` 每次可见绘制调用 `DrawBitmap`，必要时才调用 `CacheBitmap`，`Bar.Rendering.cpp:2723-2819`。 | D2D 上传在 miss；`DrawBitmap` 在每个实际绘制帧。现有 `UI3Diag` 没有单独的 parse/raster/upload/draw 计时；不能拿 `GetDC` 长耗时机械归到 SVG 或 ULW。 |

`Svg` 的 miss 条件是无 bitmap、颜色值不同、非动画稳态的基础像素尺寸差超过 `0.01`，或动画中的目标尺寸超过当前 raster 的 `1.35` 倍（`Bar.Rendering.cpp:2747-2785`；阈值在 `.cppm:24-25`）。动画时尽量复用已有 bitmap，超阈值按目标尺寸预建以保留视觉质量；不能宣称每帧解析，也不能把禁用重栅格或降低分辨率当作优化。平移、旋转、透明度在最终 `DrawBitmap` 处理，通常不构成 bitmap key；尺寸使用 `frameZoom × w/h`，所以 DPI/zoom 改变会触发适当重栅格。主题通过颜色动画或资源切换失效。内容没有显式 version/hash key，依赖 `SetTarFromString` 与中点提交主动 `ResetCache`；当前已搜索的生产调用者使用这些包装入口，未发现绕过它们直接改 `svg` 文本的路径。

单实例只保留一张 SVG D2D bitmap，失败时 `CacheBitmap` 不提交半成品（`Bar.UI.cpp:463-489`）。无全局 SVG 字节预算、尺寸上限或 hit/miss/evict 计数；多实例总占用和首次 cache 峰值需以实际尺寸及对象数测量。`renderToBitmap(static_cast<int>(tarW/tarH))` 前没有局部有限值、范围或 `width×height` 上限（`:400-463`），但当前已找到的内容均为内嵌资源、常规布局尺寸；这是需沿输入来源继续审查的**静态边界假设**，不是已确认可利用漏洞。解析或上传失败不会标记永久失败；后续仍被请求的绘制会重试，是否构成连续帧背压要看请求来源与失败率，不能静态宣称 busy loop。

## 路径几何

当前主栏仅建立一个 `MainButton` 超椭圆（`Bar.Initialization.cpp:254-262`），正常绘制只有 `Bar.RenderLoop.cpp:11527-11538` 的一次 `spec.Superellipse`。`GetSuperellipseGeometry` 的局部路径按 `width/height/n/segments` 精确相等复用，位置不同只创建 `ID2D1TransformedGeometry`；局部路径 miss 才计算四角 `powf`/三角函数、生成 Bezier 数组并创建 `ID2D1PathGeometry`（`Bar.Rendering.cpp:2533-2631`）。因此静止时不是逐帧细分；主按钮展开/收起时 `w/h/n` 动画变化可逐帧重建路径，拖动只变位置时仍可能逐帧新建变换几何。`Superellipse` 按真实几何 Fill/Draw，PointLight 再以 geometry variant 调用 `GetGeometryDiffuseMask`（`:2633-2721`）；后者有最多 24 项的量化尺寸/variant/描边/模糊键（`:2066-2243`），缓存命中不等于不绘制光影。用固定 `n` 或粗量化路径省时会改变可见轮廓，不宜无数据实施。

路径缓存是一组局部/变换几何，不是会因多个主按钮互相驱逐的多实例工作集；当前只有一个主按钮，不能据单槽本身判定持续退化。`Bar.Rendering.cpp:155-219` 在 target 尺寸或 device generation 重建时清这组几何、光影和主栏 `svgMap`/注册按钮缓存。`EnsureDeviceResources` 只有 generation、context、target、interop 和目标尺寸都相同时直接复用（`:106-152`）；因此容量意外反复改变会让 SVG/路径一起冷启动，但现有 capacity 高水位合同意在避免普通拖动重建，必须用实际 target 变化计数证实。

## 确认的跨调用者缓存失效缺口

`BarSurfaceScene::Impl` 单独拥有 `std::vector<Widget> widgets`，每个 Widget 持有稳定 `BarButtonClass::icon`（`Bar.Scene.cpp:501-520,1306-1392,1406-1476`）。Scene 绘制入口将这些图标交给同一个 `BarUIRendering::Svg`（`:433-480`），该函数在 bitmap、颜色和尺寸都匹配时直接复用。Scene 的 `ReleaseDeviceResources()` 只调用 `rendererOwner.spec.DiscardDeviceResources()`（`:1948-1952`）；后者只遍历 `rendererOwner.svgMap/pngMap/barButtonSet`（`Bar.Rendering.cpp:167-181`），**不遍历 Scene 的 `widgets`**。`PageControl.cpp:1212-1234` 会在失败/DeviceLost 路径释放 Scene renderer，`Bar.Scene.cpp:2023-2029` 后续又能为新 epoch 创建设备资源。稳定 Widget 在配置相同的下一帧保留（`:1363-1364`），旧图标 `cacheBitmap` 没有失效信号。

结论：源码层面确认 Scene 图标 D2D bitmap 没有随 renderer/device epoch 清除，属于资源所有权合同缺口；旧 bitmap 被传给新 device context 的运行期 HRESULT、是否造成 PageControl/Whiteboard 图标缺失或反复 Retry 尚未实测。最小修复应由 `BarSurfaceScene` 在自己的 mutex/资源释放边界遍历自身 widgets 并 `icon.ResetCache()`，必要时对 PNG 同理；保持内容、动画进度和 Widget 身份，避免每帧重建。测试必须调用**生产 Scene**：先在 epoch A 离屏画并确认 icon 有 bitmap，调用 `ReleaseDeviceResources` 后确认缓存清空，再在独立 epoch B 画并检查上传/`EndDraw` 成功和像素；同 epoch target resize 与 PageControl `DeviceLost` 路径另测。只检查 `DiscardDeviceResources()` 存在、或主栏 `svgMap` 清理，不能覆盖这个 Scene 所有权漏洞。

## 推荐的最小无 HWND 测量与决策

既有 `Bar.EraserAttribute.Test.cpp:80-100,643,724` 的 `--bar-eraser-offscreen-test` 直接初始化生产 RenderPipeline WARP/D2D、构造真实 `BarUIRendering`，且显式不创建 HWND；可在同一正式离屏测试入口增加独立 SVG/path 采样，避免把算法复制成测试实现。按同一个 `UI` 资源和相同 `frameZoom`/颜色序列分别记录：资源解码/宽高解析、miss 的 UTF16→UTF8 与颜色替换、lunasvg parse、CPU raster、D2D upload、`DrawBitmap`+`EndDraw`；路径记录 local path create、translation create、mask hit/miss 和 `EndDraw`。场景为冷启动、首次切换、预热稳态、颜色渐变、尺寸动画、主题/DPI/epoch 切换及重复拖动；至少三轮 Release、固定效果和实际样本量，输出 median/P95，P99 仅样本量足够时给出。独立测资源字节、缓存 create/evict、峰值及失败重试。离屏 `EndDraw` 可能包含 GPU 同步，需独立于 CPU raster 记账。

性能候选优先级：先量**动画颜色实际每帧触发的 SVG parse+raster+upload**与主按钮变化时的 path 构建占比。若三轮重复中其尾延迟确实显著且超过噪声，再考虑只复用不变解析结果或缩短同内容切换的重复解析，保持颜色、尺寸、alpha、轮廓和设备 epoch 完整 key/失效；不要预先引入跨实例无界 bitmap cache。当前尚无这两个路径的真实模块耗时、GUI/ULW 成功帧或 HC/H2 同机数据，故**不建议现在实施性能缓存/降质改动**。Scene 失效缺口是独立正确性修复，不以性能收益记账。

历史定位：08-09 归档任务已记录 SVG bitmap 复用/1.35 放大阈值、superellipse 局部路径缓存，不能重复包装成新收益；09-22 研究与 09-23 修补重点在光影遮罩分片、颜色块目标和诊断，仍未单列 SVG/path 真实阶段耗时。所有上述结论以当前产品调用链为准，旧 benchmark 不能替代 HF 真机前后对照。
