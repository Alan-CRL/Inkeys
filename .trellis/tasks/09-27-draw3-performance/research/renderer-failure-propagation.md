# Research: Draw3 raster submission failure propagation

- Query: `CommitStablePrefixToL1`、`CommitEraserRealPointsToL1`、`DrawL0LiveComposite` 在 renderer `Map`/Draw 失败后是否错误推进 L1/L0 与可见/持久化状态；如何最小修补并做无 HWND 回归。
- Scope: internal（附已有 Microsoft D3D11 能力文档线索）
- Date: 2026-09-28

## Findings

### 文件与调用链

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp` | 稳定前缀、橡皮、L0 与 Stored Stroke 的实际生产提交逻辑。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm` | `ActiveStroke` 的 CPU 点、已提交游标/缓存与三个提交 API。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp` | 普通笔、荧光笔、Shape 的真实 D3D11 `Map`、分批 Draw 与 `-1` 错误返回。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp` | L1/L0 Add/MAX、Retain/MIN、合成与 GPU 资源释放。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 每帧调用、dirty/Present、Up 存入文档/history、恢复。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | Present 故障进入 device/presenter recovery；Map 故障目前不进入此通道。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` | 现有 `--draw3-renderer-map-test` 无 HWND WARP FL11.0 测试，可扩展为真实 renderer 故障回归。 |

### 已确认的代码路径缺陷（静态确认，尚无本项故障注入运行证据）

1. **L1 稳定前缀会假提交。** `CommitStablePrefixToL1` 先构造 `[committedIndex, protectedStartIndex]`，调用 `DrawHighlighterPrimitives` 或 `DrawStrokeOrDot` 却不读取返回值，然后无条件写 `committedIndex=protectedStartIndex`、`hasCommittedGeometry=true`（`Draw3.StrokeGeometry.cpp:1311-1355`）。两个 renderer API 的 InkData、荧光 primitive 和常量 buffer 的 `Map` 均可返回 `-1`，批次不完整或完全未 Draw（`Draw3.RendererPrimitives.cpp:48-103,106-150`）。下一帧 `protectedStartIndex<=committedIndex` 时直接 no-op；`RebuildL0DrawPoints` 从 `committedIndex` 保留连接点但不重画之前稳定段（`Draw3.StrokeGeometry.cpp:1290-1308`）。故稳定段可在正在落笔期间缺失，即使下一次 `Present` 本身成功。
2. **橡皮失败更早写游标且没有 L0 兜底。** 非空真实点路径在 Draw 之前先写 `committedIndex=latestIndex`，之后忽略 `DrawStrokeOrDot` 的 `-1` 并设置 `hasCommittedGeometry=true`（`Draw3.StrokeGeometry.cpp:1358-1381`）。下一帧同一 latestIndex 被拒绝重试；Controller 对橡皮明确清空 `l0DrawPoints/currentL0Rect`（`Draw3.DrawingController.cpp:7301-7305`）。单击 fallback 在失败后也被 `hasCommittedGeometry` 阻止。结果是失败那批擦除在活动笔迹上不可见。
3. **L0 失败被当作已绘制。** `DrawL0LiveComposite` 对高亮/普通笔忽略 `Draw...` 结果并返回 `void`（`Draw3.StrokeGeometry.cpp:1384-1397`）；Controller 的共享 L0 重建先清层，再逐 contact Draw，未检查这些失败，Shape 批处理也忽略返回（`Draw3.DrawingController.cpp:1235-1269,7634-7656`）。随后仍按计算出的 rect 合成/Present（`:7747-7814`）。因此 `PresentFrame` 成功只表示提交了当时 backbuffer，**不证明本帧所要求的笔迹像素已提交**。
4. **诊断可虚报可见。** `runtime->metricVisible` 由非空计算 dirty 设置（`Draw3.DrawingController.cpp:7345-7353`）；Up 时据此 StageLanding（`:7586-7595`），帧末只用 `Present` 成功 CommitStagedLandings（`:7842-7846`）。当 Draw Map 失败而 Present 成功，Down→首次成功 Present 指标有机会把空白/不完整帧计入有效落笔，应另记 raster-success gate。仅前述静态路径成立；具体发生频率未测。
5. **Up 后文档真值和 GPU 像素是两条路径。** 非取消笔迹 Up 时从完整 CPU stroke 生成 `InkStroke`、先 append 到 canvas/history，再重新以 `DrawStoredStroke` 绘 L1→L2（`Draw3.DrawingController.cpp:7419-7477`）。`DrawStoredStroke` 自己会检查 renderer `-1` 并返回失败（`Draw3.StrokeGeometry.cpp:620-687`），Controller 在失败时不把当前 L2 当成功，并设置 `viewportRefreshPending`，后续按文档重建（`Draw3.DrawingController.cpp:7534-7565`）。因此一次仅发生于 live L1/L0 的暂态失败**可能**在 Up 的完整重栅格化中修复；不能称为永久文档丢失。持续 Map 失败则文档/history 已成立，但像素不能恢复，直至资源恢复成功；后续 viewport replay 同样依赖该 renderer。源码不足以证明真实设备在何种故障下会持续失败。
6. **presenter recovery 不覆盖这类失败。** `PresentFrame` 只在 `presentation_.Present` 返回 false 时设置 `graphicsRecoveryPending_`；resize 同样显式 MarkRuntimeFailure（`Draw3.DrawingController.cpp:1505-1521,1546-1565`）。`TransparentPresentationController::SetFailure` 会检查 `GetDeviceRemovedReason` 并安排恢复（`Draw3.TransparentPresentation.cpp:507-520`），Controller 在下一帧恢复或退出（`Draw3.DrawingController.cpp:6309-6365`）；但三处 Draw API 丢弃 `-1` 不会触发该路径。即使 Map 因 device removed 返回错误，只有 Present 也失败时现有 recovery 才有机会接手。

### F-020 与风险分级

- 已有 F-020 修复对当前设备查询 `D3D11_FEATURE_D3D11_OPTIONS::MapNoOverwriteOnDynamicBufferSRV`，不支持或查询失败时普通笔与 Shape 逐批 `WRITE_DISCARD`，支持时保留环形 `NO_OVERWRITE`（`Draw3.Renderer.cpp:419-432`，`Draw3.RendererPrimitives.cpp:59-65,172-179`）。因此 **H0 中不支持动态 SRV NO_OVERWRITE 造成的首批 Map 失败这一确定触发条件已被移除**；它是发现传播缺陷的线索，而不是证据表明修补后所有 Map 都成功。荧光笔/常量 buffer 本来就用 DISCARD，也仍可能因 device lost/资源压力/参数问题失败。
- 分类：`confirmed` 源码错误路径、运行触发 `not verified`。建议 P1/高优先级正确性：触发后用户书写/擦除可缺段，而一次成功 Present 可被当作可见回执；通常 Up 的 Stored 重绘可能纠正，持久性故障仍会阻断显示。尚无证据称其已造成 UInk 数据损坏或在当前设备复现。

### 最小修补边界与性能约束

1. 将三个提交 API 的结果改成能区分 `NoChange / Submitted / Failed` 的窄状态（附 dirty；Map HRESULT 可由 renderer 保存/返回）。仅 `Submitted` 后推进 `committedIndex/hasCommittedGeometry`；橡皮游标赋值移到成功之后。高亮 `committedHighlighterGeometry` 的 primitive/bounds 只在 Draw 成功后追加，失败时保留现有缓存，避免 retry 重复增长。无需改变 modeler、输入队列、采样率或点流。
2. 若 `DrawStroke`/高亮在超长笔迹的**后续批**失败，前面批已 Draw 到 L1。下一次从旧游标重交整段时 Add 采用 MAX、Retain 采用 MIN（`Draw3.Renderer.cpp:451-470`；`shaders/cpu-gpu-contracts.md:197-218`），对同一锁存样式/几何重复提交数学上幂等；不得用 additive blend，也不可简单把游标推进到失败批，因为段连接点与 highlighter primitive 缓存可能不完整。重试只在失败发生时付费，不改变正常长笔画每帧成本。需要实际 GPU 回读验证量化/部分批等价，不能只凭公式认定视觉完全等价。
3. L0 每次重建先清共享层；任一接触或 Shape Draw 失败应把本帧标为 `rasterSubmissionFailed`，不向 Present/landing/ready 报完整帧成功，并保留旧/新 dirty 供下一帧重试。不得只返回空 RECT：空 RECT 与 no-op 已有不同含义。恢复后按 CPU 点/primitive 重建完整共享 L0/L1；若跳过当前帧 Present，保持上一张成功画面，同时安排**有界**唤醒，避免持续错误忙转。
4. Renderer 失败需向 Controller 传播具体 HRESULT 或至少检测 `renderer.device->GetDeviceRemovedReason()`；设备丢失走现有 `presentation_.MarkRuntimeFailure`/重建和 CPU document replay。普通暂态失败可下一帧重试；连续失败要有限次数后进入受控资源重建/失败退出和可诊断日志，不能无限把 blank Present 标成功。不要让 live 提交失败回滚已接受 CPU 输入或让正常 Up 追加两次 Stroke。
5. `RebuildActiveLayers` 在 resize/另笔 Up/恢复时同样忽略 `DrawStablePrefix`/L0/Shape 错误（`Draw3.DrawingController.cpp:1200-1307,6455-6456,6364-6365,7582-7584`）；修补状态向这些调用者传播，避免恢复分支再次误标完整。历史 tile 的 `DrawStoredStroke` 已有失败返回与 replay 状态，属后续专项复核，不建议本修补无界重写历史引擎。

### 可执行的无 HWND 生产回归方案

- 复用现有 `Inkeys.exe --draw3-renderer-map-test`：它在无 HWND 的 WARP FL11.0 上创建真实动态 InkData SRV、常量缓冲与 staging，直接调用生产 `InkRenderer::DrawStroke/DrawShapePrimitives` 并回读（`Draw3.HiddenWindowTest.cpp:2061-2183`）；已有红→绿 F-020 证据见同任务 `no-overwrite-compat-fix.md`。可在同一 isolated 入口增设可写权限错误资源：创建有效但 `D3D11_USAGE_DEFAULT`/无 CPU write access 的同布局 buffer，暂时交换进 renderer，使真实 `context->Map(...WRITE_DISCARD)` 返回失败；再恢复原动态 buffer 并重试。先小范围验证该 API 在当前 WARP 上按 HRESULT 失败且不崩溃，不能直接给 Map 传 null 或破坏真实设备。荧光 buffer 和 globalCB 分别覆盖，模拟第一批失败；测试直接调用生产 `CommitStablePrefixToL1`/`CommitEraserRealPointsToL1`，断言失败时游标、flag、highlighter cache 保持且下次成功后只推进一次。此法是合法 D3D 资源状态注入，不在正式默认产品保留崩溃开关。
- 另加纯生产策略测试覆盖 `NoChange/Failed/Submitted` 的游标更新与 dirty gate、末端重试、单点橡皮、多个活动 contact 的共享 L0 失败；测试应调用上述真实生产函数/薄状态 helper，不能复制一套算法。把 `Present` 成功与 raster success 分开断言。若要验证**后续批**失败，优先通过窄的内部 renderer 提交接口注入“第一批成功、第二批失败”，并让产品和测试走同一接口；避免为测试向用户公开故障命令。另用无 HWND 离屏 RTV/BGRA 回读比较重复 L1 提交与单次提交的像素；没有该回读则只证明状态逻辑，不证明 MAX/MIN 像素等价。
- 故障注入不要写入真实用户文档/配置，也不运行需要关闭的隐藏 HWND/GUI 测试；真实 FL11.0/WARP/HARDWARE、DComp/ULW/FLIP、Win7 SP1+仅 KB2670838 的 Present 和真实笔感仍是独立人工矩阵。

## Related specs and external references

- `.trellis/spec/native-desktop/draw3-integration.md:60-75,377-384`：独立 device/owner、Win7 `NO_OVERWRITE` 能力门禁、仅 DComp/ULW 和保留 `FLIP_SEQUENTIAL`。
- `.trellis/spec/native-desktop/input-and-ink.md:5-19,157-165`：唯一 RTS producer、不可丢输入点、历史/工具回归。
- `.trellis/spec/shaders/cpu-gpu-contracts.md:3-17,197-220`：buffer 与 Add/MAX、Retain/MIN、L0/L1/L2；`.trellis/spec/native/quality-and-validation.md:12-31` 明确检查早退与提交游标。
- 官方 D3D11 动态资源文档：<https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources>；`D3D11_FEATURE_DATA_D3D11_OPTIONS`：<https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options>。这里沿用同任务 F-020 研究已核对的文档链接；本次未在真机复验。

## Caveats / Not Found

- 本轮按派单只做源码/规范研究；未运行编译、无 HWND WARP 注入、GPU readback、隐藏窗、Win7/HARDWARE/WARP、真输入或性能采样。无法给出实际触发率、延迟分位数或 crash/no-crash 结论。
- 当前没有 renderer Draw 失败的统一 HRESULT 上报；`int=-1` 只表明失败，不标识 device lost、暂态 Map/内存或非法资源。`ID3D11DeviceContext::Draw` 本身无同步 HRESULT，提交成功只保证 CPU command 已排队；像素/Present 成功另测。
- `F-020` worker 正在或刚完成其他文件修改；本研究依据读取时的工作区 `Renderer.cpp/RendererPrimitives.cpp`，不覆盖其代码或独立 review。Win7 FLIP 用户实测约束与本次 Map 故障传播问题彼此独立。
