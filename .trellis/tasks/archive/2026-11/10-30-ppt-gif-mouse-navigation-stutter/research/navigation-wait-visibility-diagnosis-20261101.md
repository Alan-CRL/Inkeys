# Research: 导航等待三点不可见的 UI 路径与最小区分采样

- Query: 用户在秒级按钮翻页等待期间看不到三点，直到完成才更新页码；现有状态、调度、绘制或提交的哪一层未生效？
- Scope: internal；只读源代码、规范及最新人工采集；仅写本研究文件，不修改产品/测试、未构建或操作 GUI。
- Date: 2026-11-01

## Findings

### 1. 实测失败成立，但现有日志没有记录 UI 接受或真实三点提交

采集目录 `Build/Validation/ppt-navigation-20261101-135913-372-bfe291f9` 的 manifest 为 PID75956、exit_code=0、DLL1061DDF5...，47个按钮请求。用户明确回复“三点不可见，同时等待完成后页数才发生跳变”。等待期保留真实旧页码符合原 R5；三点不可见属于 AC4 实机失败，不能把两件事都解释为同一 UI 错误。

原生日志有47条 enqueue 和47条 complete，全部 localSession=1。最慢 Next epoch39 为14:00:16.755入队、18.392完成，持续约1.637秒；Previous epoch47为14:00:25.606到26.618。session1首次 session-clear 在14:01:03.293，晚于所有导航。

`IdtPlug-in.cpp:226` 先调用 `PublishNavigationWaiting`，随后 `TracePptNavigation("enqueue")`（:228）。后者只证明调用经过，不知道内部是否拒绝、是否改变 model 或是否真正提交三点。已有 `[UI3Diag]` 是异常聚合，不携带导航 epoch，也不能从没有该时段 warning 推断已成功显示。

因此可以排除“这两条慢请求太短、人眼来不及看到”以及“其间已有记录的 EndSession 清场”；不能由 enqueue 推定三点已经显示，更不能认定用户看漏。

### 2. session / 生命周期来源与拒绝边界

| 代码锚 | 实际规则 | 本轮判断 |
| --- | --- | --- |
| `IdtPlug-in.cpp:175-185` | request捕获当前PptSessionToken.localSession并分配单调navigationEpoch | 日志请求全为session1，与PptInfo建立本地场次的来源相同 |
| `Ppt.cpp:316-326` | 未initialized直接返回；positions非active或session不匹配直接拒绝；之后再调用等待tracker.Publish | 目前没有内部接受/拒绝证据，必须采样这些值 |
| `Ppt.cpp:398-403` | 真实changed场次清除pageState/等待，再以positions.session调用BeginSession | 等待activeSession并非从页码/descriptor推测；来源是正式UI场次 |
| `Ppt.cppm:61-69` | PositionState.BeginSession仅接受正且严格递增的新localSession，设置active | 同场重复PublishSession无需重建等待tracker |
| `PageControl.cppm:593-606` | tracker拒绝activeSession不同、epoch为0/已完成、方向None或不递增的现存等待 | f1267e12的完成水位保护有效，但本轮是否触发仍缺具体记录 |
| `PageControl.cppm:607-625` | Complete同场推进完成水位；Clear退休activeSession | 原生session-clear日志不覆盖Shutdown/Initialize等内部reset；正常慢请求期间未见原生清场 |
| `Ppt.cpp:135` | PublishSnapshot把等待完整复制进PageControl::PptState | 不能仅凭代码存在证明现场得到该值 |

本轮没有静态发现“localSession和showSession混用”或等待tracker必然未初始化的单一错误。全局搜索waiting写入只有Ppt的Initialize/Shutdown、Publish/Complete/Clear和真实changed场次；PageState.Publish不会清等待。不能为了猜测该分支修改BeginSession或删除完成水位。

### 3. 简单的漏字段 / 漏唤醒 / 漏脏区已静态排除

- `PageControl.cppm:697-724` 的 MergePptPublication按场次/layout epoch/publicationRevision拒绝旧快照；同场拖动只合并位置与pairVersions，不清等待。
- `PageControl.cppm:828-831` 的 ArePptStatesEquivalent明确比较等待session、epoch、direction、waiting四字段；不存在“去重没把等待算进去”的直接缺陷。
- `PageControl.cpp:5639-5660` 更新publishedRevision并在changed时RequestAll；`:542-545`请求共享RenderPipeline的PPT四目标。
- `PageControl.cpp:774-784` 的SnapshotForRender整体复制PptState；`:746-759`拖动合并只影响layout，`:2321`调用的ResolveRuntimePageControlLayout以PptState值拷贝为输入并返回，亦不丢等待。
- `PageControl.cpp:1174-1181` 在观察到新publication revision时重新ConfigurePagination；等待不要求页码变化。`:915-916`的BuildPageContent按同场和方向映射两个等待bool，白板明确隔离。
- `Bar.Scene.cpp:379-380` 对等待方向变化分别IncludeButton(2/4)；`:286-293`的Include负责映射、扩张和invalidated/damageRevision推进。
- `Bar.Scene.cpp:679-693` 在对应按钮自定义content绘制三颗ellipse，使用TextPrimary、0.9 alpha、1.6DIP半径；`:941-960`真实Render会遍历分页drawOrder。没有静态发现必然被后绘背景盖掉的顺序错误。
- `Bar.Button.cpp:2105-2123` 先画按钮背景/图标，再调用options.content，再画文字；SVG旋转在`Bar.Rendering.cpp:3037`恢复原transform，不能直接归因为残留旋转把三点移走。

这些只是局部代码路径正确性的静态证据，不证明现场有正确model或像素。可能仍有现场publication被拒/替换、窗口candidate延后、实际绘制失败或点位/尺寸/裁剪不可见，现有样本无法区分。

### 4. 最小诊断方案：只采数值，正常退出后输出，不猜测性修 UI

读取 `ui3-render-diagnostics.md` 后，必须遵守 render owner 仅写固定数值槽、禁止I/O/格式化/任意sink的合同。不能在RenderSurface或DrawButton直接调用同步IDTLogger或Raw WriteFile。建议专用、默认关闭的有限POD采样，沿用现有INKEYS_PPT_PERF开关与QPC时间基准，不新增日志线程、不改变UI行为、COM、页回执或输入门禁。

在正常启动、所有producer开始前，仅当开关开启时一次配置固定容量数组；多producer按原子序号认领独立槽，满后保留前缀并增加dropped，不扩容、不覆盖。捕获只写enum/整数/QPC，不保存字符串、COM、HWND/地址、路径或标题。正常退出必须等PageControl交互与RenderPipeline等所有producer真实join以后，再封口、按固定枚举表格式化为独立`[PptWait]`前缀并写采集stderr；报告capacity/seen/retained/dropped，seen=retained+dropped。若15秒兜底强退或未join，尾部可能没有report，必须判采样不完整，不能把缺失当“没有等待”。

建议最少边界如下，能共用既有纯值session/epoch，不能从当前TLS猜跨线程父子身份：

| 阶段 | 文件/原边界 | 最少字段与用途 |
| --- | --- | --- |
| `publish-decision` / `complete-decision` / `session-reset` | `Ppt.cpp:316-347,398-403` | request session/epoch/direction、initialized、positions active/session、tracker activeSession/current epoch/completedEpoch、接受/拒绝原因。判定等待是否建立或被退休；原状态锁内只捕获POD，保持原返回路径 |
| `model-decision` | `PageControl.cpp:5639-5660` | incoming/current publicationRevision和layout session/epoch、waiting session/epoch/direction、Merge接受/拒绝、changed、最终publishedRevision；判旧快照丢弃或新model是否唤醒 |
| `scene-configure` + `dots-draw` | `PageControl.cpp:1174-1181`、`Bar.Scene.cpp:679-693` | surface、model revision、navigation session/epoch/direction、mode、等待mask；实际进入三点分支/brush是否成功/实际ellipse次数。仅回调内数值记录，不能用“配置wait=true”代替实际画点 |
| `pixels-result` + `native-commit` | `PresentScene`成功或失败边界`:1473-1510`，`ConsumeNativeSurfaceReceipt`:2130-2206` | 原snapshot导航身份、surface/model revision、原有结果、pixelsSubmitted、candidate/frame serial或revision、真实receipt接受与shouldShow。区分无damage空Success、真实D2D/GetDC/ULW/ReleaseDC/EndDraw成功，以及Window Service receipt提交 |

为把实际DrawButton与导航身份相连，可在既有PresentScene调用scene.Render前后增加诊断专用TLS数值context，RAII恢复；携带调用方已取得的session/epoch/surface/revision，不扩展产品页模型/ABI。scene内部只向当前槽或固定sidecar写draw flags；无需为诊断读取COM/额外查询实时HWND。若现有FrameDiagnostics/raw sidecar能承载同等纯值可复用其生命周期；不要修改RawCaptureReport正式schema或创造跨产品通用遥测层。

重绘可连续多帧，按session/epoch/surface/阶段的首次结果或状态改变有限记录即可；失败→成功仍需保留。不能因只记首次失败而丢掉实际恢复，不为诊断制造render wake。实际点位/clip交集若被怀疑，只记录该分支已算出的数值包络、尺度和clip关系，不能采截图或在热路径读回GPU。

复用边界：`PptNavigationDiagnostics.h::Enabled/Now/Frequency/Process/Thread`可作为关闭门和时钟参考，但其Recorder.Emit会调用任意sink，不能直接复用到render热路径；`ShutdownProgressTrace.h::LastErrorGuard`可参考保存Win32错误，但其WriteRaw仅可用于最终导出。采样关闭时零新QPC/格式化/数组分配，不影响现有诊断TLS、Win32 LastError或原业务结果。

### 5. 验证与最小文件边界

产品改变限定为诊断helper、Ppt.cpp（状态决策）、PageControl.cpp（model/configure/原成功事务）、Bar.Scene.cpp（实际三点分支数值）、正常启动/退出的配置与真实join后导出挂点；视最小复用情况，不必更改IdtPlug-in业务逻辑，原native-request已有session/epoch可关联。无需修改PptCOM.cs、动态COM访问、接口/TLB、等待持续时间或渲染策略。

无窗口验证应复用生产路径：

1. 固定容量POD recorder的关闭零读钟、容量溢出/seen账本、并发独立槽、seal只在真实join后、输出失败不改业务/LastError，边界不引入I/O。
2. 扩展当前`PageControl.cpp:3503-3513`等待测试。现有测试仅检查BuildPageContent的两个bool与旧页码，未比较实际等待像素；它可以全绿而三点不可见。
3. 复用`:3725-3758`已有真实D2D读回/逐像素对照：正常帧→同页wait→complete，验证对应按钮区域像素确有差异、另一方向/页码/白板无变化，partial-damage结果等于完整重画；覆盖四目标及现有有意义倍率。无窗口像素测试可先发现绘制/clip错误，避免再让用户测同一无证据版本。
4. 诊断on/off操作与渲染BGRA/ready状态等价；失败及空Success不伪装pixelsSubmitted/真实commit。

如无窗口测试直接暴露确定错误，再由实施代理提出最小修复；当前不能先改UI。若静态与离屏仍不能定位，则下一次人工样本必须用新增可区分版本，短时复现一次明显等待并正常退出即可，不要求用户再跑同版大矩阵。

## Files Found

- `Inkeys/IdtPlug-in.cpp`：按钮业务入队/完成、原native-request身份、EndSession。
- `Inkeys/Inkeys/UI/Ppt/Ppt.cpp` / `Ppt.cppm`：正式UI场次、等待tracker、纯值snapshot发布及回执。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cppm`：等待纯策略、PptState去重/merge和布局值归一化。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`：共享model、调度请求、scene配置、真实像素和Window Service候选回执、既有离屏像素fixture。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp`：等待bool脏区与真实三点绘制。
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` / `Bar.Rendering.cpp(m)`：公共按钮自定义content顺序、brush、transform恢复。
- `Inkeys/PptNavigationDiagnostics.h` / `ShutdownProgressTrace.h`：既有默认关闭QPC诊断和错误保护可参考，render不能直接使用其sink输出。
- 最新采集manifest/stderr/native日志：导航时长与场次清理边沿；没有等待model/像素关联字段。

## Related Specs

- `.trellis/spec/ppt-interop/native-session-ui3.md`：导航等待R5、正式场次/epoch、保留旧页码、不产生ready。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：PageControl四目标、dirty clip、真实GetDC/ULW/ReleaseDC/EndDraw及candidate回执。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：固定数值热路径、禁止render I/O/格式化、真实join后seal、掉样与软件提交证据边界。

## External References

无外部查询；全部结论来自本地代码、规范和本次人工日志，不涉及新平台/库版本事实。

## Caveats / Not Found

- 未发现能静态证明本轮三点必然不可见的单一代码根因；未运行构建/测试、COM/GUI，也未修改产品。
- 用户反馈为实机可见性失败；软件draw/ULW/receipt即使成功也不等于DWM/光学可见。新诊断将定位到可证边界，不宣称观察到用户屏幕像素。
- 本轮退出正常与三点失败、owner排队1.239秒为独立事实；不能用正常退出替代等待反馈或性能验收。
- 行号为本轮研究快照，主会话/实施代理后续编辑可偏移，按方法和语句定位。
