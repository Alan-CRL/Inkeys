# Research: 抬笔时突然出现长斜线的完成态几何与光栅路径

- Query: 正常颜色阶段四人工验收时，未绘制的长斜线在物理 Up 突然出现；检查完成态拟合、颜色拆段、L1→L2 重绘和 GPU 段上传是否可能增加不属于当前笔划的连接。
- Scope: internal
- Date: 2026-10-13

## Findings

### 当前可以确认的事实

1. 正常颜色开关的变化不改变 CPU 线段拓扑。`Draw3.StrokeGeometry.cpp:33` 的 `kDebugCompletedTailVisibility=false` 仅使写盘/输出诊断不可达、Stored debug marker 空、光栅拆段不启用。`SplitTailDebugRanges`（同文件:92）在调试开启时将 `[a,b)` 拆为 `[a,s+1)` 和 `[s,b)`，共享点 s；两段线段集合恰等于原段，没有首尾闭合操作。对 n=2..64、每个拆点和不拆点，总共 2142 次纯索引等价检查，0 失败。此检查是源逻辑的离线拓扑推演，不是 GPU 验证。
2. `FinalizeStoredStroke`（:763）从完整 `realPoints` 复制 scratch 后调用拟合。`FitCompletedPenGeometry`（:617）先在独立 candidate 生成结果、验证源映射及前缀关系，最后才 resize/insert 替换尾部（:736）。稳定前缀没有被拼到另一条 Stroke。
3. 生产参考 source 在 :650 构造为 `{i,i,0}`。候选每个 source 必须在当前 `[start,count)`、相邻索引、fraction 有限且 [0,1]、参数有序（:692）；属性只从原 output[source] 插值。入口位置与真实 Up 精确检查在 :713。入口 Seam 是旧 output[start-1]→原值 output[start]，没有另造跨距连接。
4. 拟合在发布前检查补丁对窗口外前缀的相交、committed 区域公切线安全和验证预算（:719）；这些保护不保证与真实输入的全部轨迹距离有界，也不能证明输入 Up 正确。
5. `ApplyLiveTipTaper`（:251）只改变 r 并可能在线段内部插入一个线性派生点；`EnforceCapsuleTangency`（:218）只修改 r。两者不改变旧位置顺序，也不把多个 Stroke 拼接起来。源码未发现这两处正常颜色特有的长线生成分支。
6. `PlanStoredStrokeRasterRanges`（:863）按原相邻 Stored 点的 AABB 选择半开范围；合并时至多包含原点列中隔开的屏幕外原段，并未将离散端点抽成新的点列。`DrawStoredStroke`（:930）对每个范围清空 pointScratch，然后完整复制 subspan；DrawStrokeOrDot 调用在 :994。
7. `Draw3.RendererPrimitives.cpp:53` 的 DrawStroke 每批上传连续点；draw 顶点数 `(batchCount-1)*6`，下一批共享最后一点 `startIndex += batchCount-1`（:105）。`Assets/inkVertexShader.hlsl:219` 起只访问 `globalBufferOffset+id/6` 和紧接下一点。代码中未发现上一笔最后一点与下一笔第一点自动连接的逻辑。
8. `Draw3.DrawingController.cpp:13455` 的完成流程先清 L1/L0，CPU commit 完成，再从刚追加的唯一 Stored Stroke 正式重绘 L1（:13508），随后 ApplyOperatorLayers 到 L2（:13574）；每笔独立 resolve 后 RebuildActiveLayers。`Draw3.InkHistoryGpu.cpp:749/:772` 后续 tile/历史重放同样调用 DrawStoredStroke。没有完成时重新拟合其他笔划的流程。

### 自由 P2 与“没有位移硬门”的实际边界

`terminal_tail_reconstructor.cc:507` 的 SampleAdaptive 构造单条 cubic，P0/入口方向和 P3(Up)固定，P2自由；:834/:878 的三元最小二乘与极弱正则稳定解。控制柄无与窗口长度的显式上界。

- 连续导数曲率 129 个探针、参考弧长 65 个探针（:529）和总转角/反曲保护存在。
- 正常模型段累计转角 >1 rad 时，候选不能降到原正常转角一半以下（:583）；其他曲线的最大 Hausdorff 形变并非解析保证。
- 输出保留原参数锚点，有序来源，按控制多边形到弦的误差自适应细分（:590）。这些保证采样顺序与曲线采样精度，并不保证拟合靠近原始路径。
- `max_displacement` :621 和 `candidate_length` :637 只作为报告；adaptive 路径没有采用旧 Local :990/:992 的 max displacement / length ratio veto，后者为旧接口对照保留。禁止将这一事实直接当成现场根因，也不应恢复固定 3 px 门，用户已明确反对。
- 因而，较长可修改窗口的单 cubic 可以合法改变形状；如果传入 Up 本身发生远距跳变，端点固定会向跳变端点连接。缺少现场 pre/post 点列，当前不能判断长斜线是输入/模型原有跨度、合法但不合意的拟合，还是 GPU 上传。
- `InkStroke::IsValid`（`Draw3.InkDocument.cpp:79`）只检查 style、finite、width 非负及 shape 约束，不检查普通 Pen 相邻距离。不正确但有限的长边可以进入 Stored 文档。这不是建议增加任意绝对长度门；先按输入身份与阶段定位。

### 当前现场证据的缺口

读取 `Build/modeler-baseline/stage3-main-native/ARM64-Debug/Inkeys/bin/log/idt1791876669945.log`；未找到 pen/final tail、solver决策、RawUp、Stored点列、ShaderSelection 信息。这个主日志不能还原这条斜线。

`Draw3.Host.cpp:1590` 仅在 `enableHiddenTestContactInjection` 下注册 ObservePenDiagnostics；正常产品 `publishPenDiagnostics`（Controller:7663）因此没有终态八点信息。原同步 TEMP 尾段日志也被关闭，且它原来最多只记录尾窗，不够判定完整笔划首端。

应在开发诊断明确 opt-in 前提下，独立控制记录与反色，保留正常颜色复现；无需为捕获日志重新启用反色，不能改变绘制参数。

### 优先诊断字段与判别法

同一完成事务携带 record opaque 值 + generation + diagnostic strokeId、input sequence、tool/device/admission/page/viewport 身份以及物理 Up 时间。

| 阶段 | 最小证据 | 能区分的问题 |
| --- | --- | --- |
| 原始 Down/近期 Raw/真实 Up | 位置、qpc、sequence、phase、source/device、handle generation | 真实 Up 坐标或联系身份跳变 |
| 拟合前 realPoints | 总数、首尾、最长边及端点、末窗start、committedIndex、terminalFirstPoint；必要时完整有界异常窗口 | 长线是否原本存在于当前笔划 |
| 求解结果 | 状态原因、attempt/repair、source范围、maxdisp、length、输出点数；最长候选边和其source | 长跨度是否由拟合创造/是否Rejected保留基线 |
| taper后 Stored | 总数、首尾、最大邻边、hash、changed boundary、canvas offset | taper/插点/坐标变换与存储差异 |
| 绘制提交 | Stored ID/hash、planned ranges、batchCount/start/head/mapType/capability、shaderhash、画布尺寸/原点 | Stored没有此线时，追踪GPU提交 |

第一判据：若 Stored 中存在截图对应长边，沿 pre-fit→post-fit→post-taper 定位第一次出现的阶段；不存在则无需调拟合阈值，应追查 GPU 上传和 resource/data epoch。后续 Undo/Redo/保存重放是否也出现同线只能提供辅助分流证据，不能替代同事务日志。

### 最小回归建议

1. **颜色拆段等价**：对同一最终 Stored 点列，debug开/关的线段(source对)集合完全一致；不要只验证点数、首尾和 marker。
2. **正式渲染 range/batch 覆盖**：长笔划、多 tile 分离 ranges、长度跨 kMaxBufferCapacity、两条首尾远离笔划；核验每次 GPU 原语对应当前 Stored 的某个相邻点对，没有 range/Stroke 间跨接。
3. **远距 Up 输入对照**：正确定义一个正常长弧的 Down/Move/Up，再加入可控的错误 Up 联系身份或位置跳变，分别保存pre-fit/post-fit点列。测试应先确认输入路径接纳合同；不能把人为错误终点当成需要尾部平滑的正样本。
4. **长曲线无辅助数据的拟合**：对末端确有人工 pin 的大尺度浅弧/U/S，比对旧模型形状、候选全窗弧长和方向分布，保留保护前缀。这个矩阵能发现 cubic 近似“抄近路”的边界，不足以证明本截图就是这一类。
5. **正常颜色实际 GPU (需主会话确认授权)**：不依赖反色marker或无窗口CPU检查，实际像素读回和shader/上传身份配套。当前用户只提供人工截图，研究未运行GUI、隐藏窗口或D3D测试。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp` — 终态拟合、taper、Stored构建、范围规划及统一重绘。
- `Inkeys/additional/ink_stroke_modeler/terminal_tail_reconstructor.cc` — 自适应局部cubic求解及形状/预算校验。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp` — 普通笔、点击和Shape批次上传。
- `Inkeys/Inkeys/Drawing/Draw3/Assets/inkVertexShader.hlsl` — 实际相邻点取数及胶囊quad构造。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp` — 动态SRV、head和L1/L0 resolve。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` — Up结束CPU commit与首次L2绘制。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp` — 历史与tile重放。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkDocument.cpp` — Stored数据所有权及有效性检查。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` — 正常产品诊断observer启用条件。
- `Build/modeler-baseline/stage3-main-native/ARM64-Debug/Inkeys/bin/log/idt1791876669945.log` — 最新现场主日志，没有本笔几何记录。

## Related Specs

- `.trellis/spec/native/terminal-tail-reconstruction.md` — 硬入口/Up、前缀不回写、可选raw、有限窗口和无位移硬门。
- `.trellis/spec/native-desktop/draw3-integration.md` — 唯一渲染线程、设备、动态SRV capability gate、统一Stored重绘与正式证据边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md` — Draw3与UI3独立渲染生命周期。
- 当前任务prd/design — 关闭调试不应改变几何，正常颜色人工验收仍需通过。

## External References

本次纯内部代码追踪，无需外部资料；未引用或核验第三方API新版本。

## Caveats / Not Found

- 不能根据截图百分比确认根因。未发现“关闭反色直接增加CPU连接”的代码路径；并不代表GPU或输入不存在问题。
- 源码索引分析与2142次等价推演不等于实际GPU像素验证。
- 未修改代码、阈值、配置；只写本研究文件。未执行Git操作、构建、GUI或提交。
- 连续曲率/side约束均有限数值采样，不是解析全形状保证；最终定位需同一完成事务真实pre/post数据。
