# Research: 原生 DX11 纹理创建失败与销毁崩溃

- Query: 当前设置窗口打开后，ImGui_ImplDX11_DestroyTexture 对空 pTextureView 调用 Release 的直接原因、共享设备恢复链路、品牌入场的可能关联和无窗口回归边界。
- Scope: mixed（本地产品/测试源码、用户截图、最新本地日志、Microsoft 官方 API 文档）。
- Date: 2026-10-11
- Task: .trellis/tasks/10-10-setting-home-design
- Evidence: 以下源码行号记录研究时的实现状态；后续实施可能移动行号。研究代理只写此文件，不修改产品、规范或测试，不编译、不启动 GUI、不执行 Git 操作。research 角色隔离要求禁止读取 implement.jsonl/check.jsonl，已读取 workflow、相关规范、PRD/design/implement 与最新 brand-entry/texture-crash revision。

## Findings

### 直接确认的崩溃链

用户截图中，tex 的 BackendUserData 非空，TexID=0，Status=OK，backend_tex 的 pTexture/pTextureView 均空；异常位于 Inkeys/additional/imgui/imgui_impl_dx11.cpp:350 的 pTextureView->Release()。截图输出先出现 D3D11: Removing Device，不能据此知道创建调用的具体 HRESULT 或移除原因。

源码有足以产生这组状态的确定缺陷：

1. imgui_impl_dx11.cpp:371 值初始化 backend texture，使两个 COM 指针起初为空。
2. :389 调用 CreateTexture2D，不保存/检查 HRESULT；:390 仅用断言判断空指针。设备移除、资源不足和参数无效都属于运行期可失败条件，断言不是错误传播或事务回滚。
3. :399 即使 texture 创建失败也继续调用 CreateShaderResourceView，仍不保存/检查 HRESULT，:400 仅断言。
4. :403-405 无条件发布 TexID、Status_OK、BackendUserData，允许把空 SRV/半成品标为有效。由此可准确解释截图的 TexID=0、非空 backend、空资源。
5. :345-358 的 DestroyTexture 在 backend 非空时无条件 Release 两个资源，空 SRV 首先触发访问冲突。:554-563 的 InvalidateDeviceObjects 遍历 RefCount=1 的动态 atlas texture，使设备重建、字体重建或退出清理都可能进入该崩溃点。

这是已确认的资源生命周期缺陷。初始共享设备为何被移除仍未确认，不能声称艺术字网格造成了 device removal 或 WARP OOM。

### 创建/更新/绘制合同

- ImGui 版本为 1.92.7（imgui.h:32-35），当前 DX11 后端开启 RendererHasTextures（imgui_impl_dx11.cpp:589）。ImTextureData 使用动态 atlas 请求协议，不应把失败的 WantCreate 改为 OK。
- ImTextureData::SetStatus(Destroyed) 会在仍保留 Pixels 且未请求永久销毁时重新变为 WantCreate（imgui.h:3544-3546），所以设备失效时撤销 GPU 引用可以保留 CPU atlas 和可重建状态。
- RenderDrawData 在 :174-177 更新全部非 OK texture 后无条件继续提交。仅把创建失败保留为 WantCreate 仍不够：后续 ImDrawCmd::GetTexID() 会要求已上传且非 Invalid（imgui.h:3934），必须在创建失败时终止本次 draw submission，避免继续读取无效纹理 ID 或提交缺失资源。
- WantUpdates 分支直接读取 backend_tex 并调用 UpdateSubresource（imgui_impl_dx11.cpp:411-418）；应对丢失/部分 backend 状态进行防御，且不能将无法实际更新的 texture 标为 OK。UpdateSubresource 返回 void，设备移除需由 GetDeviceRemovedReason 或调用边界判定。
- 后端 vertex/index buffer 创建和 Map 失败目前只 void return（:190-211），无法让 Setting 区分失败。若 VB Map 成功而 IB Map 失败，现有实现未 Unmap 已映射的 VB（:208-211）；此处属于同一提交失败清理边界，修复应确保已经取得的映射按逆序释放。
- CreateDeviceObjects 的 shader/输入布局/常量 buffer 创建检查返回值，但部分 render-state/sampler 创建 :505/516/530/546/548 未检查；若实施采用统一 last HRESULT/失败事务，必须保证不会把不完整 device objects 当成初始化成功，也不能用有 VS 指针就判断所有设备对象已就绪。
- Init 获取 IDXGI factory 后无条件 AddRef bd->pd3dDevice/context（:599-610），需要失败前不留下可误判成功的 backend 状态；修改限于实际失败恢复链，不升级 ImGui 或扩展无关第三方清理。

### 设置与共享设备恢复

- Setting.cpp:1876-1892 常驻初始化在字体/backend 创建失败时销毁 context 并返回 Retry；未区分 device removal。
- 实际绘制在 :3919-3929 先清 RTV、调用 void RenderDrawData，再只根据 Present HRESULT 返回 DeviceLost/Retry/Continue。纹理创建/更新和 draw upload 的失败无返回渠道，发生在 Present 前的失败可能被延后到失效资源清理时才暴露。
- RenderSettingFrame :4050-4073 是共享 epoch 变化后的重建；先 Shutdown 旧后端，这正是截图中的无条件空 Release 可阻断恢复的位置。失败时回滚部分资源，但统一 Retry，若新设备本身失效而没有返回 DeviceLost，旧 epoch 可能重复重试。
- 字体重建 :4075-4085 同样先 InvalidateDeviceObjects，然后重建 fonts/device objects，失败统一 Retry。
- 初始化/呈现创建 :4047/4102-4105 也仅返回 Retry。只需在当前已有资源创建失败路径，保留实际 HRESULT，并在 GetDeviceRemovedReason 显示设备失效时返回已有 DeviceLost；不要增加第二个恢复线程或自行创建 device。
- 现有真正路由 DeviceLost 的入口是 Present、occlusion probe 和 ResizeSwapChain（:3924/4115/4126）。SessionState::Resolve :186-201 在 epoch 不同且有 resident 时选择 rebuildDeviceResources，CommitEpoch :222-225 只能在完整重建成功后执行。
- RenderPipeline.cpp:252-270 由唯一共享渲染线程创建并发布完整新 epoch。调度器 :759-792 在恢复失败时不回调任何旧 epoch 客户端；:859-860 遇到 DeviceLost 立即停止本批后续客户端。修复应接入该现有协议，不能局部继续向已移除 device 提交。
- Setting.Base.cppm:175-190 的普通图片 texture/SRV 创建已经检查两阶段结果、失败不发布 SRV，说明可以沿用相同事务原则。该路径丢失实际 HRESULT（返回 bool），在失败分支查询 shared device reason 可以判定恢复方向，但诊断须区分具体创建失败值与设备移除原因。

### 最新日志与品牌入场诱因审计

研究只读取主会话定位的最新日志 Build/ARM64/Debug/log/idt1791715266583.log 的相关行，不扫描其他会话日志：

- :25 / 18:41:16.320 聚合记录显示 Settings 首次 visible callback 为 302.366ms，并返回 Continue；同时共享 Bar 正常绘制。该值包括完整客户端回调工作，不能当成单独入场动画耗时。
- :26 / 18:41:16.350 Bar 首次 ULW 提交失败，错误31。
- :27-28 / 18:41:16.607 Bar A8 EndDraw=0x8899000C，GetDC=0x887A0005（DeviceRemoved），共享图形故障先于最终 AV。
- :29 / 18:41:16.704 Bar 切换 epoch 后资源重建失败=0x8007000E（E_OUTOFMEMORY）。这证明当时新资源创建也出现内存不足，不能证明 Settings 字体/入场就是最初压力来源；日志未记录失败 atlas 尺寸、独立内存占用或 GetDeviceRemovedReason 原始原因。

品牌实现本身：

- HomeBrand::Geometry 一次 Build 缓存曲线/轮廓/索引（Setting.HomeBrand.h:52-247），Draw/DrawWriting 复用原网格（:362-394），没有 texture、shader、离屏渲染目标或 device 创建。
- 每个活动 cap 的 reserve 为108 indices/23 vertices，循环实际写入12×9 indices、1+11×2 vertices（:402-424）；数量一致。六组笔画、k 闭合笔画额外一端上限7个 cap，新增顶点上限161。前景覆盖为一个普通矩形，未发现无界 geometry 扩容。
- RenderHomeEntryOverlay 只增加 client content clip、主题矩形和一份原字标（Setting.Pages.cpp:286-299），未新增 GPU 纹理。连续绘制的入场会增加覆盖像素/一次前景批次，不能直接等价于 OOM。
- 标准 shader 是单次 texture sample 乘 vertex color（RenderPipeline/Assets/ImGui/imgui_ps.hlsl:7-11），无循环、离屏 blur 或入口特效 shader。源审计未发现品牌动画独有的 device removal 触发调用。
- 当前 WARP 测试使用单独 WARP FL11.0、真实后端/CSO、生产 RenderHome/导航/前景并读回（setting_home_warp_tests.cpp:84-136），没有真实 Setting coroutine 全量初始化、HWND/swap chain 或所有共享客户端。它不能覆盖本次原生资源失败组合。

字体 atlas 是值得记录而非自动修改的候选：原生 RebuildSettingFonts 注册旧 Main/Strong（各合并两类图标）及独立 Design Main/Strong/Icons，设置 RasterizerDensity=effective scale 和 FontScaleDpi（Setting.cpp:1389-1479）；无窗口设计测试使用5个校准面而非原生完整字体组合（setting_design_tests.cpp:3646-3665）。ImGui 动态 texture 增长会保留上一张到下一帧销毁（imgui_draw.cpp:4026-4058）并受 TexMax 尺寸夹紧（:4147-4176）；缓存多种 baked 字号和短暂双 atlas 可增加内存，但现有证据没有出现超过后端上限的实际 atlas 尺寸。8192×8192 RGBA 单张为256MiB是容量上界的算术示例，不是日志显示的实际申请。不得在没有测量时擅自更改字体密度、文字大小或全局 atlas 配置。

### 推荐最小修复

1. 以局部临时 texture+SRV 执行两阶段创建；任一失败释放本次局部资源，不发布 BackendUserData/TexID/OK，不对可发生的 HRESULT 故障使用致命断言。
2. DestroyTexture 对空/部分成员分别安全 Release，清 ID/user data 并遵循 SetStatus 协议；重复清理幂等。有效对象仍检查原有效性合同。
3. 增加小的后端错误结果边界（例如 per-ImGui-backend last HRESULT，或保持现有 void 入口并提供只读结果）；捕获 texture/create-state/buffer/map 的失败，停止该次 draw submission并释放已取得的临时/映射资源。不要引入异常跨 C ABI 或全局静态跨 context 错误槽。
4. Setting 在初始化/重建/实际提交故障时保存 HRESULT 和 device reason，再选择已有 Retry/DeviceLost。真正设备失效必须进入共享 epoch 恢复；普通资源故障不强制无限切换新 device，也不每帧刷屏日志。
5. 保留已经批准的入场动效与 Show/Hide、0.3s停留和尺寸门禁。本次无需换 shader、把矢量改 PNG 或撤销动画以绕开资源故障。

### 推荐真实后端回归

- 在当前无窗口 WARP harness 加小的 standalone ImTextureData 真实 CreateTexture2D 失败：width=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION+1（16385）、height=1，只有约65KB CPU pixels，却稳定超过FL11.0最大尺寸。验证 HRESULT 为失败、不发布 ID/user data/OK、可重复 WantCreate、WantDestroy/Invalidate/Shutdown 均无空 Release。
- 使用测试专用 device COM proxy 或仅编译到 headless 的故障 seam，精确覆盖 CreateTexture2D=E_OUTOFMEMORY、CreateTexture2D=DXGI_ERROR_DEVICE_REMOVED、texture成功而SRV失败；断言已建 texture 被释放、没有 invalid draw GetTexID/提交、错误结果保存。记录这是精确故障注入，不能称真实 WARP 系统移除。
- 覆盖 device epoch 重建时旧坏 texture 清理、重新 Init/CreateDeviceObjects、正常上传/实际WARP读回恢复，并检查重复清理无残留。若加入 map 故障测试，覆盖VB Map已成功/IB Map失败的解映射。
- 保留原实际WARP/真实字体/透明圆头/并行书写与Hold/Dock读取和既有交互回归。可追加 cold first-show/full-native字体组合与多倍率 atlas 尺寸/vertex/index有限性观察，只有新证据表明资源压力源才调整相关逻辑。
- 不应尝试诱发系统wide TDR、修改注册表/显卡驱动或启动 GUI来实现失败测试。

## Files Found

| Path | Description |
| --- | --- |
| Inkeys/additional/imgui/imgui_impl_dx11.cpp | 直接崩溃、失败后错误发布、device object初始化和draw上传生命周期。 |
| Inkeys/additional/imgui/imgui_impl_dx11.h | 当前公共入口全部为void/无错误诊断渠道（除 Init/CreateDeviceObjects）。 |
| Inkeys/additional/imgui/imgui.h | 1.92.7 dynamic texture状态合同、GetTexID非法ID断言、atlas尺寸字段。 |
| Inkeys/additional/imgui/imgui_draw.cpp | texture增长、旧atlas待销毁、纹理/UV共享数据更新与尺寸夹紧。 |
| Inkeys/Inkeys/UI/Setting/Setting.cpp | native字体组合、常驻初始化/设备和字体重建、Present结果与共享恢复返回。 |
| Inkeys/Inkeys/UI/Setting/Setting.Base.cppm | shared device lease、独立presentation、普通图片texture事务。 |
| Inkeys/Inkeys/UI/Setting/Setting.SessionState.h | show/hide/epoch/字体序号合同和重建决策。 |
| Inkeys/Inkeys/UI/Setting/Setting.HomeBrand.h | 缓存矢量字标和有界活动cap网格，没有新texture/device。 |
| Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp | 入场前景普通矩形+字标绘制。 |
| Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp | 唯一shared device恢复与调度禁止旧epoch提交。 |
| Inkeys/Inkeys/UI/RenderPipeline/Assets/ImGui/imgui_ps.hlsl | 生产单采样ImGui shader。 |
| InkeysHeadlessTests/setting_home_warp_tests.cpp | 无HWND真实backend/CSO/FL11.0离屏提交与读取，可复用失败回归。 |
| InkeysHeadlessTests/setting_design_tests.cpp | CPU真实i18n/font矩阵，字体组合与原生旧字体不完全相同。 |
| Build/ARM64/Debug/log/idt1791715266583.log | 用户此次最新native失败线索；忽略生成物，不提交。 |

## External References

- [ID3D11Device::CreateTexture2D](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d)：官方HRESULT创建边界；研究访问日期2026-10-11。
- [ID3D11Device::GetDeviceRemovedReason](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-getdeviceremovedreason)：可能返回HUNG/REMOVED/RESET/DRIVER_INTERNAL_ERROR/INVALID_CALL或S_OK；保留原创建HRESULT并另记录device reason，不把两者混为一值。
- [处理Direct3D 11设备移除](https://learn.microsoft.com/en-us/windows/uwp/gaming/handling-device-lost-scenarios)：在释放旧device之前读取移除原因帮助区分应用命令、驱动和外部变更；本文只借用基础D3D11恢复诊断原则，不迁入UWP架构。
- [ID3D11Device5](https://learn.microsoft.com/en-us/windows/desktop/api/d3d11_4/nn-d3d11_4-id3d11device5)：只新增fence方法，没有RemoveDevice。本研究早期消息误把D3D12的RemoveDevice建议用于D3D11，已立即通知主会话/实施代理更正；不可采用该无效接口。

## Related Specs

- .trellis/workflow.md：研究持久化、主会话 implement→check流程与最小任务边界。
- .trellis/spec/native-desktop/index.md：Settings借用shared WARP epoch、独占ImGui session/presentation资源。
- .trellis/spec/native-desktop/rendering-and-ui.md:115-151：固定客户端顺序、唯一渲染线程、DeviceLost恢复与失败重试禁止旧epoch提交。
- 同文:1938-2014：真实字体/倍率与CPU测试证据边界。
- 同文:2090-2151：主页入场缓存几何/无新增shader设备和无窗口WARP验收边界。
- .trellis/spec/native-desktop/errors-logging-and-resources.md：按实际HRESULT/生命周期处理、释放和失败日志合同。
- .trellis/spec/native-desktop/cpp-conventions.md：最小变更、同步状态/所有权边界。
- .trellis/spec/native-desktop/build-and-compatibility.md：完整Solution Debug|ARM64、当前原生MSBuild、Win7静态与实机证据区分。
- .trellis/tasks/10-10-setting-home-design/texture-crash-revision.md：本轮允许最小后端修复与明确故障注入边界，优先当前崩溃。

## Caveats / Not Found

- 没有用户现场GetDeviceRemovedReason、失败CreateTexture2D/SRV HRESULT、atlas尺寸、内存快照或完整native调用栈；日志OOM发生在恢复后的Bar资源，不足以归因第一次失效。
- 本研究没有执行产品/测试、编译、GUI或受控故障；上述真实失败和恢复回归为实施建议，不能记录为已通过。
- 无窗口WARP成功只能证明对应单独device/offscreen draw路径；不能替代完整native共享Bar/Settings/swapchain故障、Win7实机或用户现场验证。
- 未发现品牌入场新增texture/device/shader或无界提交源；这不是证明不存在所有运行条件下的设备移除诱因。

## 主会话验证更正（2026-10-11）
首次完整原生构建退出0；首次 headless 退出3，原因是本机实际 FL11.0 WARP 接受16385×1纹理，原提议不能稳定制造 E_INVALIDARG。后续fixture必须使用确定非法描述（例如Width=0），并在意外创建成功时先经真实后端撤销资源再退出失败case，不能直接DestroyPixels/改Status后重用带owner的对象。此失败不证明产品事务修复失败，也不计为测试通过；主会话正在修正并完整重测。
