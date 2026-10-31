# Research: 入场时序与 WARP 性能瓶颈

- Query: 总时长约翻倍、减慢书写并停顿后归位；遮罩下先准备 ImGui 首帧，优化当前低帧率，保留 Win7 SP1 + KB2670838 兼容和合理资源占用。
- Scope: internal；只读当前生产 Setting 协程、主页/字标热路径、ImGui 动态 atlas、共享调度及现有 WARP bench。
- Date: 2026-10-11

## Files found

- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：常驻协程、首帧、主页和导航生成、DX11 提交及 Present。
- `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp/.h`：Show 入场状态、实际布局尺寸门禁、覆盖层及真实主页。
- `Inkeys/Inkeys/UI/Setting/Setting.HomeBrand.h`：一次曲线网格缓存、书写区块/截面/临时圆头及视差缓存。
- `Inkeys/Inkeys/UI/Setting/Setting.Base.cppm`：独立 discard swap chain 和 RTV，借用共享 WARP device/context。
- `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp`：唯一串行渲染线程和最多 60 FPS 调度。
- `Inkeys/additional/imgui/imgui_draw.cpp`：动态 atlas 与字号 bake，Build 的实际语义。
- `Inkeys/additional/imgui/imgui_impl_dx11.cpp`：动态纹理更新、全 DrawData VB/IB 上传、既有 HRESULT 恢复。
- `InkeysHeadlessTests/setting_home_warp_tests.cpp`：FL11.0 离屏像素及 100 帧完成提交对照。
- `research/brand-entry-validation.md`：历史同机离屏测量，已承认未达 60 FPS，不能当原生帧率。

## Findings

### 1. 已确认的隐藏成本；尚未证明唯一慢帧根因

- `Setting.cpp:2126-2138` 每帧执行三个 NewFrame 和 Fluent style，`2321-2382` 正常内容 child 和 RenderHome，`3899-3919` 正常导航和前景覆盖，`3938-3951` 清 RTV、完整 RenderDrawData、Present(1,0)。即使书写/停顿时覆盖 alpha=1，仍完整生成和提交其下的页面及导航。
- `Setting.Pages.cpp:454-498` 每帧重做真实文案测量、字符串连接、多段 InlineLayout 的 string/vector 构造；`16-49` MeasureInline 构建 joined、substr 和 fragments。原 bench 的 ImGui 分配计数不覆盖这些 C++ 分配。
- `Setting.Pages.cpp:127-136` 已在活动入场时跳过横幅第二份字标，所以并非“同时画两份完整 Inkeys”造成当前成本。
- 透明渐退开始前，后台页完全被遮住。最优先可验证优化是正常首帧准备后，在完全不透明的稳定书写/停顿阶段停止后台整页重复生成和提交；渐退首帧恢复正常页渲染，标题栏/关闭、输入取消及尺寸门禁继续有效。不要为此停掉唯一共享调度器或加线程/定时器。
- 历史 `research/brand-entry-validation.md`：1000×1100 WARP，writing 完成提交 median 37.136ms、hold 38.559ms、dock 37.210ms，static 30.626ms；这已经不是 60 FPS 的完成预算，但数据包含 CopyResource/Map 等待，不能按倒数直接声明原生 FPS，也不能从 warning 宣称 GPU/字形唯一根因。

### 2. 字体 Build 不等于已准备真实页面所有字号/字形

- `Setting.cpp:1411-1501` 设置多个 regular/strong/design/icon 字体及 RasterizerDensity，调用 io.Fonts->Build；`1885-1917` 在第一次 Show 前建立 backend/resident 资源。
- `imgui_impl_dx11.cpp:729` 声明 RendererHasTextures，`imgui_draw.cpp:3428-3445` BuildMain 只在没有动态纹理支持时 LegacyPreloadAllGlyphRanges。动态模式不会因为调用 Build 就把首页所有汉字和实际字号全部提前 bake。
- `imgui_impl_dx11.cpp:227-241` 在 RenderDrawData 前处理 Textures WantCreate/WantUpdates；真实测量/绘字可以在第一次使用实际字号时继续请求字形和上传。
- 因此 `Setting.cpp:1500` “Show 不承担字体工作”的注释过强；只能说 resident 初始化完成，不应据此排除首开字形卡顿。
- 推荐有限暖帧：实际尺寸、当前语言/主题/倍率下生成完整首页/导航，完成动态 atlas 上传和真实 backend 资源；动画计时仍为零，纯色遮罩有效。需要最多有限帧容纳 AutoResize 子窗稳定，不能全 CJK 字库 × 所有字号预烘焙。冷启动和热重开分别测量。
- 不得在缓存 DrawData 时保留跨帧 ImFontBaked* 或原 mutable draw-list 指针；atlas grow/repack 会更新 UV/纹理，旧复制几何必须按字号/字体重建/atlas revision 失效，或优先不缓存整页而仅在 opaque 期间不生成。

### 3. DrawWriting 热路径的真实情况及正确性陷阱

- `Setting.HomeBrand.h:34-242` 曲线和 AA 网格仅 Geometry() 首次 Build；无需每帧曲线采样。`347-372` 普通字标通过现有 PrimReserve/PrimWrite 直接提交。
- `374-397` DrawWriting 先扫描各 stroke blocks 计数，然后再次扫描写 indices，始终 reserve+提交 mesh 的全部顶点，即使只显示很短书写前缀。`399-404` 再线性查 sections，临时 cap 只用有限顶点；circle 三角函数表已静态缓存。
- blocks **不是整体严格 progress 排序**：`181-209` 正文截面索引后，`210-236` 再追加首/末圆头；首 cap 可能 progress=0，但在末尾。不能直接 upper_bound 现有 blocks 或把全局 indices 简单截成一段连续前缀。
- 如原生分段测量证明字标上传/CPU 显著，可离线生成每笔 section 对应可见区块索引计数和顶点范围，按六组做有界 prefix 查找/提交。所有实际引用顶点和圆头截面都要保留，重映射后对完整进度和 k 首尾接合像素复验；不要先降低采样或 AA 品质。
- 更低风险先优化 opaque 背景和首帧预热；此前被证实正确的网格、透明 AA、k 接合、固定命中框保持。不要把字标复杂度大归为结论而忽略整客户区 WARP 像素填充。

### 4. 时钟、冻结阶段和尺寸边界

- `Setting.Pages.cpp:222-248` 默认 120ms 等待、530ms 写、300ms hold、270ms 曲线渐退、110ms settle，总长 1.33s；`317-356` 在 RenderHome 实际布局检查后推进时钟，`Pages.h:103-123` 全局动画开关/速率及 ShowSerial 触发。
- 仅修改时序不会改善重复成本。按用户“总时长慢一倍”约 2.66s，可把等待/写/hold/曲线/settle 设置为统一命名边界，再测试完整字标先停顿、背景透明和移动同步开始；不要依赖多个散落 magic number。
- 若 opaque 快路径跳过 RenderHome，必须把有效帧推进从布局 helper 拆至安全的 motion advance；保持首帧计时零、隐藏/遮挡/最小化重基准、动画开关/速率中途变化及同次 Show 不重播。
- 真实 resize/font rebuild/theme/atlas epoch/input 使缓存失效。尺寸变化先重验实际可见横幅与 64 样本运动包络，不安全立即取消；不能用缓存旧矩形继续动画。
- AutoResize child 暖帧不可简单省去：首次内层 clip 为空属预期。若 opaque 期间不 Begin 这些子窗，恢复 fade 的第一帧可能再次出现旧 LastFrameActive/AutoResize 状态；应在渐退前的最后一两个 opaque 暖帧恢复完整生成，或用明确稳定布局复位，像素检查首个半透明帧无突然 pop。
- 标题栏仍可关闭；任意真实输入可以结束入场，但取消帧不得把该输入穿透链接/导航。正常 ImGui NewFrame/输入队列消费保持，不停用 backend 或绕过 SessionState。

### 5. Present 与共享调度只作为测量项

- `Setting.cpp:3951` Present(1,0) 可以阻塞当前共享渲染线程；`RenderPipeline.cpp:735-740` deadline 以回调开始时间计算，开始后超过 16.67ms 不再额外 sleep。代码并非无条件两次完整帧 sleep。
- `Setting.Base.cppm:75-102` discard 双缓冲/既有 API 是当前兼容链。不能因用户希望流畅就换 FL11.1、flip-discard、waitable swapchain 或独立设备并宣称 Win7 可用。
- 先分开记录 CPU 首帧/正常生成、backend 提交、Present 等待和实际相邻呈现间隔；若 Present 显著阻塞才考虑现有兼容 API 中的最小调整。不得直接 Present(0) 解锁造成 WARP 满核跑、撕裂或其他共享客户端饥饿。

## Minimal implementation order

1. 保存当前基线 exe/hash、测试配置、窗口客户像素尺寸/DPI/倍率/语言/主题；不要先改完再凭旧日志横比。
2. 统一时序约 2×；有限正常暖帧在遮罩下准备实际首页和 atlas，计时零。
3. opaque 书写/hold 阶段用当前后台几何稳定状态；首选省后台绘制/提交，保持输入/标题栏和 cached layout 安全。渐退恢复正常渲染，输入取消/resize 时当帧安全回到完整 UI。
4. 若测量仍显示字标为主要成本，进行有界前缀顶点/索引提交缓存，不改曲线品质。shader/离屏大纹理不是首选；允许方案变更必须先记录明确预算和兼容边界。
5. 真实生产协程与 headless WARP 两类证据并列；正常最终页面 RGBA、圆头/k 接合/下伸、下层布局和命中不变。

## Baseline measurement method

- 原生 Windows11 ARM64 WARP 同 exe 配置：固定客户像素 1920×1400（DPI192，即 960×700DIP），另 1440×1040 与更大窗口；对照冷首开、热重开、静态/等待/书写/hold/dock/settle。性能运行期间避免逐帧 GDI 截图，采帧截图与计时分次。
- 用已有单调 QPC/steady_clock 分段测量：RenderSettingFrame resident/font/resize setup、ImGui 页+导航生成、entry geometry、RenderDrawData、Present；报告 median/p95、最大 cold spike、相邻呈现间隔分布及执行帧数，不能只有平均 FPS。短 2.66s 的一次动画样本太少，需多轮热重开并单列 cold。
- 同时记录真实 TotalVtxCount/IdxCount/commands、cache rebuild/capacity、Textures revision/update 和 resident memory 的开屏前后差。ImGui allocator 计数、C++ string/vector 分配及驱动内存分别标记；没有 global allocator 观测不能声称整页零分配。
- 现有 `setting_home_warp_tests.cpp:27` 固定1000×1100像素，`149-194` 是生产主页/导航 helper 的测试 frame，**没有完整 Setting.cpp 协程、HWND、swapchain Present、caption 字形或真实图片**。`677-731` 20暖+100测，`121-146` Submit 包括 CopyResource/Map 等待；适合内部前后对照，不代替 native 用户观感。
- 扩充 WARP 检查到实际 DPI2 的相同物理面积，明确 FL11.0；保持 cold 首帧、暖帧到首次透明、整段 hold 像素相同、最终静态精确一致、原 k/cap zoom1/1.875/4验收。GPU readback 时间可作为完成成本，不冒充纯 GPU timestamp。
- 使用 bounded/native 缓冲采样，不逐帧格式化日志/写盘，不新增 profiler 线程或 timer。既有 UI3Diag warning 仅辅助相关性，不能根因诊断。

## Related specs

- `.trellis/workflow.md`：research 必须持久化、implement→check→spec 流程；本代理不编辑产品/spec。
- `.trellis/spec/native-desktop/rendering-and-ui.md:113-184`：共享串行调度、生命周期、禁止客户端本地 Sleep；`2101-2158`：主页矢量/Show/Hide/尺寸/时钟、像素与性能证据边界（旧 1.33s 时序由本轮需求覆盖）。
- `.trellis/spec/native-desktop/build-and-compatibility.md:73-95`：平台声明不能当运行证据，完整 solution ARM64 验证。
- `.trellis/spec/native/platform-and-resources.md`：Win7 SP1+KB2670838 兼容目标与实机证据分开；本设置路径沿用 existing FL11.0 WARP/backend，无新增现代专属 API。

## External references

- 本轮仅使用仓库实际源码及既有验证，不新增外部 API 依赖；没有为性能建议声称新的 DXGI/D3D 可用性。相关平台更新官方页面在用户既有批准计划中为 https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7，本次未新浏览或推导额外能力。

## Caveats / Not Found

- 本代理未启动 GUI、未使用 Computer Use、未构建/运行性能或测试、未改产品/任务元数据/spec，未进行 Git 操作；以上具体源码成本是可测候选而非已实测根因。
- 没有实际 Win7 SP1+KB2670838 机器，不可声称该平台已达到任何 FPS 或恢复通过。
- 当前旧文档存在历史时序与边界，研究以生产源码和最新用户授权为准；本轮任何选择都仍需实际基线和独立审查。
