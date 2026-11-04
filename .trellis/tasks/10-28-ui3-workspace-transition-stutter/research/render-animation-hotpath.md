# Research: 三态转场的主栏动画与公共绘制热路径

- Query: 桌面/PPT/白板切换为何放大 UI3 主栏绘制负担，哪些路径可做最小修复？
- Scope: internal
- Date: 2026-10-28
- 基线: 当前工作目录源码；只读调查，未编译、未启动 GUI、未调用性能 runner。

## Files found

- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`: 主栏快照、目标提交、共享动画推进、中心派生、容量、damage 和成功呈现事务。
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`: 公共按钮状态投影、尺寸目标、颜色目标和统一推进。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp`: 公共 Shape/SVG/Word、设备资源、光影父遮罩和 exact/slices 回退。
- `Inkeys/Inkeys/UI/Bar/Bar.UI.cpp`: SVG 内容/颜色的解析、栅格化和上传。
- `Inkeys/Inkeys/UI/Bar/Bar.Animation.cppm`: 幂等目标、Back 曲线及解析极值、字体连续修正。
- `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h`: 已存在的实际曲线包络算法。
- `Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h`: 映射变化及根布局变化的整窗替换合同。
- `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp`: 多客户端串行调度及 batch 起点节拍。
- `Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp`: 稳态基准及独立的六向转场正确性 fixture。

## Findings

### 1. 已确认：工作区切换会改变真实公共按钮，而不是替换一套主栏

`Bar.Button.cpp:959-975` 采样 Whiteboard/PPT/内容/选择/Freeze 状态并生成 A2 投影；`:1003-1021` 决定 Whiteboard/Freeze/Pierce/EndShow 的尺寸和显隐。`Bar.RenderLoop.cpp:1678-1685` 在 presentationMutex 内取得公共 Snapshot，`:1734-1736` 用 ResolveBarButtonListLayout 求完整目标宽度，`:2544-2587` 比较前次真实布局并决定加入/重启批次。该锁覆盖目标布局扫描，但不能仅凭锁存在认定发生长时间争用。

公共按钮持续持有同一对象；`Bar.Button.cpp:2166-2203` 的 ConfigureBarButtonVisualTargets 只提交数值目标，不每帧 new 一套按钮。`Bar.Animation.cppm:793-814` 相同目标提前返回，只有真实变化或显式 forceRestart 才清 progress。因此“每帧调用 SetTar/Configure 就必定重启动画”不成立。转场中必须进一步测实际 layout revision/target 变更，不能删除公共投影或绕到私有按钮绘制。

### 2. 已确认的持续放大：居中宽度转场使根位置变化，触发整 viewport 重画

`Bar.RenderLoop.cpp:5584-5616` 在 BottomDocked+Centered 的既有门禁下，根据本帧动画宽度求联合中心，再用 SetDirect 派生 root x；这是保持可见中心稳定的合同。只要 root x 真实变化便设置 bottomDockRootLayoutChanged。

`Bar.PresentDecision.h:18-24` 将该标记、viewport/source 改变和映射 tuple FullReplacement 任一命中视作整窗替换；`Bar.RenderLoop.cpp:9644-9658` 对应 ForceFullDamage 和 businessDirty=candidateViewport。它会让公共按钮、文字和光影一起进入真实绘制，降低局部 dirty cull 在转场时的收益。变化在 Centered 主栏宽度仍在运动时可逐帧发生；稳定状态会归零，不是永久唤醒。

这是明确可达的工作量放大原因，不能直接称为错误，也不能简单删 rootLayoutChanged/full replacement：旧像素、source映射和未提交快照会有残影/命中风险。第一批应保留该保护；若性能采样证明它主导且前面较小修复不足，再单独设计稳定局部坐标与映射、用真实完整/部分 BGRA 对照证明可减少替换。

历史旁证：本轮只读解析 `TestResults/ui3-workspace-centering/samples.csv`（441394 bytes，mtime 2026-10-28 10:48:24），按(case,phase)共96组；right-100 的 DesktopToPpt 43/43、WhiteboardToPpt 44/44 记录 full_replacement=1。该 CSV 没有在本研究中核对二进制 hash/commit，且 frame_time_ms 是 fixture 时间；它证明既有测试轨迹经常走整窗保护，不是本轮墙钟性能实测。

### 3. 已确认的白板转场成本：颜色变化直接触发 SVG 重新解析/栅格化/上传

可达链不依赖用户切主题：

1. `Bar.Button.cpp:961,1003-1006`：进入/退出 Whiteboard，使 Whiteboard 按钮 Selected/None 改变。
2. `Bar.RenderLoop.cpp:2643-2645` 由 selected 决定 Accent/TextPrimary；`:2688-2689` 调用 RetargetBarButtonInteractionVisual。
3. `Bar.Button.cpp:1940-1944,1953-1962` 将公共 SVG color1/color2 提交到对应内容色；统一 AdvanceBarButtonVisual 在 `:2296-2325` 推进它。
4. `Bar.Rendering.cpp:2951-2982`：每个实际变化后的颜色都会令 colorChanged=true，直接调用 CacheBitmap；尺寸动画本身已有阈值复用，不能泛称“所有动画每帧重建 SVG”。
5. `Bar.UI.cpp:467-548` 替换 SVG 颜色字符串并 lunasvg::Document::loadFromData；`:554-565` renderToBitmap；`:582-589` CreateBitmap 上传。

白板图标初始化确实带 color1（`Bar.Button.cpp:576-577`）；`Inkeys/src/UI/barWhiteboard.svg:2-4`、`barDismiss.svg:2` 均包含 rgba(10,0,7,0) 占位色。故白板进出期间的连续换色可以产生连续解析/栅格化成本；中点图标切换还会形成合法的内容冷建。

这确认额外 CPU 资源工作存在，未证明该工作占用户长帧主要比例；Desktop↔PPT 在工具和 selected 状态未变时，不保证触发此枚按钮变色。应使用已有 Ui3SvgStageTimer/观察探针采集 Parse/Raster/Upload；若占比低即不扩大 SVG 优化范围。禁止以直接跳色、降低光效或粗量化尺寸来换表面流畅。

### 4. 有条件的高影响候选：容量启发式随当前 Back 值增长，扩容连带清设备缓存

`Bar.RenderLoop.cpp:7743-7758` 的 AnimationAbsMaximum 每帧取 abs(val/startV/tar/middleV) 最大值，活动曲线为 Back 时再乘1.06。`:7783-7805` 对 requestedCapacity 精确 ceil 并只扩不缩。进入 EaseOutBack 峰值时 val 高于 target；因此原本按 target×1.06 预留后，当前值又可能被1.06放大，形成额外逐帧容量请求。

解析复核：生产 back=1.1（`Bar.Animation.cppm:91-121`），完整 EaseOutBack 峰值为 1+4b^3/[27(b+1)^2]≈1.0447136；从0到T的例子，启发式首帧上界1.06T，峰值帧约1.1073964T。这只是生产公式的解析例子，不是执行真实 coordinator 的重现；实际尺寸是否跨 ceil、是否超过历史 capacity、是否被其他包络预留覆盖，均影响是否真的 resize。

一旦真实 target 变大，`Bar.Rendering.cpp:306-314` 不再走 S_OK 快路，`:317-350` 新建 context/target 后调用 DiscardDeviceResources；`:355-373` 清除设备缓存，`:172-176` 也清主栏 SVG/PNG/按钮 icon。随后光影/SVG 冷建叠加整窗替换。此路径有界，已有足够历史容量时不触发；没有证据证明无界泄漏。这里没有确认自动清 BarFormatCache，不应把 DWrite 字体缓存也归入同一个清理链。

反证必须保留：上述历史 samples.csv 的96组，组内 target尺寸变化总数为0、增长总数为0；right-100一直2026×946、right-150一直3040×1420。既有预热可能已覆盖容量，因此不能用该 CSV 宣布重复扩容已解释三态卡顿。

最小候选应先用 fresh coordinator 的首次/新最大布局建立红测，再复用 `BarUiGetCurveExtrema`（Animation.cppm:64）及 `ResolveBarWindowAnimationRange`（Bar.WindowGeometry.h:63）计算稳定、完整的实际段包络，替换“当前值反复乘1.06”的启发式。保持现有峰值容量、source包含、窗口viewport和full replacement规则。只有跨帧 target增长被证实并主导 Resources 时才纳入修复。

次级方案是 same-epoch 只换 target、保留设备关联缓存；它要逐项核实COM兼容性、epoch重建和失败旧资源事务，范围明显更大，不宜先做。

### 5. 光影缓存命中仍有提交成本；不能把已有旧候选原样恢复

`Bar.Rendering.cpp:1697-1725` 父圆角遮罩 key 仅含半径/描边/模糊标准差的四分之一像素值，不含矩形宽和绝对位置。所以主栏仅横向变宽或平移，不必每帧创建新 Gaussian。等比属性面板也已有 frameDiffuseMaskGeometryScale 归一（`:2604-2619`）。

然而 exact 整图路径要求 geometryScale≈1、量化半径一致、96dpi、严格 Identity 矩阵及整数落点（`:2012-2049`）；主栏常用负capacityOrigin平移（`Bar.RenderLoop.cpp:9693-9696`）和body/grip变换，会触发回退。缓存命中后仍走 `FillRoundedRectDiffuseMaskSlices`（Rendering.cpp:1878-1907）；半径一致最多3×3，不一致的动画/分数圆角最多5×5（`:2251-2289`），分别向主光/鼠标光绘制（`:2626-2637`）。

这解释为何“mask hit”不等于低成本；转场整窗重画会重复承担已有光影提交。不能据此断言每帧25次或把Gaussian创建数与slices数混为一谈。旧09-22 F1/F3已修且有独立验证；旧mask-slices性能候选已撤回的结论由主会话汇总，不应直接恢复。若Draw/GetDC为主要耗时，需现有完整 BGRA 对照、混合光/剪裁/AA/分数位移后再设计局部改进；GetDC可能包含此前D2D flush等待。

### 6. 调度与基准边界

`RenderPipeline.cpp:736-739` 在 batch开始时设置 nextDeadline=frameTime+16.666667ms，而不是渲染结束后追加固定休眠。`:802-857` 按DispatchOrder串行执行各客户端；`:884-889` Continue/Retry持续排入下一批。新增PPT四目标或白板两个目标会延后下次主栏机会，但须按每callback和batch的真实时长归因，不能拿历史34ms直接认定“双睡眠”。

`Bar.FramePacing.cppm:93-130` Idle后首次Tick=0，活动帧clamp仍为50ms；`Bar.RenderLoop.cpp:13128-13139` 的失败退避发生在WakeAndSnapshot/Tick后、动画推进前。这是失败分支的时间丢弃/慢放放大候选，正常无错误转场不能用它解释第一个慢帧；不应在本轮盲删clamp或改退避。

稳态 `--ui3-multitarget-benchmark` 在每run固定Workload，只持续按压真实按钮和更新页码（Bar.MultiTarget.Test.cpp:280-349）；它不是六向转场性能覆盖。已有 `RunUi3WorkspaceCenteringTest` 的六向/rapid fixture 可作为实际转场轨迹基础（`:550-557,1692-1699`），但当前使用确定frame时间和几何正确性断言，不等于墙钟性能基准。后续应复用它建立fresh与warm两个负载；同时接真实Scheduler/PageControl，按实际成功软件提交记录stage、mask、SVG和target增长，保持失效帧不推进成功快照。

## Minimal repair / validation candidates

1. 先建立六向+快速反向的fresh/warm转场采样，复用已存在的真实coordinator/公共Button/renderer和raw capture；分别报告Main callback、全batch、真实成功commit间隔、Resources、Draw、GetDC、ULW、PresentLockWait、mask create/slices、SVG Parse/Raster/Upload。记录二进制hash、配置、目标终态、sealed和零掉样。
2. 对容量候选先红测：fresh owner从较窄布局进入更宽Back目标，跟踪target尺寸/设备cache reset次数及source包含。若无新增目标时仍逐帧增长，改成稳定实际曲线包络；绿测要求包络覆盖、容量不随同一段current样本继续扩、失败重试保留旧帧。若实际已有一次预留完全覆盖则降级该候选。
3. 若热态无resize仍卡，优先使用已确认的整viewport重画×光影提交/白板SVG换色阶段确定主导者；根据测量选择一个公共封装修复，不默认做全套缓存重构。保持视觉轨迹/色彩/光照质量/字体连续性。
4. full replacement优化必须另有像素与命中事务证据；same-epoch保缓存也必须证明跨context设备资源合法，不能当小修顺带实施。
5. 回归复用现有 no-window、离屏公共渲染/字体/SVG、workspace-centering、multitarget入口；真实Office/WPS、原生ULW/DWM和主观帧率留显式GUI授权后人工验收。构建遵循用户当前指示 InkeysRepo.sln Debug|ARM64+ARM64原生MSBuild及同invocation PATH workaround，不根据旧spec矛盾绕过依赖。

## Related specs

- `.trellis/spec/native-desktop/index.md`
- `.trellis/spec/native-desktop/rendering-and-ui.md`：公共按钮、统一状态投影、共享串行调度、变化脏区、复合视觉、批次加入、连续字体目标。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：默认off、raw-only详细采样、有界日志、成功commit时刻与像素可见区别。
- `.trellis/tasks/09-22-ui3-animation-stutter-investigation/findings.md`、`verification/first-batch-result.md`：旧确定缺陷及已完成修复，不把历史行号当当前源码。

## Caveats / Not Found

- 无外部资料查询；本报告技术事实来自仓库源码与既有记录。
- 本轮没有实时帧数据，不能给任一候选标注“已复现用户卡顿”或“唯一根因”。历史数据与源码结构分开记录。
- 没有确认主栏每帧新建按钮对象、同目标必重启动画、每次几何平移必建Gaussian、稳定帧持续清缓存或调度器在渲染后追加一整帧睡眠。
- 未修改源码、项目配置、规范或其他任务；本报告是规划输入，非实施结果。
