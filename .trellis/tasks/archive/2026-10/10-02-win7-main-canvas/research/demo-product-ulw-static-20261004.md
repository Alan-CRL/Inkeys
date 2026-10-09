# Research: 原 Inkeys3-Draw3 与当前产品 ULW 静态差异

- Query: 用户曾验证 Inkeys3-Draw3 在 Win7 SP1 + 仅 KB2670838 可用；现在先比较原 Demo 与产品的设备、交换链、最终 RTV、读回及 ULW 顺序，寻找能够解释 USER32 调用前 staging 全零的新增差异。
- Scope: internal；原独立源仓库和当前产品只读；外部仓库访问尝试失败后使用本地原仓库。只写本研究文件。
- Date: 2026-10-04

## Findings

### 1. 结论和证据等级

**在所比较的版本中，设备创建、FLIP_SEQUENTIAL 描述、GetBuffer(0)/RTV 建立、dirty CopySubresourceRegion、阻塞 Map，以及 Primary 的 BGRA/DIB/ULW 参数均继承自原 Demo。没有找到这段核心链路的产品新增静态缺陷。**

这不会证明 Win7 当前执行正确：已有现场证明 source staging 全零，不能把输入/document 成功提升为“GPU 像素已成功写入”。本研究采用现有 `gpu-pipeline-narrow-20261004.md` 的同帧光标证据，不重查其 offsets、shader 数学或通用 D3D 契约。

**最重要的身份缺口：找到的原源仓库与旧 TestResults，尚未能对应用户曾在 Win7 成功使用的 EXE、日期、模式和 Hardware/WARP。** 因此“旧 Demo 核心链路与产品相同”属于当前文件对比证据；“这些文件就是当年绿色测试源码”没有证据。

### 2. 找到的文件、版本与边界

- 当前产品基线 HEAD ref 文本：`529722e6877a35d2aa5e804657e55543436610ef`。本次早退 CLI 修补等现有工作区改动按原样保留；本研究不实施、不构建、不运行 GUI、不 commit/push。
- 独立源仓库：`D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3`，其 `.git/refs/heads/main` 为 `8d045298eaaac76f752b4f8b5f3303b3520e50b7`。当前目标规范 `.trellis/spec/native-desktop/draw3-integration.md:5` 已把此 commit 记为 Draw3 来源快照。
- 原仓库远程配置：`https://github.com/Alan-CRL/Inkeys3-Draw3.git`。当前主仓库还保留指向该本地仓库的 `draw3-source` remote。只读 `gh repo view` 因 runner 的 localhost 代理拒绝连接失败，不声称已核验远程最新状态。
- 当前本仓 Demo：`inkStrokeModelerTest/main.cpp`、`draw3/graphics_initialization.cpp`、`draw3/transparent_presentation.cpp`、`draw3/renderer.cpp`、`draw3/renderer_primitives.cpp`。
- 产品：`Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp`、`Draw3.TransparentPresentation.cpp`、`Draw3.Renderer.cpp`、`Draw3.RendererPrimitives.cpp`、`Draw3.DrawingController.cpp`、`Draw3.Host.cpp`。
- 产品外部窗口配置与 style 应用：`Inkeys/IdtMain.cpp`、`Inkeys/Inkeys/Window/Window.cpp`。
- 既有 Win7 证据：`research/capture-224836-analysis-20261004.md`、`research/gpu-pipeline-narrow-20261004.md`。

下面五个关键文件在原独立仓库与本仓 Demo **字节相同**（通过读取 bytes + SHA-256 验证，不用修改 Git 状态）：

| Demo 文件 | 两边相同 SHA-256 |
| --- | --- |
| draw3/graphics_initialization.cpp | `2eb9c4ac175eb0f419423f978247dce119dd51bee420e8ca1bd1af9b9fe4812d` |
| draw3/transparent_presentation.cpp | `3186618c52fd8ecf2aaf9572d7fcbcc25943a699420b9e7f3bfb67694b6962f6` |
| draw3/renderer.cpp | `c2b4e12c31a5696863830e79ce6741d51aa89df21dbb013ff467d88a9b5697b4` |
| draw3/renderer_primitives.cpp | `0ad20200bae52c9ba2fcf64e2cbc4bec3e7bd2d7fd0d3285be124c1aa1195394` |
| main.cpp | `e4ff89c6fb269c6811e45803d10390b981a9b06335f99a8ea19e944f4676a4c0` |

`drawing_controller.cpp`、Demo 项目文件以及 PS 源存在后续差异，不能扩大为“整个 Demo 没有变”。Shader/CSO 与编译 profile/flags 的精确比较由另一个研究分工负责。

### 3. 设备、资源与呈现合同对比

D 路径在表中指本仓 `inkStrokeModelerTest/draw3/`，其上述核心文件与原独立仓库相同；P 指产品 `Inkeys/Inkeys/Drawing/Draw3/`。

| 边界 | 原 Demo / 当前本仓 Demo | 当前产品 | 对 source staging 全零的解释能力 |
| --- | --- | --- | --- |
| 设备创建 | `graphics_initialization.cpp:49-97`：BGRA_SUPPORT、Hardware→WARP、先 11_1/11_0，E_INVALIDARG 时仅 11_0 | `Draw3.GraphicsInitialization.cpp:63-120`：同描述和回退；额外日志及 `resources={}` | 核心相同。初始化时重置资源与保留 HR 不是新增像素路径；旧测试采用什么 driver 仍未知 |
| DXGI device / factory | D `:102-125`：取 IDXGIDevice1、latency=1、adapter→IDXGIFactory2 | P `:129-153`：同获取顺序 | 无新增差异；现场已获取成功 |
| 普通 ULW swapchain | `transparent_presentation.cpp:572-599`：B8G8R8A8_UNORM、single sample、2 buffers、STRETCH、FLIP_SEQUENTIAL、UNSPECIFIED alpha、flags=0 | P `:878-905`：同描述；waitable 失败仍 ordinary fallback | 不是产品新增 enum/格式/flags 改变。不能凭 Win7 标签改 BLT；现场 GetDesc1 也已证明实际 enum=FLIP_SEQUENTIAL |
| HWND swapchain 创建 | D `:494-527`：CreateSwapChainForHwnd；仅 DWM 模式有 unspecified alpha retry | P `:800-833`：同创建方式；绑定 primaryWindow | ULW 路径没有额外 alpha retry 差异 |
| 最终 backbuffer 与 RTV | `renderer.cpp:262-268`：GetBuffer(0) 得到 texture，以 nullptr desc 创建同资源 RTV | P `Draw3.Renderer.cpp:308-314`：同方式 | 当前 Cursor 实际绑定的 RTV canonical resource 与 final backbuffer 匹配。既有证据降低“画 A 读 B”优先级 |
| L2 / L1 / L0 | D `renderer.cpp:270-290`：L2 BGRA DEFAULT RTV/SRV；两套 Add BGRA、Retain R16_FLOAT | P `:316-335`：同主资源描述 | 核心继承；shader 的实际执行仍未证明 |
| state | D `renderer.cpp:440-493`：stroke MAX/MIN、resolve ONE/SRC1、depth disabled、solid/cull none | P `:499-552`：同描述 | 同一 dual-source/raster 不是产品迁移时新增；API 创建成功也不能代替像素证明 |
| staging 描述 | D `transparent_presentation.cpp:203-219`：BGRA、sample1、mip1、array1、STAGING、CPU_READ、无 bind/misc | P `:378-405`：相同；增加 HRESULT 日志 | 无新增 staging format/stride 描述错误 |
| GPU copy 与同步 | D `:293-300`：dirty box、同 left/top destination offset、CopySubresourceRegion→Map READ flags0；未在这段解除 RTV，也不 Flush | P `:513-521`：完全相同模式 | inherited 路径；“未先解除 RTV / 未显式 Flush”不能仅凭差异认定产品回归，也不能未经实验证明而改生产 |
| DIB | D `:223-257`：client size、top-down 32bit BI_RGB、memory DC，先填 alpha1 | P `:408-443`：同配置；Primary alpha1，Selection alpha0 | Primary 语义相同；Selection 差异属于已明确的穿透合同 |
| CPU 行拷贝 | D `:302-322`：RowPitch→DIB width×4；memcpy，再仅改变 destination alpha | P `:191-242,528-538`：同 source offsets/memcpy/underlay，添加 source/final 统计 | 新统计读取 source 不写 source。未发现 SIMD、SSE/NEON 或 OS 分支。低优先级，并且全部零是 USER32 前读取事实 |
| ULW 参数 | D `:324-339`：windowRect destination，完整 DIB size，src=(0,0)，AC_SRC_OVER/255/AC_SRC_ALPHA，ULW_ALPHA，dirty/full | P `:548-575`：同参数，加提交日志/observation | 此处发生在 source 统计之后；即使 Win7 USER32 dirty 有额外问题，也解释不了这些已观测 staging 全零 |
| ULW 是否真正 flip present | D `:702-711`：ULW直接读 backbuffer，只有 GPU 模式 Present1 | P `:1237-1258`：Primary/Selection ULW直接读同 backbuffer，只有 GPU 模式 Present1 | 本次 ULW帧不是 swapchain Present1→flip→DWM 的链路；backbuffer 本身仍可作为需检查的资源种类 |

### 4. 产品确实新增的差异与排序

1. **旧运行模式与设备身份未知 — 排查价值最高，尚非缺陷。**
   原 Demo `transparent_presentation.cpp:664-668` 默认优先 DComp、再 DwmBlurBehind2、最后 ULW。产品 `Draw3.TransparentPresentation.cpp:98-102,945-958` 只允许 DComp/ULW，旧两个 DWM 模式禁用。用户明确记得测试 ULW，必须尊重该证据；但当前找到的本地原仓运行日志不足以把旧 EXE、driver 与 ULW 身份连起来，不能把默认回退顺序当成用户记错，也不能恢复已禁止的 DWM 作为猜测修复。

   限定搜索原仓 `TestResults` 中 `Current D3D device: WARP`、`Software Adapter`、`Active transparent present mode: UlwDirtyRect`、Win7/KB2670838 均未找到匹配运行记录。能找到的 `TestResults/dcomp-fixed-release/run-1.stdout.log:1-8` 是 Qualcomm Adreno X1-85 **Hardware/FL11_1 + DComp waitable**；`dcomp-clean-wpo-diagnostic.stdout.log:7-27` 同样 Hardware/FL11_1，最后 **DwmBlurBehind2**。这些只证明那些特定旧测试的身份，不是 Win7 的成功或失败证据。README:102 仍把 Win7 透明/resize 标为未验证；这只是文档记录限制，不能否定用户后来进行的外部实测。

2. **产品 Host + 两个 HWND/两套 staging — 需要精确实验，静态未发现破坏 source 的点。**
   原入口 `main.cpp:80-121` 直接创建独立 device/presenter/controller；产品 Host `Draw3.Host.cpp:1455-1491` 在 Host 初始化中创建独立 graphics 并向 presenter 传两张外部 HWND。产品 `Draw3.TransparentPresentation.cpp:908-933` 两套 presenter 都持有同一 graphics device/context，且 Primary/Selection 都读取 `renderer.backBufferTexture`。没有把 UI3 共享 device/context 混入 Draw3，第二套 CPU staging 不会从源码上覆盖 backbuffer。

   窗口创建方式确有变化：D `transparent_presentation.cpp:119-158` 直接规范 popup/clipchildren 及 layered/transparent/no-redirection；P `:126-136` 只通过 Window Service 改 layered/no-redirection。产品 `IdtMain.cpp:2783-2784,2835-2849` 已预设 WS_POPUP|WS_CLIPCHILDREN、NOACTIVATE|TOOLWINDOW 并去除 Primary transparent，`Window.cpp:1754-1771` 由 owner 写入后核验；已有 Win7 现场读回 Primary layered、未 transparent、size1920×1080。即使 owner/topmost/Windows7 桌面合成存在差异，它首先影响桌面可见，不足以解释正式 Cursor 同帧源读回全零。

3. **产品 CPU pixel telemetry / alpha 底层 — 已降低优先级。**
   Primary 公式两边相同：`sourceAlpha + ((255-sourceAlpha)+127)/255`。产品新增的 `result.allZeroAlpha`、`premultipliedAlphaValid`、可选 source/final stats 全部只读源；underlay写 destination alpha，源与DIB资源分别是 Map staging 与 CreateDIBSection 的地址。源码上没有钳制源 RGB/alpha，也没有 fallback 把 source 替换为零。已有 `Draw3.TransparentPresentation.cpp:1467-1508` 保留四类 source pattern×两种 underlay、采样/非采样输出一致性校验；本研究不重复执行。

4. **产品普通笔/Shape 的 NO_OVERWRITE capability gate — 不解释正式 Cursor 的失败。**
   产品 `Draw3.Renderer.cpp:476-480` 查询 D3D11_OPTIONS；`Draw3.RendererPrimitives.cpp:63-71,176-184` 不支持时 DISCARD/offset0。原 Demo 会在环形非溢出时使用 NO_OVERWRITE，产品增加保守回退。两边 Cursor 本身都 DISCARD，upload/bind 的基本代码一致；后续不出 Cursor 和 Laser 不能由仅普通 Pen/Shape 的门禁解释。

5. **Laser scratch Bake / 新失败门控 / metrics — 低于公共 GPU 分界的优先级。**
   产品 `Draw3.Renderer.cpp:214-256` 增加 Laser scratch 和 commit swap，它们引用 Laser 独立纹理，不替换 `backBufferTexture`。产品 frame `Draw3.DrawingController.cpp:13681-13732` 在 composite 成功后追加 Laser 和 Cursor，最后 Present；本仓 Demo `drawing_controller.cpp:5650-5666`（独立原仓 `:5484-5500`）同样先合成再瞬态 Cursor再Present。产品多出的失败 guard可能有意阻止提交失败帧，但日志已证明正式 Cursor draw-issued紧跟ULW；不能拿 skipped guard解释该实例。Cursor后 metrics 段只有 CPU bookkeeping，没有重清GPU目标；既有研究已核对这一点。

### 5. 能留下 red/green 因果证据的最小分界

此对比没有出现已证明的 red defect，因此不建议当前静态阶段猜测修改 swap effect、blend 或 OS fallback。

- 使用已经交付的六例自检，先把相同设备上的普通 texture 与 swapchain backbuffer 的 Clear/readback 对照起来；这比“全部换掉 FLIP”更准确地判断问题是否依赖 backbuffer 资源。
- 若 ordinary texture clear 成功而 backbuffer clear 失败，在同一 Win7、同device/context、同 readback描述/同步策略下重复即可建立资源种类 red/green；还需区分 runtime/driver 和具体交换链资源 contract，不自动推出永久 BLT fallback。
- 若 Clear 成功而正式 Cursor draw失败，比较已有正式 blend与关闭 blend 的诊断对照；这里不能用 input/history 成功代替 framebuffer 证明。
- 若六例都成功，后续只把诊断对齐当前失败的白色 EraserGrip（GPU shape6）、同 dirty矩形、生产CopySubresourceRegion/blockingMap，比较原Demo与产品actual per-frame状态。已有独立自检是 circle shape4/full CopyResource/unbind/event同步，不能据六例PASS宣称生产已恢复。这一差异已在既有 GPU 报告中记录，不新增大范围测试矩阵。
- 想以旧 Demo 作为绿色对照，必须先固定“用户当年使用的EXE hash + activeMode=ULW + actual driver/FL + Windows/build”，或明确把新构建 Demo 当成新的对照实验。当前原仓现存多个 2025-11/12 命名 EXE以及2026-08的x64产物，文件名和时间均不能证明那个已验证版本。

### Related Specs

- `.trellis/workflow.md`：research落盘、任务保持进行中。
- `.trellis/spec/native-desktop/index.md`、`rendering-and-ui.md`：Draw3 device独立于UI3，Primary和Selection是同一Host的输出目标，不能建立第二runtime/input。
- `.trellis/spec/native-desktop/draw3-integration.md:5,24`：来源快照8d045298；预编译模型库来源边界。
- `.trellis/spec/native-desktop/input-and-ink.md`：source staging、最终DIB和可见呈现证据分层；保留Win7实际验证边界。
- `.trellis/spec/shaders/cpu-gpu-contracts.md:15`：动态SRV NO_OVERWRITE按实际capability门禁，不从FL11或KB泛化。
- Task `prd.md` / `design.md`：明确保留FLIP_SEQUENTIAL、禁用DWM历史模式，不猜测改变窗口或呈现后端。

### External References / Versions

- 仓库身份链接：[Inkeys3-Draw3 source snapshot](https://github.com/Alan-CRL/Inkeys3-Draw3/tree/8d045298eaaac76f752b4f8b5f3303b3520e50b7)。此链接用于标识本地ref，不声称本轮已读取远程。
- DXGI、Windows7 Platform Update、shader/profile/runtime API支持由主session及shader专门研究负责；本文件仅做已读源码对比，不独立重复其官方资料调查。

## Caveats / Not Found

- 不把8d045298当前源码或现存EXE命名等同为用户当年的Win7已验证版本。没有找到当次Win7 driver/mode/EXE hash。
- 原仓 main ref + 当前文件hash只固定本次实际读取内容，不是完整git-clean证明；研究角色不运行git操作，不reset/fetch/checkout，也不读implement.jsonl/check.jsonl。
- native research注入给出Active task和角色；未提供research manifest。按research角色隔离直接读workflow、PRD、design、implement及相关spec，未加载其他角色manifest。
- 没有在Win7或本机运行本研究对照，没有执行build、GUI、隐藏HWND或GPU脚本；全部结果是静态读源和既有日志。
- 本文不独立复核当次ULWpixel logs或shader数学；精确现场计数和同帧source零证据采用已完成的gpu-pipeline-narrow与capture报告。
- 测试identity缺口会影响“为什么旧Demo绿色/产品红色”的推论，但不影响已观测“正式光标上传和绑定后，USER32前source全零”的定位边界。
