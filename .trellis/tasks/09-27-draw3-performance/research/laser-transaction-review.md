# F-025 Laser 事务候选独立复审

审查基线：H0 `8b156fca59f0337a6afc6d722941666fcf143080` 对当前工作树；只复核 `Draw3.DrawingController.cpp/.cppm`、`Draw3.Renderer.cpp/.cppm`、`Draw3.RendererLaser.cpp`、`Draw3.HiddenWindowTest.cpp/.h` 中的 F-025 路径。F-023 的普通笔栅格修补与 F-026 的故障退出保存顺序另有 owner。此复审只读源码和现有日志；没有自行构建、运行 CLI、启动 GUI 或采样性能。

## 结论与分级

| 等级 | 结论 | 证据与最小后续动作 |
| --- | --- | --- |
| 阻断性新问题 | 本轮未确认 | 第二层 Map 失败后，生产 bake helper 不交换 scratch；已提交 `laserCompositedColor` 和 `compositedBounds` 保持原值，按 Down 顺序保留两层 `completedPoints`。恢复可写 buffer 后从已提交颜色重新复制到 scratch，再完整烘干，避免重复 source-over。见 `Draw3.DrawingController.cpp:860-952`、`Draw3.Renderer.cpp:214-256`。 |
| P2，既有未闭环视觉风险 | 成功烘干后的 device lost 仍可使 Hold/Fade 激光轨迹提前消失 | 成功 `CommitLaserBake` 后清除 CPU layers（`Draw3.DrawingController.cpp:944-951`）；恢复路径清空 GPU coverage，只从剩余 layers 重建（`:6570-6600`）。已成功烘干的层此时没有 CPU 源。此问题不影响 UInk/持久笔迹，不属于失败批次重试回归。若要承诺整个 Hold/Fade 可恢复，需先制定受界 CPU 保留策略，再做 device-lost 回归；本次不建议临时无限保留。 |
| P3，表述/测试精度 | “失败后 CPU layers 严格不变”只对权威点列、ID、顺序成立 | 完整 bake 在每层前会重新写 `layer.bounds`（`:924-925`），增量失败可更新 `incrementalState`，同时 `coverageMode` 改为 FullRedraw、`bakeDirty` 并入范围。失败不清 `layers`，测试核两层 ID 和点数量，但未逐字段比对点、样式及 bounds（`:1455-1476`）。这不会破坏从 `completedPoints` 全量重绘；报告应使用“权威几何保留”。若要求字节级不变，最小改法是把烘干用 bounds 放局部变量，并增加字段比较测试。 |

## 事务和失败链

1. `FinalizeLaserStrokeLayer` 在 Up 将真实点复制到 `completedPoints` 并解除 runtime 指针（`Draw3.DrawingController.cpp:839-858`）；失败后 runtime 即使在同帧回收，CPU layer 仍可重试。`BakeLaserStrokeLayers` 先 `BeginLaserBake`，后者从已提交 t6 复制到独立 RGBA8 scratch；完整路径逐层检查 `ClearLaserCoverageRect`、`DrawLaserCoverage`、`ResolveLaserStrokeCoverage`，全部成功才 `CommitLaserBake` 交换纹理并清 CPU layers（`:874-951`）。第二层失败只留下未发布 scratch 的部分绘制。
2. 增量分支的 coverage/resolve 失败切 FullRedraw、清增量 coverage，并再次 `BeginLaserBake` 从已提交 t6 复位 scratch（`:879-920`）；完整重绘失败继续返回 false，不会把降级误当成功。`DrawLaserStrokeLayers` 在逐帧完整重绘中也检查 clear/upload/resolve；`RendererLaser.cpp:279-309` 的三个 bool 接口把资源/Map/目标失败传给调用者。零面积的 `DrawLaserRectPass` 返回 true，调用者跳过空 layer/交集（`:214-227`、Controller `:924,985`）；预热器的忽略返回是非产品提交路径（`RendererLaser.cpp:468-479`）。
3. 最后 Up 的 bake 结果进入 `rasterSubmissionFailed`，只有成功才清 `laserLiveBounds` 与置 Inactive（`Draw3.DrawingController.cpp:7616-7633`）。Hold/Fade 中旧 Up→新 Down 的烘干若失败，则保留旧层、置 FullRedraw、仍接受新 Down，并通过 `pendingLaserBakeFailed` 在同帧进入错误门（`:3273-3322,6900`）。完整重绘及稳定颜色 resolve 失败也进入同一门（`:8048-8062`）。错误帧不调用 `PresentFrame`；下帧 `activeLayerRebuildPending` 强制全画布重建 backbuffer，覆盖失败帧的部分像素（`:6697-6708,8040-8099`）。持续普通 Map 失败时空闲分支最长等待 250 ms，设备移除重建等待 16 ms；没有自发满速 busy loop（`:8160-8173`）。
4. `BeginLaserBake` 只在需要烘干时分配 scratch，并要求已提交纹理、RTV、当前 device/context；两纹理来自同一 `CreateLaserCoverageResources`（RGBA8、单采样、RTV/SRV）。复制前解绑 Laser SRV 与 OM；交换前再次解绑（`Draw3.Renderer.cpp:214-256,283-302`）。`ClearAllLaserCoverage`、尺寸资源释放和 `ReleaseResources` 释放 scratch；Resize 建新尺寸资源后只复制旧已提交 t6 的交集，不复制未提交 scratch（`:206-211,376-382,411-456`）。设备重建经资源释放也换 epoch，不会重用旧 scratch。`CopyResource`/GPU Draw 是异步 D3D 命令，没有同步成功回执；bool 只证明同步提交和 Map/资源门，后续设备/Present 失败仍须既有恢复路径。
5. 成功 bake 的资源交换发生在该帧最终 Present 前；若 Present 失败但设备仍可用，t6 可再解析。若同时 device lost，已清的成功批次 CPU 源不可恢复，归上表 P2。`CommitStagedLandings(presentSucceeded)` 与 workspace ready 只在成功 Present 后推进；失败 Up 的 landing 可能漏记，不能把该指标当成轨迹像素回执（`:8090-8150`）。

## 证据与未验证边界

- 现有无 HWND `RunLaserRasterFailureTest` 在真实 WARP FL11.0、FLIP composition swapchain 和生产 shaders 上，将合法不可 CPU 写的 DEFAULT InkData buffer 在首层成功后换入；直接调用同一生产 bake helper，并用 64×64 BGRA 与无故障参考逐字节比较。它还断言完整重绘失败上报和恢复后重试（`Draw3.HiddenWindowTest.cpp:2354-2413`；Controller `:1360-1490`）。正常产品路径的 after-layer 回调始终为 null；该注入只在显式 CLI `--draw3-laser-raster-failure-test` 可达（`IdtMain.cpp:284-285`）。
- **red Debug CLI exit 1 → green exit 0 是主 agent 的运行证据，不是本 reviewer 实测。** 我只读了已保存 stderr：red 有“CPU layers 被丢弃 / compositor 被改写 / 重试 BGRA 不等”三项失败；green 为 `PASS: no-window WARP Laser bake transaction`。主 agent 报告 Debug ARM64 Solution green Build exit 0；本审查只做七个目标文件的 `git diff --check`，exit 0。
- 这项 CLI 不运行 Host/RTS/真实 Window Service、ULW/DComp Present、device-lost、Resize 失败、scratch 分配失败、长 Hold/Fade 或 Win7 SP1+KB2670838 Hardware/WARP。DrawLaserDots 与粒子仍是 best-effort void 路径；异步 GPU 故障和 F-026 的退出保存屏障仍独立待验。F-025 七文件改动没有改生产 FLIP swap effect、DComp/ULW 选路或 Win7 回退策略，也不证明整帧性能收益。

复查命令：`git diff --unified=5 --` 加上述七个文件；`rg -n 'BakeLaserStrokeLayers|DrawLaserStrokeLayers|BeginLaserBake|CommitLaserBake|ResolveLaserCompositedColor|pendingLaserBakeFailed' Inkeys/Inkeys/Drawing/Draw3`；`git diff --check --` 加上述七个文件。后续若改代码，应由 owner 串行跑完整 Solution 与无 HWND CLI；真实设备/GUI 保留人工门禁。
