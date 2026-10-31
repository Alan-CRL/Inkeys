# Research: Win7 22:48 反馈的 GPU 管线与读回分界

- Query: 正常应用已成功上传、绑定并发出 Cursor Draw，但随后 ULW staging 仍为全零；沿生产共享管线寻找可证明缺陷和下一步最短分界。
- Scope: mixed（生产源码、实机日志、Microsoft API 合同）
- Date: 2026-10-04

## Findings

### 结论与证据身份

当前证据把本次橡皮光标故障压缩到“有效 CPU visual / 上传 / Draw 发出 → GPU 实际执行或写入 → Copy / staging 读回”的区间。没有发现足以静态证明唯一根因的生产代码错误。不能将 Draw 的 void 返回、成功 Map 或正确绑定当作实际 shader invocation / 非零输出的证明。

原始采集目录：`G:/Inkeys/Inkeys-Win7-Gpu-Probe-x64-Release/Inkeys-Win7-Gpu-Probe-x64-Release/20261003-224836-752/`。`identity.txt` 记录 EXE SHA-256 `06CD58C7E1E1F01042D489902457C18FF3E32DAF843197386AE7BB7B283DF8AE`、Windows 6.1.7601 SP1、PowerShell 2.0、正常应用退出码 0；`pixel-test-error=无法使用指定的命名参数解析参数集。`。独立 Clear / Draw / blend 自检未启动，不能从没有 pixel-test 文件推断自检通过或失败。采集器修补由另一执行分工负责。

**计数校正：15 条 cursor-gpu 记录实际是 5 次采样，每次三个阶段；不是 15 次独立采样。** 5 次 `upload-success`、5 次 `before-draw`、5 次 `after-draw-issued`。全部 before-draw 的 targetMatch、VS/PS/b0/t0 比对通过，设备 removedReason 为 0。全日志有 39 条 ULW 像素采样。

### 同帧正反证据：不是采样了一块合法空白

`console.stdout.txt:14611-14616`（这里为日志行号）有特别紧凑的一组：

- tick `56460693`，`upload-success hr=0 removed=0`，所有必需资源非空；CPU 尺寸 `1920×1080`、光标中心 `(819,955)`、尺寸 `32×32`、opacity `0.5`。
- `:14612` before-draw 和 `:14613` after-draw-issued：viewport `(0,0,1920,1080,0,1)`；RTV resource 与 backBuffer canonical identity 一致；VS、PS、双方 b0 和 VS t0 均匹配 renderer 实际对象。
- `:14614` 同 tick 紧随其后的 Drawpad ULW dirty `(801,937,837,973)`，包含光标中心及完整 36×36 包围框；1296 源像素的 alpha、RGB、四通道最大值全零。API success；最终 DIB 仅 alpha=1 的 CPU 命中底层。
- `:14616` 正式 visual 为 shape=2（EraserGripCircle）、fill `1.0`、白色 RGB、outline `1.3`。这不是 opacity=0 / 不填充的可合法透明光标。

另一组 `:14898-14901` tick `56461707`，中心 `(1045,811)`，18.5×18.5 光标，viewport / 目标 / shader 绑定同样匹配，随后的 dirty `(1033,799,1057,823)` 覆盖中心，576 源像素仍全零。其余三次采样没有强行用不同 tick 记录宣称精确同帧；本报告只用上述两组建立最强闭环。

### Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp`：Cursor upload、实际 OM/IA/VS/PS 状态绑定、Draw 及稀疏诊断。
- `Draw3.Renderer.cpp`：共享 renderer 资源、blend/raster/depth 状态、backbuffer 获取、合成、尺寸和资源复制。
- `Draw3.Renderer.cppm`：InkPoint 和 GlobalShaderConstants 的 CPU 布局。
- `Assets/ink.hlsli`：HLSL b0 / t0 布局和 VS→PS 数据。
- `Assets/inkVertexShader.hlsl`：shape 4/5/6 的真实几何、screen 到 clip space 映射。
- `Assets/inkPixelShader.hlsl`：Cursor coverage、白色预乘 Add 和 Retain 输出。
- `Draw3.DrawingController.cpp`：L2→backbuffer、图层/激光合成、瞬态 Cursor、最终 Present 的顺序。
- `Draw3.TransparentPresentation.cpp`：生产 dirty CopySubresourceRegion、staging Map、原始像素统计和最终 ULW 调用。
- `Draw3.GraphicsInitialization.cpp`：独立 D3D11 immediate context 与 hardware / WARP 初始化。
- `Draw3.HiddenWindowTest.cpp`：已有独立六例 GPU 像素自检和有界 pipeline statistics。

### Code Patterns 与已降低优先级的假设

1. **绘制后再清屏覆盖**：`DrawingController.cpp:13681` 先完成 L2/操作层合成，`:13687-13714` 完成激光叠加，`:13716` 才绘制当前 Cursor；`:13717-13729` 是 CPU metrics 冻结，`:13731` 调用 PresentFrame；该函数 `:7056-7057` 立即调用 presenter。ULW 分支 `TransparentPresentation.cpp:1251-1252` 直接传 renderer.backBufferTexture，随后 `:517-520` Copy / Map。这条同帧路径没有第二次 Clear、L2 覆盖或 swapchain Present。不能用“下一帧会先重建背景”解释同 tick 的首次读回全零。
2. **错误 dirty 偏移或 staging RowPitch**：生产 source box 为 dirty，目标 offset 也是 dirty.left/top（`TransparentPresentation.cpp:513-518`）；CPU 起始地址同样加 dirty.top×RowPitch + dirty.left×4（`:198-208`）。源和目标坐标一致，front/back=0/1。源采样位于命中底层合成前（`:212-225`），不把 alpha=1 底层计为真实 GPU 输出。没有发现双重偏移、错误 stride 或采样最终 DIB 冒充源的错误；两组实例都覆盖真实光标中心。
3. **输出资源别名**：`Renderer.cpp:311-314` 从 GetBuffer(0) 创建同资源 RTV；现场 `before-draw` 的 canonical identity 已实证匹配。ULW Present 没有调用 Present1，所以不存在该分支逐帧 flip 轮转的源码正证据。
4. **上传失败 / 常量错位 / 无效 geometry**：`RendererPrimitives.cpp:337-351` 上传两个 InkPoint，分别保存中心/半尺寸与 outline/fill；`:361-372` 完整写 b0，包括 width、height、shape、offset=0、white/opacity、Draw operator、outline RGB。InkPoint 16 字节（`Renderer.cppm:74-82`），b0 48 字节（`:386-398`），与 `ink.hlsli:6-17,52-57` 对应。现场两次 Map 成功，viewports 正确。Map 成功仍未独立核验 shader 实际读取的 buffer 字节。
5. **shape 6 天生不绘制**：appearance.shape=2 按 `RendererPrimitives.cpp:335-336` 映射 GPU shape=6。VS `inkVertexShader.hlsl:77-94` 对 4/5/6 都从 t0 获取两点，生成中心±半尺寸+2px 的 quad，z=0,w=1。PS `inkPixelShader.hlsl:269-308` 的 shape 6 先执行同样的 circle fill，再叠 grip stripes；白色 fill=1、opacity=.5 的中心应产生非零预乘 alpha，stripe 分支不能把有效填充减为零。这里是数学和数据合同核对，不是 shader execution 证明。
6. **scissor / cull / depth 阻断**：Cursor 每次都 `SetOMTarget`（`RendererPrimitives.cpp:376`），该入口绑定空 DSV 并设置 depth-disabled dsState（`Renderer.cpp:152-157,545-546`）；Cursor 还显式设置 rasterState（`RendererPrimitives.cpp:388`），它为 fill solid、cull none、depth clip true、scissor false（`Renderer.cpp:548-553`）。shape 的 clip z=0 位于 viewport depth 范围。产品 Draw3 源码未发现 GS/HS/DS setter 或 predication 设置；独立 immediate context 不受其它 UI renderer 状态污染。当前日志没有直接记录 raster/depth/blend 描述，因此这里保持静态结论。
7. **动态 SRV NO_OVERWRITE 可选能力**：Cursor 使用 WRITE_DISCARD（`RendererPrimitives.cpp:343-344`）而非 NO_OVERWRITE；普通笔按 `Renderer.cpp:476-480` 实际 CheckFeatureSupport 回退。缺少 Win8 的可选 NO_OVERWRITE 不足以解释这个 Cursor 实例。
8. **dual-source 使用错误**：`Renderer.cpp:521-531` 只为 RTV0 设置 ONE/SRC1_COLOR 和 ONE/SRC1_ALPHA，写掩码 ALL，独立混合默认 false；PS `inkPixelShader.hlsl:150-154,305-308` 输出 o0=Add、o1=Retain。正式模式仅绑定一个 RTV 是 dual-source 的正确合同；没有第二 RTV 不是缺陷。不能仅因 Win7 Platform Update 部分支持 11.1 就认定 11.0 的 dual-source 不支持。

### 已有自检的最短判别矩阵

`RunRendererPixelTest` 已存在，不需要先增加新的生产变化才能取得下一步证据：

| case / 结果 | 能支持的下一步结论 | 不能直接推导 |
| --- | --- | --- |
| offscreen.clear 失败 | texture clear / copy / Map 基础链异常，优先看 HRESULT 与读取色值 | 某个 shader 或 RTS 问题 |
| offscreen.clear 成功，而 backbuffer.clear 失败 | 交换链 backbuffer 与普通纹理的差异已复现 | 立即换 swap effect 或宣称 Win7 FLIP 全不支持 |
| 两类 Clear 成功，production cursor 失败，而同目标 blend_disabled 成功 | 正式混合相关执行差异；保持 shader/目标相同继续精确验证 | 已获授权永久关闭混合，或替换后其它笔迹必然正确 |
| Clear 成功，production / disabled Draw 同样失败；VS或PS invocation 0 | 可借 IA / VS / PS stats 缩小到无执行、无光栅化或 shader/数据问题 | stats 不支持/失败时也断言 invocation=0 |
| Clear 成功，VS/PS invocation 有值，Draw 像素仍失败 | shader discard / 输出 / 混合 / 写入等较后区间 | PS invocation 等于非零颜色输出 |
| 两目标正式 Draw 与两个比较全部成功 | 独立128×128 Circle基准正常，转向运行期状态、尺寸、shape或 dirty 读回差异 | 整个生产 ULW、所有 shape、1920×1080或桌面可见性已正常 |

该测试总共六例，但末尾 `production_cases=4 result=PASS` 只验收两个 Clear 和两个正式 Cursor。两个 blend_disabled 比较 case 的 FAIL 不计入 productionFailures（`HiddenWindowTest.cpp:2368,2372,2397,2425-2427`），分析必须逐 case 读取，不能只看尾行 PASS。

如果现有六例全部通过，才有理由做下一层独立对照：

- 生产故障同款 shape=6 EraserGripCircle；自检默认 Circle shape=4（`HiddenWindowTest.cpp:2358-2364`）。先保持白填充/尺寸，减少 shader 分支差异。
- 复制小 dirty 到相同非原点 offset 的 staging，复用生产 CopySubresourceRegion；自检当前为 OM解绑后全量 CopyResource（`:2277-2278`）。
- 分别比较保持 RTV 绑定 / 解绑、生产阻塞 Map / 显式 Flush+EVENT 的同步形态。自检当前含显式 Flush、3秒 EVENT 等待和 DO_NOT_WAIT Map（`:2279-2298`），生产为普通 Map(flags=0)。这些只是实验变量；官方 Copy 合同没有要求源纹理复制前必须解绑 RTV，不能预先把生产调用称为非法。
- 如 VS/PS 计数异常，优先独立读回实际上传的 t0/b0 数据并核 shader 输出，避免只按对象指针推断其内容有效。仅在上述缺失证据补齐后设计此测试，不改当前生产 shader 或 backend。

### External References（2026-10-04 核验）

- [Microsoft Output-Merger Stage](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage)：dual-source 用 o0/o1 对单个 slot0 进行混合；空 DSV 时 depth/stencil 测试通过。本案 ONE/SRC1、ADD 和两个PS输出满足这一基本合同。
- [Microsoft ID3D11DeviceContext::CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)：要求资源类型/格式兼容、非当前 mapped、box 与 offsets 在界内；2D front/back=0/1；Copy 异步。这里未给出必须先解绑源 RTV 的要求。
- [Microsoft ID3D11DeviceContext::Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)：动态 SRV 的 NO_OVERWRITE 按设备能力核验，不能与通用 WRITE_DISCARD 混淆。DO_NOT_WAIT 返回 still-drawing 的语义也与生产 flags=0 不同。
- [Microsoft Platform Update for Windows 7](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)：11.1/DXGI1.2 是部分支持，Win7 通常最高FL11.0，需要按具体能力/失败返回处理；这不能证明本案某个创建成功的11.0 draw必然失败。
- [Microsoft Direct3D 11.1 Features](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-1-features)：WARP扩展与可选功能范围资料。未找到足以证明本案 Win7 WARP dual-source / BGRA 静默全零的官方结论。

### Related Specs

已读 `.trellis/workflow.md`、本任务 `prd.md/design.md/implement.md`、`.trellis/spec/native/{index,runtime-and-rendering,platform-and-resources}.md`、`.trellis/spec/shaders/{index,cpu-gpu-contracts}.md`、`.trellis/spec/native-desktop/{draw3-integration,cpp-conventions,errors-logging-and-resources}.md` 和 `input-and-ink.md` 的 Win7 GPU 分界诊断合同。保留独立 renderer、既有 premultiplied Add/Retain、普通 FLIP_SEQUENTIAL、设备能力探测以及 Win7 实测与本机验证分开记录的合同。

## Caveats / Not Found

- 原始 G: 文件只读；本分工只写当前 research 文件，没有源码、spec、构建、GUI 或 Git mutation。
- 未找到可静态证明的生产 GPU 唯一缺陷；不能把“当前 staging 源为全零”提升为“GPU 原纹理必定没有写入”。
- 本报告聚焦共享 GPU 和精确同帧复制，不重新分析窗口/输入时序。RTS / Mouse 总数由输入证据分工另行记录。
- 现有六例 Win7 自检因采集器参数冲突缺失，优先补齐；在没有结果时不能宣布最终修复，也不应继续猜测改shader、blend、输入或交换模式。
