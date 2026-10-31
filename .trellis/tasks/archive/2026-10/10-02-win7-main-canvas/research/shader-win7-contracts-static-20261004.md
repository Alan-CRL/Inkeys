# Research: Win7 shader 字节码与 D3D11 资源合同静态调查

- Query: 在 RTS 与 CPU 墨迹链已进入、Cursor Map/绑定正常但 ULW staging 全透明的条件下，核查 compiled shader、共享 buffer、dual-source blend 和 Win7 WARP FL11_0 的运行合同，寻找可证明的兼容缺陷。
- Scope: mixed（生产源码、只读原 Demo/CSO/现场 EXE、Microsoft 官方文档；不执行产品、不编译、不改源码）
- Date: 2026-10-04

## Findings

### 结论与证据等级

本分工没有找到能够静态证明本次唯一根因的 shader/profile/资源绑定违规。新增的强证据是：**现场 EXE 与当前候选嵌入的四个 shader，分别与当前对应 CSO 全字节相同；产品四个 shader 与当前仓库 Demo 的执行字节码和输入/输出签名逐字节相同。** 因此“集成时误用 shader 文件、升级到 SM5.1、混入 Win8 新 opcode 或常量布局漂移”没有当前证据支持。

仍然不能把“Map 成功、Draw 已发出、CPU document/history 写入成功”写成“GPU backbuffer 已生成有效像素”。本次异常位于 Draw 执行/资源数据/混合写入到 staging 读回的区间；直到已知颜色 Clear 与正式 Cursor 对照补齐，不能先将问题全部归到 USER32/ULW 调用之后。本报告复用 `gpu-pipeline-narrow-20261004.md` 的精确同帧、几何数学和复制偏移结论，不重新计数输入。

| 等级 | 结论 |
| --- | --- |
| 已证明 | 现场/候选嵌入的 301–304 shader 与当前四个 CSO 逐字节相同；全部 SM5.0；产品与当前仓库 Demo 的 SHEX/ISGN/OSGN/STAT 全相同 |
| 已静态核对 | Cursor b0/t0 CPU 与 compiled reflection 的字段、48/16 字节布局一致；dual-source 单 RTV0、完整 o0/o1 和 ADD 符合基本合同；Cursor/LaserTip 均只使用 WRITE_DISCARD |
| 可疑但未证明 | Win7 老 WARP 对实际 buffer 数据读取、同一正式 Draw 或 blend/写入的运行差异；对象绑定/Map 成功不足以验证 shader 实际读到什么 |
| 仅文档歧义 | MiscFlags 与 CPU_ACCESS 的泛化备注和继承的 dynamic structured 描述存在书面冲突；该描述来自原 Demo，且 Win7 实际创建/Map 成功，不能单凭备注当作本次因果缺陷 |
| 降低优先级 | 缺少 Pointer API；shader 超出 FL11_0/SM5.0；Cursor 依赖动态 SRV NO_OVERWRITE；仅新增 thin-ink 分支解释全部工具消失 |

### Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp`：全局/结构化资源创建、特性探测、混合状态、嵌入 shader 加载。
- `Draw3.Renderer.cppm`：InkPoint、HighlighterPrimitive、LaserDot、常量结构的 CPU 大小与容量。
- `Draw3.RendererPrimitives.cpp`：实际 Cursor 上传、b0/t0 和 OM/IA/VS/PS 设置。
- `Draw3.RendererLaser.cpp`：LaserTip shape 9 使用相同 t0/b0/VS/PS/resolve-blend；矩形 resolve 不读取 t0。
- `Draw3.LaserParticleSystem.cpp`：粒子 CS 及 DEFAULT structured UAV/SRV；失败仅禁用粒子。
- `Draw3.GraphicsInitialization.cpp`：专用 immediate context、BGRA-support 设备和 Hardware/WARP 创建。
- `Assets/ink.hlsli`、`inkVertexShader.hlsl`、`inkPixelShader.hlsl`、四个 `.cso`：真实寄存器/几何/输出以及只读反汇编证据。
- `Inkeys/Inkeys.vcxproj:1309` 与 `Inkeys/Inkeys.rc:358`、`Inkeys/resource.h:75`：SM5.0 FXC 与资源 301–304 的实际嵌入链。
- `inkStrokeModelerTest/{ink.hlsli,inkVertexShader.hlsl,inkPixelShader.hlsl,laserParticle*.hlsl,*.cso}`：当前 repo Demo，只读对照。
- `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/`：真正原仓库 Demo，只读对照。原仓库 ref/renderer 恒等结果由 `win7_demo_ulw_diff` 分工确认，本分工直接比较此处 HLSL/CSO，不做 Git 操作。

### 1. 已测试 EXE 和新候选的嵌入字节身份

只以 `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)` 读取资源，不执行 EXE、不创建 HWND、不加载其程序初始化。读出的 resource type 为 `SHADER`、ID 301/302/303/304。

- 现场旧 EXE：`G:/Inkeys/Inkeys-Win7-Gpu-Probe-x64-Release/Inkeys-Win7-Gpu-Probe-x64-Release/Inkeys.exe`，SHA-256 `06CD58C7E1E1F01042D489902457C18FF3E32DAF843197386AE7BB7B283DF8AE`；与 22:48 capture identity 一致。
- 当前候选：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-224836-followup/x64-Release/Inkeys.exe`，SHA-256 `2D468B91156333C5CBD9E1E3C82D5713961DD94647ECCF4B44285FFC8EDDD72C`。
- 两 EXE 的四个嵌入 shader 均与当前对应 `.cso` **全字节相同**。这将本次静态反汇编与实际故障 EXE 联系起来；它不证明所有 C++ 源码都与故障 EXE 相同。

| ID / shader | bytes | profile | SHA-256（两 EXE 与当前 CSO 相同） |
| --- | ---: | --- | --- |
| 301 / inkPixelShader | 28748 | ps_5_0 | F55DDF08624442C5A3B512897E9D00130E692AA99709A996879ECE837F1A69F8 |
| 302 / inkVertexShader | 12484 | vs_5_0 | 14855FC66150900536BD0F3710F096EAA534B04B1A1089A83D495E155F85ECD5 |
| 303 / laserParticleUpdateCS | 3572 | cs_5_0 | C02E3266B9E69B4214E4297063F96F8407040C465E5354947FB5C417CB88E8CF |
| 304 / laserParticleEmitCS | 5360 | cs_5_0 | 21F855A1B6E387DB787E06B2B90E8C13742D9B94B540DF06E9FF388F52887732 |

### 2. 反汇编与 Demo 对照：hash 不同不等于指令不同

使用已安装 SDK 的 ARM64 `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/arm64/fxc.exe /dumpbin <existing.cso>`；四次退出码均 0，输出只保存在进程内存。本分工未调用 shader 编译、未写入/替换 CSO。

四个产品 DXBC 均只有 **5 个块：RDEF、ISGN、OSGN、SHEX、STAT**，没有 SFI0 扩展功能块。compiler 标识均为 `Microsoft (R) HLSL Shader Compiler 10.1`。profiles 的 SM5.0 是从反汇编与真实 SHEX version 核对，不只是工程项声明。

| 对照 | VS | PS | UpdateCS | EmitCS |
| --- | --- | --- | --- | --- |
| 当前产品 vs 当前 repo Demo | SHEX/ISGN/OSGN/STAT 全同 | SHEX/ISGN/OSGN/STAT 全同 | SHEX/ISGN/OSGN/STAT 全同 | SHEX/ISGN/OSGN/STAT 全同 |
| 当前产品 vs 真正原 Demo | SHEX/ISGN/OSGN/STAT 全同 | OSGN 同；SHEX/ISGN/STAT 有差异 | SHEX/ISGN/OSGN/STAT 全同 | SHEX/ISGN/OSGN/STAT 全同 |

当前 repo Demo 四个 CSO 与产品相比：RDEF 唯一差异为该块 **offset 21 一字节**，产品为 `0x01`，Demo 为 `0x81`；32-bit flags 分别为 `0x100` 和 `0x8100`。除此之外容器 checksum 也随字节改变。差异的 `0x8000` 对应本机 `d3dcompiler.h:154` 的 `D3DCOMPILE_OPTIMIZATION_LEVEL3` 标记；产品 RDEF 没有该位。**实际 SHEX 完全相同，所以不能将此标记直接升级成优化指令回归。** 这里只核对保存的二进制 metadata，不推测当时完整 FXC 命令或重新编译。

Demo 完整 CSO SHA：

- 当前 repo Demo VS：`CA9A83348E43B12FF3AD059EC4F1DB47338720C77C0EF897002AEDEC63214B08`。
- 当前 repo Demo PS：`3D4701811FF9A5F556C408FA4BC9885444697C793A8059EF63C9A92626E1CE85`。
- 真正原 Demo PS（25744 bytes）：`C9714B94F436FF3D0C17918F609E1866CDE480BCBB6EE208D288F2C7A81CEF33`。
- 两套 Demo UpdateCS：`EF3F2373AF2275A4900594A865B4B93B7E1861AF16506D989DCCA374DD56107D`。
- 两套 Demo EmitCS：`46DE79F93D6C181E941CCBC0B406D278AFE83F73839017F4A956DEB3DEC21523`。
- 真正原 Demo VS 与当前 repo Demo VS 同 hash。

原 Demo 的 PS 与当前产品源码实质差异只在新增 `GetInkLocalRadius` 和 `type == 0 && globalOperatorKind == 0 && min(input.r1,input.r2) < 1.0` 的 thin-ink coverage（产品 `inkPixelShader.hlsl:50,504`）；另有 grip 的三个数字常量变为同值宏（`:290`）、注释/空白变化。VS、两个 CS 和 `laserParticleCommon.hlsli` 三方全字节相同；`ink.hlsli` 只有空白变化。当前 repo Demo PS 与产品仅宏化/注释/空白差异，实际指令全部相同。

新增 thin-ink 的条件不适用于 shape 4/5/6 Cursor 或 shape 9 LaserTip。旧 WARP 整体 shader 编译器对更大程序的差异仍可作为运行期假设，但没有官方缺陷证明；不得把新分支静态判成全部工具透明的原因。

### 3. Compiled reflection 与执行所需字段

- VS：`InkData t0` 为 structured、stride 16；`HighlighterData t3` stride 24；`LaserParticleData t8` stride 80。输入仅 SV_VertexID / SV_InstanceID，IA 不需要传统 vertex buffer；`IASetInputLayout(nullptr)` 不构成缺陷。
- VS/PS 的 b0 `ScreenBuffer` 均为 3 个 float4 register：width offset0、height4、shape8、bufferOffset12、color16、operatorKind32、padding36；总 48 bytes。产品 `Renderer.cppm:74,82,386,397` 对应 InkPoint16 / GlobalShaderConstants48，Cursor `RendererPrimitives.cpp:337-372` 写两点 32bytes和 b0 全部字段。
- PS 反汇编签名只输出两个完整 float4：`SV_Target0 -> o0.xyzw` 与 `SV_Target1 -> o1.xyzw`；没有 SV_Depth/SV_Coverage、PS UAV、额外 RTV output、最小精度或 double 声明。VS→PS 的当前 ISGN/OSGN 字段与实际寄存器匹配，未发现缺字段/错绑定。
- CS 都是 `cs_5_0`，`u0` 为 structured stride80，thread group64×1×1；不使用超出 11_0 的 typed UAV load 或跨所有图形 stage 的 UAV。粒子创建门禁为 FL>=11_0（`LaserParticleSystem.cpp:82`），且初始化失败只记录并禁用粒子（`Renderer.cpp:609-615`），不能解释普通 Cursor 一起消失。
- FXC 反汇编的粗略 slots：VS294、PS869、Update72、Emit115。没有新 profile 或扩展要求的证据；数目本身不是 Win7 GPU 执行成功证据。

### 4. 共享资源、Map 与 Win7 支持边界

| 资源 | 创建/使用描述 | 本案判定 |
| --- | --- | --- |
| b0 GlobalShaderConstants | DYNAMIC + CONSTANT_BUFFER + CPU_WRITE，48bytes，MiscFlags0；每次 WRITE_DISCARD | 基础 D3D11 路径；无11.1 constant-buffer offset/partial-update 依赖 |
| t0 InkData | DYNAMIC + SHADER_RESOURCE + CPU_WRITE + BUFFER_STRUCTURED；200000×16=3200000bytes，view UNKNOWN、200000元素 | 正确大小/stride/view；同原 Demo 的 descriptor；泛化 CPU_MISC 文档歧义见下，不定因 |
| t3 HighlighterData | 从 t0 描述复制，200000×24=4800000bytes，stride24；WRITE_DISCARD | 同继承描述；它不是 Cursor 故障必经资源 |
| t8/u0 粒子池 | DEFAULT + SRV/UAV + BUFFER_STRUCTURED，2048×80=163840bytes，CPU flags0 | 不依赖 mappable-default-buffer；CS/粒子失效不应影响普通 Cursor |
| 纹理/RTV | 生产创建 DEFAULT BGRA8/RGBA8/R16_FLOAT，CPU flags0 | GPU input/output 的标准用法；并非 WARP Win7 不支持的共享表面 |

引用的源码：`Renderer.cpp:476-480,488-495,561-582`，`Renderer.cppm:230`，`LaserParticleSystem.cpp:95-143`。本分工扫描 renderer/laser 文件未找到 ClearView、DiscardView、DiscardResource、UpdateSubresource1、CopySubresourceRegion1、SetConstantBuffers1、ID3D11DeviceContext1 或 SHARED/NT-handle 的生产 Draw 必需调用。

普通笔/Shape 先按实际 `CheckFeatureSupport(D3D11_OPTIONS).MapNoOverwriteOnDynamicBufferSRV` 门禁选择，失败按不支持处理；Cursor `RendererPrimitives.cpp:343,355` 与 LaserTip `RendererLaser.cpp:357,363` 均直接 WRITE_DISCARD。官方 [D3D11_MAP](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map) 将 NO_OVERWRITE 的动态 SRV 扩展与 DISCARD 区分；[D3D11_OPTIONS](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options) 要求按实际支持查询。因此不能用“Win7 缺少 NO_OVERWRITE”解释这些实际 Cursor 样本。

Map DISCARD 会使整个资源旧内容无定义，当前 Cursor 只读刚写好的0/1槽、bufferOffset0（VS `inkVertexShader.hlsl:79-80`），并未继续读取其余199998槽。后续新一轮 DISCARD 不应静态解释成覆盖前一 Draw 的 GPU 数据；正常 D3D11 dynamic-resource 管理由运行时完成。[Microsoft dynamic resources](https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources)。具体 Win7 WARP 是否遵守这一执行行为仍需同设备测试。

**CPU/Misc 文档歧义必须如实保留：** [CPU_ACCESS_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_cpu_access_flag) 与 [RESOURCE_MISC_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_resource_misc_flag) 的泛化 Remarks 都说 MiscFlags 不能与 CPU access flags 同用于创建。另一方面 Microsoft [D3D11 功能规范](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm) §5.4.2 列 structured 的 stride/divisibility/bind 限制，没有给出该组合的明确 Win7 禁令；§5.6.1.2 明确 dynamic SRV 在11.1之前允许 DISCARD；§5.6.1.3 甚至在11.2 default-map 说明允许 structured misc 与 CPU flags，表明泛化备注不适合无条件推广到所有描述。后者是11.2，不是本案在Win7使用的理由。

没有找到能直接裁定“Win7 dynamic structured + WRITE_DISCARD 一定非法/一定静默读零”的官方具体说明；也没有找到针对 Win7 的明确例外。结合原 Demo 描述相同、实机 CreateBuffer 和两次 Map 成功，当前只能列为低优先文档/运行期待证项，**不是已定位的生产 defect**。若正式 Cursor 在 offscreen 也失败且 Clear 正常，可独立核查 t0/b0 GPU 可读数据是否等于刚写入值；不改生产 buffer 使用方式作为无证据 fallback。

### 5. Dual-source blend 本身与共享失效解释

`Renderer.cpp:521-531` 设置 RTV0 ONE / SRC1_COLOR、alpha ONE / SRC1_ALPHA、ADD、写掩码ALL；`SetOMTarget` 仅绑定一个 RTV0。compiled PS 输出完整 o0/o1，满足 [Microsoft Output-Merger](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage) 的 dual-source 基本合同。不能因为只有一个 RTV 反而要求添加第二 RTV，也不能把普通双RTV的 stroke MAX/MIN 与 single-RTV resolve ADD 混成同一状态。

LaserTip `RendererLaser.cpp:349-385` 直接读取 t0 并使用与 Cursor 相同的 VS/PS/b0/resolve blend/RTV，**不经过历史、L1/L0 或 document 烘干**。因此 Cursor 与 LaserTip 同时无像素，优先指向共同 GPU Draw / 数据 / blend / 当前目标 / staging 区间；CPU 文档渲染历史的单独缺陷解释不了它们。矩形合成的 shape8/11/12/13 不读 t0，而是 b0 rectangle+SV_VertexID；必要时它们可成为后一层 discriminator，但当前没有它们的独立像素正证据，不应凭调用成功声称 t0 已被排除。

Win7 Platform Update 只提供部分11.1功能、WARP达到11_0且不支持共享表面，见 [Platform Update](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)。当前真实 shader 都是 SM5.0，FL11_0 对应 SM5.0，见 [Direct3D feature levels](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro)。源码使用核心 immediate context（`GraphicsInitialization.cpp:75-77`）和 BGRA支持创建标记（`:97`），没有为了这些 Draw 硬性使用上述缺失11.1能力。不能从“Win7/WARP/旧驱动”单独推出 dual-source 必然不支持。

### 6. 今晚最短决定性证据与静态工作边界

已有 `RunRendererPixelTest` 是同 Win7、同 shader、同 WARP FL11_0 的第一步；本分工不新增代码/包或改变该顺序。

1. **Clear 对照**：普通 offscreen 与 swapchain backbuffer 已知色读回，区分基础纹理/Copy/Map 和 swapchain 资源。
2. **正式 Cursor 与关闭混合对照**：Clear 通过但正式 draw 失败、比较 draw 通过，才把正式混合提升为重点；两 draw 都失败，结合 VS/PS pipeline statistics 再查 SRV/b0/geometry/shader/write。
3. **六例全通过时**：只能证明128×128 Circle形状的独立链正常，再核同实际 EraserGrip shape6、大尺寸、非原点 dirtyCopy、绑定/同步和生产状态；不能宣布1920×1080 ULW可见恢复。
4. **需要 t0/b0 内容判别时**：只在上一步证据需要时设计独立 readback 或输出，不以正确 COM 指针当作正确字节，也不直接永久替换资源/禁用blend。

### Related Specs

已读 `.trellis/workflow.md`、当前任务 `prd.md/design.md/implement.md`、先前 `research/gpu-pipeline-narrow-20261004.md`；相关 specs 为 `.trellis/spec/shaders/{index,cpu-gpu-contracts,build-and-encoding}.md`、`.trellis/spec/native/{index,runtime-and-rendering,platform-and-resources}.md`、`.trellis/spec/native-desktop/{draw3-shaders,input-and-ink}.md`。本分工只读 specs，不替主会话更新契约。

## Caveats / Not Found

- 没有生产源码、spec、项目、CSO、G:数据、候选包或 Git mutation；只新增本文件。FXC仅dumpbin，EXE仅datafile resource读取，不做 build/GUI/产品启动。
- 原 Demo 的当前文件/CSO身份可精确比较，但用户以前成功验证的 Win7 Demo EXE、当时driver/设备、窗口与系统补丁身份尚未给出；不能把当前原仓库CSO自动称为当时运行二进制。
- 无SFI0/SM5.0/合同核对只降低显式新能力依赖的可能性，不证明 Win7 shader 实际执行正常。
- 现场upload行 bindings=0 是未查询；after-draw-issued hr=0 是调用点诊断占位、Draw(void)无执行HRESULT。不能据此推断“GPU 渲染完毕”，也不据此反报资源失败。
- 尚未拿到同 Win7的 Clear/Draw/混合比较与执行计数，本报告没有宣称根因确定或主画布已修复。
