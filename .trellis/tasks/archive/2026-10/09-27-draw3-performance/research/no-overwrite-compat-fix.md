# Draw3 动态 SRV 映射兼容性（F-020）

## 证据与适用范围

- 生产 `InkRenderer::DrawStroke` 与 `DrawShapePrimitives` 将点和形状上传到同一 `D3D11_USAGE_DYNAMIC | D3D11_BIND_SHADER_RESOURCE` 结构化缓冲区，并在环形空间足够时直接调用 `Map(..., D3D11_MAP_WRITE_NO_OVERWRITE)`。映射失败返回 `-1`，该批不提交 Draw；状态与几何层不能把这种失败当作输入样本已经可见。
- `DrawHighlighterPrimitives` 和现有 Laser 上传使用 `WRITE_DISCARD`，不依赖此可选扩展。
- Microsoft 的 [D3D11_FEATURE_DATA_D3D11_OPTIONS](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options) 明确要求以 `MapNoOverwriteOnDynamicBufferSRV` 判断动态 SRV 的该映射模式；不支持时运行时会使 `Map` 失败。[Win7 Platform Update](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7) 说明该系统上许多可选能力不可用，不能从 FL11.0、Hardware/WARP 或当前 Win11 开发机推断该位为真。
- Win7 SP1 + **仅** KB2670838、`FLIP_SEQUENTIAL` 可用是用户实测的项目约束；本修复不改 FLIP、DComp/ULW 顺序、两个禁用的 DWM 透明模式或最低系统要求。

## 设计与测试

- 每次 `InkRenderer::Init` 针对本次 device 查询一次 `D3D11_FEATURE_D3D11_OPTIONS`；查询失败和返回 FALSE 都走保守路径。释放 renderer 时清除缓存位，避免设备重建沿用旧能力。
- TRUE 时保留原环形 `NO_OVERWRITE`；FALSE 时每个普通笔/形状批次采用 `WRITE_DISCARD` 并重置 `m_bufferHead=0`。仍上传所有点与 primitive，保持相邻笔段的共享端点、常量缓冲 offset、顶点数、HLSL 和混合逻辑不变。
- `RunRendererMapCompatibilityTest()` 创建 FL11.0 WARP device，**不创建 HWND**，直接对生产 `InkRenderer::DrawStroke/DrawShapePrimitives` 调用真实 D3D context。强制 capability=false，重复上传后检查 head 回零和 staging readback 中第 0 槽确实是最新输入；若设备报告支持，再核对原环形路径保留。该测试验证 Map/上传合同，不等于像素 Present、真机笔感或 Win7 运行验证。

## 当前状态

- 红测：只加入测试与默认 false capability，保留旧 `NO_OVERWRITE` 实现。主任务串行完整 `InkeysRepo.sln Debug|ARM64` 构建 exit 0；真实进程 `Inkeys.exe --draw3-renderer-map-test` exit 1，第二笔/Shape 均未重置偏移，staging 回读不是最新第 0 槽。原始日志：`TestResults/release-hardening/draw3-map-red-debug-arm64.*`。这证明测试能抓住旧实现，不代表当前 Win11 WARP 缺少扩展。
- 绿测：修补后完整 `InkeysRepo.sln Debug|ARM64` 与 `Release|ARM64` 构建各 exit 0；同一真实进程 `Inkeys.exe --draw3-renderer-map-test` 在 Debug、Release 各 exit 0，stderr 为 `PASS: no-window WARP dynamic-SRV map compatibility`。原始日志：`TestResults/release-hardening/draw3-map-green-build-{debug,release}-arm64.log`、`draw3-map-green-{debug,release}-arm64.{stdout,stderr}.log`。独立 diff/调用链审查由父任务安排，结果待补。
- 修改符号：`InkRenderer::Init` 对本次 device 查询；`ReleaseResources` 清能力位；`DrawStroke`、`DrawShapePrimitives` 按能力选择 map；`RunRendererMapCompatibilityTest` 在无 HWND WARP 上验证生产 map/upload。未修改输入队列、样本、HLSL、FLIP 或 presenter。`git diff --check` 通过；原文件 BOM/CRLF 保持。
- 无窗口测试手工建立与产品相同描述的动态 buffer、SRV 和常量 buffer，直接调用真实生产 Draw 方法；它没有创建 swap chain，也未执行完整 `InkRenderer::Init`、像素栅格化和 Present。当前 WARP 可能支持该选项；测试中的不支持路径是显式强制。支持路径仅在设备实际报告 TRUE 时运行，日志未单独导出该分支是否执行。生产 Init 的设备能力缓存仍需独立代码审查与目标设备验证。
- 性能收益不能从此兼容修复推断；不支持扩展的设备上每批 DISCARD 可能增加分配或同步成本，需真机测量。
- Win7 FL11.0 Hardware、缺少 FL11.0 的 WARP、DComp/ULW 及 `FLIP_SEQUENTIAL` 成功 Present 尚需目标系统实测。本机 Win11 ARM64 的 WARP 无窗口结果只能覆盖当前运行时。
