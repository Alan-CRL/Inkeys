# Research: Win7 GPU 源像素为零时的共享渲染链

- Query: 20261003-172838-167 实机已接受输入，但 ULW 源 alpha/RGB 始终为零；检查普通墨迹、橡皮瞬态光标、激光共有的 GPU 链路是否存在可证明原因。
- Scope: mixed（生产源码主导，Microsoft 文档用于确认 API 合同）
- Date: 2026-10-03

## Findings

### 证据边界

现场输入/像素数由主调查独立统计，本文件不重复完整日志。`console.stdout.txt:16650` 的 Drawpad 脏区 `(948,938,984,974)` 源 alpha/RGB 全零，而最终 DIB 含 alpha=1 命中底层；该证据说明当前可见反馈尚未进入读回内容。必须区别“GPU draw 成功执行”“API void 调用已提交”“Map 返回成功”“呈现调用成功”。后面三项不能证明前一项。

### 文件与关键路径

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp`：共享资源初始化、输出纹理/RTV、双源合成、screen viewport。
- `Draw3.RendererPrimitives.cpp`：普通笔/Shape 上传及橡皮瞬态光标直接绘制。
- `Draw3.RendererLaser.cpp`：激光 coverage、笔尖、粒子和材质解析，预热 viewport 保存恢复。
- `Draw3.Renderer.cppm`：CPU 常量布局和持有的资源对象。
- `Assets/ink.hlsli`、`inkVertexShader.hlsl`、`inkPixelShader.hlsl`：GPU 常量和结构化缓冲协议、几何与材质。
- `Draw3.DrawingController.cpp`：L2→backbuffer、操作层合成、瞬态光标、最终 Present 顺序。
- `Draw3.InkHistoryGpu.cpp`：tile/cache viewport 修改及结束恢复。
- `Draw3.StrokeGeometry.cpp`：历史 stroke raster 会显式设置目标 viewport。
- `Draw3.TransparentPresentation.cpp`：主 presenter 接收 renderer backbuffer；ULW 与 GPU Present1 分支分离。

### 已检查并降低优先级的假设

1. **backbuffer texture 与 RTV 指向不同资源**：`Renderer.cpp:311-314` 从 `swapChain->GetBuffer(0)` 取 `backBufferTexture`，立即从同一对象创建 `backBufferRTV`。`DrawingController.cpp:7039` 将 L2 脏区拷入该 texture，`:7042` 将图层合成至该 RTV，`:7056` 将该 texture 传 presenter。源码不存在独立 cursor/ink 输出纹理选择；正常 resize 会释放两者并同步重取。仍可用 RTV GetResource/IUnknown 身份在现场核验，静态逻辑没有发现别名错误。
2. **每帧 flip 后仍读旧 buffer**：ULW 分支 `TransparentPresentation.cpp:1251` 直接调用 primaryUlwPresenter.Present；`PresentSwapChain/Present1` 在独立 GPU 分支 `:1258`。因此当前 ULW 逐帧没有 swapchain Present，不能仅凭 FLIP_SEQUENTIAL 就断言每帧 buffer 轮转导致旧别名。保留用户明确要求的 swapchain 模式。
3. **常量布局/structured stride 不匹配**：`Renderer.cppm:386-398` 的 GlobalShaderConstants 为48字节，对应 `ink.hlsli:6-17` 三个16字节寄存器；InkPoint 为16字节并有 static_assert，buffer stride 使用 sizeof。Cursor/合成显式写 width/height/shape/offset，VS/PS b0均绑定；没有发现生产 CPU/HLSL 字段错位。
4. **viewport 永远未设置或预热未恢复**：`Renderer.cpp:347` 初始化screen尺寸；`:129-134` 同时维护CPU尺寸与RS viewport。Shape预热 `RendererPrimitives.cpp:227-242`、Laser预热 `RendererLaser.cpp:445-484` 都保存/恢复 viewport。History结束 `InkHistoryGpu.cpp:612-621` 恢复screen尺寸。静态检查不支持“必然留下零viewport”的结论；现场RSGetViewports仍值得核验。
5. **资源/Shader必需初始化失败后仍继续**：`Renderer.cpp:492,574,581,603-604` 检查CB、SRV和VS/PS创建失败并返回false；blend/raster/depth创建也有失败返回。当前能够完成renderer启动说明这些创建没有沿显式失败路径退出。创建成功不能保证Win7实际shader执行结果。
6. **动态SRV NO_OVERWRITE不支持造成所有工具失败**：`Renderer.cpp:476` 按实际设备查询能力；普通笔/Shape条件回退DISCARD。Cursor `RendererPrimitives.cpp:258-260` 固定DISCARD，L1/L0合成 `Renderer.cpp:82,87` 也固定DISCARD。因此该可选能力不是解释三类反馈共同全零的充分原因。
7. **只有历史cache/ClearView失效**：橡皮光标 `RendererPrimitives.cpp:245-296` 直接写backbuffer，绕过历史cache、L1/L0权威重放。历史专项无法独立解释实时光标全零。

### 更有价值的共同检查点

普通墨迹最终合成、Cursor、Laser笔尖/材质最终输出都使用共享VS/PS、b0常量、rasterState、depth-disabled状态和operatorResolveBlendState。普通stroke先写MRT Add/Retain，但Cursor无需成功的MRT内容即可自行写出像素。

- `DrawTransientDrawingCursor` 固定写两个有效InkPoint，上传有效appearance并设置backbufferRTV，绑定VS/PS/t0/b0和dual-source blend，最终 `Draw(6,0)`；这可作为最短生产shader基准。
- Cursor入口和两个Map失败直接return，无HRESULT日志，返回void（`:247,258,264`）。Controller仍可能记录visual数量且ULW成功。普通笔或合成失败会触发Controller `:13738` 后的 `[Draw3.Raster] submission failed`，而Cursor的局部失败没有这一覆盖。
- 所有Draw调用返回void；目前无管线统计或D3D debug message证明vertex/pixel invocation。无Raster失败日志只说明现有可检测分支未失败。
- 需要现场核验RSGetViewports、CPU viewportWidth/Height、OMGetRenderTargets的资源身份、当前shader/CB/SRV以及GetDeviceRemovedReason。任何字段异常都比凭OS名称改backend更直接。

### 下一步最小判别（建议，尚未实现）

在同一Win7设备上执行有界 GPU 像素自检，而非只做CPU dirty-copy或buffer Map：

1. 已知premultiplied颜色ClearRenderTargetView → staging readback，分别比较backbuffer及普通offscreen BGRA纹理，判别实际texture读写能力/交换链资源异常。
2. 已知中心坐标、白色filled cursor，使用正式shader/资源和当前viewport →读回非零alpha；再比较显式已知viewport的独立测试，判别状态残留。
3. 正式shader cursor 在dual-source混合和关闭blend下分别测试；关闭blend仍须完整write mask，且保留原生产blend策略。差异可隔离Win7共享blend执行，不能未测试即替换生产混合。
4. 普通stroke→L0 Add/Retain读回→最终合成读回，区分几何draw与dual-source合成；必要时以pipeline statistics区分没有VS/PS invocation与执行后零输出。

独立测试应复用已有无窗口Draw3验证入口并使用隔离资源；若在产品诊断中执行，应完整恢复所改状态，不能向生产画布注入可见标记或改首帧行为。上述尚没有Win7结果，不能将某一个建议称为已定位根因。

### 外部参考

- Microsoft Output-Merger Stage：https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage 。Dual-source允许o0/o1同时作为slot0混合源，因此PS声明SV_Target0/SV_Target1并仅绑定一个RTV是有效模式；不能凭两个outputs就判定缺少第二RTV。
- Microsoft D3D11.1 features：https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-1-features 。Win7 Platform Update只有部分11.1；这要求具体按能力验证，不支持将所有shader失败归于缺少Pointer API。
- Microsoft WARP guide：https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp 。未找到官方材料足以证明本案Win7 WARP的dual-source/BGRA必然静默失败。

### Related Specs

已读 `.trellis/workflow.md`、任务prd/design/implement、`.trellis/spec/native-desktop/draw3-integration.md`、`cpp-conventions.md`、`errors-logging-and-resources.md`、`.trellis/spec/shaders/index.md`、`cpu-gpu-contracts.md`。保持独立D3D设备、生产输入所有权、premultiplied alpha、shader布局和FLIP_SEQUENTIAL；未改源码或shader。

## Caveats / Not Found

- 没有找到能静态证明“每一帧所有GPU输出均为零”的唯一错误。当前结论是较精确的共同故障范围，不是根因宣告。
- 像素采样只覆盖对应dirty，不等价全纹理全时段均无像素；主调查负责检验采样与visual时序。本文件没有复写该分析。
- 不能以Win11 WARP通过替代Win7 SP1+仅KB2670838；没有运行GUI、构建、git mutation或新GPU测试。
