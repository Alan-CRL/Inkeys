# F-023 Draw3 光栅提交失败传播

状态：Debug|ARM64 完整 Solution 复建与无窗口 WARP 绿测通过；独立 diff 审查、真实 HWND/Win7/笔迹像素未验证。

## 已复现的红灯

- `InkeysRepo.sln /t:Rebuild Debug|ARM64` 完整构建 exit 0；随后隔离进程 `Inkeys.exe --draw3-renderer-commit-failure-test` exit 1。原始输出：`TestResults/release-hardening/draw3-raster-failure-red-debug-arm64.stderr.log`，八项失败分别覆盖普通笔、荧光笔、橡皮和单点橡皮的错误游标/缓存与恢复后不能重试。首次增量 `/t:Build` 在本机 native link.exe 内 `LNK1000`/AV 卡住，已中断并保留环境故障证据；Rebuild 的 exit 0 才是红测可执行文件的编译证据。
- 测试使用真实无 HWND WARP FL11.0 `ID3D11DeviceContext::Map`。注入资源是合法 `D3D11_USAGE_DEFAULT`、无 CPU 写权限的结构化缓冲；不传空指针，也不更改产品默认设备或用户文档。

## 最小修复及调用链

1. `LiveRasterSubmission { dirty, succeeded }` 区分合法 no-op 和提交失败。`CommitStablePrefixToL1` 只在普通笔/荧光笔 Draw 返回成功后更新 `committedIndex`、`hasCommittedGeometry` 与高亮缓存；`CommitEraserRealPointsToL1` 的真实点游标和单点资格也只在成功后推进。失败时保留原 CPU 点，下帧可重试。
2. `DrawL0LiveComposite` 和 Controller 的 `DrawStablePrefix`、Shape batch、`RebuildActiveLayers` 传播实际 Draw 返回。共享 L0/Shape、resize、设备恢复、Up 后重建均把失败并入本帧状态；Stored Stroke 的 L2 resolve 已有失败分支，额外阻止本帧把缺失像素作为完整 Present。
3. `CompositeLayersToBackBuffer` 保持唯一实现并返回 `ApplyOperatorLayers` 结果。首帧 Clear、完整画布和主帧均检查可检测的合成失败。主帧仅在全部所需光栅提交成功后调用 `PresentFrame`；失败的 staged landing 通过 `CommitStagedLandings(false)` 清除，workspace ready、content revision 和上次成功可见光标快照均不推进。
4. 设备实际移除时沿 presenter 原恢复链重建，普通 `Map`/合成失败保留活动 CPU contact 与 document/history，全画布重试。错误日志最多约每秒一次；无活动或静止橡皮时 `WaitForWake(..., 250ms)` 可由新输入打断，活动输入仍走既有帧 deadline。没有引入按固定失败次数退出的新路径。

## 绿灯验证

- 当前冻结源码完整 `InkeysRepo.sln /t:Rebuild Debug|ARM64` exit 0，日志 `TestResults/release-hardening/draw3-raster-failure-green-rebuild-debug-arm64.log`；之后同 Solution `/t:Build` 明确重新编译最终 `Draw3.DrawingController.cpp`，exit 0，日志 `draw3-raster-failure-green-final-build-debug-arm64.log`。两次构建由主任务串行执行，未与性能采样并行。
- 隔离无 HWND WARP CLI `Inkeys.exe --draw3-renderer-commit-failure-test` 真实进程 exit 0，stderr 为 `[Draw3Hidden] PASS: no-window WARP raster submission failure retry`，原始位置 `TestResults/release-hardening/draw3-raster-failure-green-debug-arm64.stderr.log`。八项红灯失败对应的提交/重试断言已转绿；同测试另执行 L0 普通笔/荧光笔及 Shape 的真实 Map 失败/恢复断言。
- `git diff --check` 对本改动文件 exit 0；原有代码文件保留 UTF-8 BOM/CRLF，原先无 BOM 的 HiddenWindowTest 保留无 BOM/CRLF。此处的“通过”限定于本机 Win11 ARM64 的构建与无窗口软件设备逻辑，不扩展成 Present 或 Win7 真机验收。

## 验证边界和遗留风险

- Release、Win32/x64 与独立 reviewer 均待主任务串行执行；本子任务不并行争用构建输出或采样。
- 无窗口测试验证真实 Map 失败时的状态和下一次提交资格，并覆盖 L0/Shape Draw 的错误返回；未创建完整着色器/RTV 管线，因而不能证明 L1/L0 像素完全等价、MAX/MIN 部分批重试的最终颜色，也不能代替成功 Present/光学延迟。
- `D3D11DeviceContext::Draw` 是无同步 HRESULT 的提交，GPU 异步故障依赖后续设备状态/Present 才能发现。本补丁只覆盖普通笔/高亮/橡皮/Shape 的 Draw/Map 与合成分支。Laser 全重绘 `BakeLaserStrokeLayers`/`DrawLaserStrokeLayers` 仍忽略 `DrawLaserCoverage` 或 Resolve 的错误返回（Controller.cpp 约 912/960 行），部分 Laser API 为 void；编译期关闭的 reconnect 手工描线以及可选可信快照的视觉 fallback 也未纳入 F-023。需要独立审计，不把它们记为通过。
- 既有 `RecoverFromRuntimeFailure()` 自身失败后请求 Host 退出，可能绕过活动 contact 的最终保存屏障；本补丁没有扩大该出口。持续非 device-lost 光栅失败只保留 CPU 状态并低频重试，真机恢复与最终持久化仍是发布门禁。Win7 SP1+仅 KB2670838 的 FL11.0 Hardware/WARP、DComp/ULW、`FLIP_SEQUENTIAL` 实机 Present 没有由 Win11 ARM64 的 WARP 无窗口测试证明。
