# F-023 光栅提交失败独立审查

日期：2026-09-28。范围：最终工作区中 `Draw3.StrokeGeometry.cpp`、`Draw3.InkPrediction.cppm`、`Draw3.DrawingController.cpp/.cppm`、`Draw3.HiddenWindowTest.cpp/.h` 的差异及生产调用链。只做静态审查和读取已有构建/测试日志；没有重新编译、运行 GUI 或采样性能。

## 结论

L1 普通笔/荧光笔/橡皮的首批 `Map` 失败假提交已修复，L0/Shape 和主帧 `ApplyOperatorLayers` 的可检测失败也会阻止**当帧** `Present`。可见 tile 失败门禁与空闲重试的自唤醒问题经后续最小修改，均已在静态控制流上关闭。最新两处修改没有包含在先前构建/无 HWND 绿测里；完整 Controller/Present 故障注入仍未运行。

## 发现

### [已静态关闭] Stored 失败后的可见 tile 假回执

此前的条件链是：Up 的 `DrawStoredStroke` 失败后 CPU 文档/history 保留、当前帧不 `Present`；下一帧可见 tile 的 `RestoreComposition` 再失败时，只保留 tile 游标，随后可能对不完整 L2 成功 `Present`。最新 `Draw3.DrawingController.cpp:6664-6669` 在 `!tileCompleted` 分支设置现有 `rasterSubmissionFailed=true` 后才 `break`，且不推进 `viewportTilePlanIndex`。随后 `:7810-7812` 跳过合成/`Present`，`:7919-7947` 的内容 revision、workspace ready、landing 和成功可见快照均不推进。故对**可检测的可见 tile 重放失败**，原 P1 的假回执已静态关闭；仍缺 Controller/Present 级运行验证。

### [已静态关闭] 失败帧自身 control wake 绕过空闲等待

失败收尾现于 `Draw3.DrawingController.cpp:7885-7908` 只保留 `activeLayerRebuildPending=true`，不再调用会同步 enqueue `nullptr` control 记录的 `window_.RequestFullPresent()`。无 active/静止橡皮时，`:7969-7973` 若无外部待处理输入可实际进入 `WaitForWake(...,250ms)`；到时后下一轮 `:6520-6528` 根据 pending 重建 active L1/L0，并强制全画布 Present 尝试。可见 tile 失败也走同一标志和等待，原 tile 游标由 `:6664-6675` 保留，所以下次会自动重试而不依赖新输入。活动 contact 仍按原帧 deadline 前进，Host Stop 的 control wake 能提前结束等待。空闲时设备移除的恢复起步也可能因此等至多 250 ms，这是有界延迟。`PresentFrame`、首帧/完整画布合成以及 composition-changed 的其他 `RequestFullPresent` 调用仍保留，未见它们在这条持续光栅失败循环中自发重复。此结论仅为静态调度审查，没有 CPU 占用实测。

### [P3] tile-only 失败触发额外 L0/L1 重建

可见 tile 失败只涉及 L2/history，当前通用失败收尾仍设置 `activeLayerRebuildPending=true`（`Draw3.DrawingController.cpp:7885-7889`）；下一帧 `:6520-6528` 清空并重绘全部 active L1/L0。空 active 时这是额外清层；有活动长笔画时每次 tile 重试都增加重放成本，且可能因另一个 `Map` 错误延后恢复。未见因此推进错误游标或丢失 CPU 点。可考虑仅在 L1/L0/Shape/合成已被部分写入时安排 active 重建；本审查不做性能结论。

### [范围边界] 可信快照 pass 的 bool 返回是可选退路状态

`Draw3.DrawingController.cpp:7838-7849` 仍忽略 `CompositeTrustedL2SnapshotToBackBuffer` 的返回值。该函数先复制当前 L2；快照资源缺失、与当前 viewport 无交集，以及 `globalCB`/`trustedL2SnapshotCB_` 的 `Map` 失败都返回 false（`Draw3.RendererSnapshot.cpp:177-201,213-223,241-253`）。前两者是可选视觉退路不可用，不能把每个 false 直接升级为 F-023 光栅失败，否则预算分帧恢复/平移时可能无谓阻止 Present。后者是真实快照 pass 失败，但当前 API 没有区分原因；若要求该 pass 的失败也阻止成功回执，需先拆分结果。此行为早于 F-023，属于 F-024 渐进恢复/可信快照专项边界，不应再把它列为 F-023 当前 P2 阻塞项。

### [P2，既有生命周期缺口] 致命设备恢复失败绕过最终保存屏障

普通非 device-lost `Map` 失败会保留 active CPU 点，Up 仍先写文档/history，之后正常 Host Stop 的 `PrepareExitAutoSave` 扫描文档和 parked slot（`Draw3.DrawingController.cpp:6064-6079`；`Draw3.Host.cpp:1320-1348`）。但 `RecoverFromRuntimeFailure()` 或 history cache 重建失败时，Run 在 `Draw3.DrawingController.cpp:6351-6357,6372-6377` 直接请求退出并 `break`；如果当时尚有 active contact，其 Up/文档收尾和最终 barrier 均没有保证。Host Stop 会在 controller 已停时记录 `exit_barrier ... controller_stopped`（`Draw3.Host.cpp:1326-1336`）。这条退出路径在 F-023 前已存在，本补丁只是让已检测的设备移除也沿用它；不能把一般 `Map` 重试缺口误写成永久 UInk 丢失，也不能宣称所有恢复失败都有保存屏障。

## 已核对且通过的静态路径

- `CommitStablePrefixToL1` 只在 renderer 返回非负后推进 `committedIndex` 和 `hasCommittedGeometry`，荧光 primitive/bounds 缓存也只在成功后追加（`Draw3.StrokeGeometry.cpp:1311-1356`）。`CommitEraserRealPointsToL1` 对真实点与单点 fallback 同样在成功后推进（`:1359-1384`）。返回结构中空 dirty 的 no-op 默认为 `succeeded=true`，失败为 false（`Draw3.InkPrediction.cppm:628-642`）。
- 后续批失败会留下前批 GPU Draw，但 CPU 游标不前移；下一帧 `activeLayerRebuildPending` 清空并从已提交 CPU prefix 重绘 L1/L0，再重试未提交段（`Draw3.DrawingController.cpp:1278-1320,6520-6528,7342-7352,7881-7904`）。L1 Add/MAX、Retain/MIN（`Draw3.Renderer.cpp:452-469`）避免重复覆盖率相加；橡皮也是 MIN。无 HWND 测试只注入**首批**失败，没有后续批注入或 GPU 像素回读，故跨批颜色/接缝等价仍未验证。
- `RebuildActiveLayers` 的 Stable/L0/Shape 返回、共享 L0 重建、resize/device 恢复、Up 后 active 重建都进入帧失败门禁（`Draw3.DrawingController.cpp:1298-1319,6401-6404,6495-6498,7636-7639,7700-7715`）。主帧 `ApplyOperatorLayers` 和 `CompositeLayersToBackBuffer` 的 bool 检查阻止该帧 `Present`（`:1506-1515,7833-7879`）；首帧 `ClearCanvas` 和 `PresentFullCanvas` 也检查合成结果（`:1537-1576`）。D3D `Draw`/`CopyResource` 本身是无同步 HRESULT 的命令，不能从这些检查推出 GPU 最终像素正确。
- 失败帧的 `CommitStagedLandings(false)` 不计 Down/Up landing，`contentRevisionNeedsPresent`、workspace ready 与上次成功 cursor/laser 快照仅在成功 Present 后推进（`Draw3.DrawingController.cpp:7800-7808,7919-7947`）。Up 当帧若 live 提交失败而 Stored 成功，landing 可能因 runtime 已回收且 staged 被清除而缺记；这是指标漏报，不是虚报成功，未见数据回滚。新增 tile 门禁也沿用这条路径。
- `Host::Stop` 正常路径先封桥、停止 RTS、唤醒绘制线程，等待 `PrepareExitAutoSave` 回执后排空 worker，再请求绘制线程退出（`Draw3.Host.cpp:1303-1348`）。无 active/静止橡皮的持续光栅失败现在可等待至多 250 ms，且真实输入/停止信号可提前唤醒；活动 contact 按帧 deadline。tile-only 失败仍多做一次 active 层重建，见上方 P3。

## 证据和边界

已有红测日志 `TestResults/release-hardening/draw3-raster-failure-red-debug-arm64.stderr.log` 为八项 L1 游标/缓存/重试 FAIL；绿测日志 `draw3-raster-failure-green-debug-arm64.stderr.log` 报无 HWND WARP PASS。worker 记录 Debug|ARM64 完整 Solution Rebuild 与最终 Build exit 0，最终 Build 日志也显示 `0 Error(s)`；这些证据**早于可见 tile 失败门禁和移除失败分支自唤醒的修改**，本审查没有重跑。先前 `git diff --check` 对六个审查代码文件 exit 0。测试用合法 `D3D11_USAGE_DEFAULT`、无 CPU write 的资源触发真实 WARP FL11.0 `Map` 失败（`Draw3.HiddenWindowTest.cpp:2190-2245`），并覆盖 L1 重试、L0 返回和 Shape 直接返回（`:2254-2349`）；它没有创建完整 RTV/Present/Controller 循环。

Laser 全重绘的丢弃错误返回属于独立 F-025；极端有限坐标导致 footprint/history 不原子属于 F-024。两项均不能当作 F-023 绿灯。Release、其他架构、Win7、真实 HWND/笔迹像素、持续资源故障与性能数据仍无本审查证据。
