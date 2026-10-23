# Inkeys3：实现 Scroll Follow 实时可视化诊断窗口

## 一、任务背景与目标

请在 `Alan-CRL/Inkeys` 仓库最新的 `feature/move` 分支中，开发一个用于 **Scroll Follow（视图调整/滚动批注）** 的实时可视化调试窗口。

当前已有功能使用：

- Magnification API + PrintWindow 采集底层桌面画面。
- OpenCV 稀疏光流追踪和运动一致性估计。
- 特征点接力、累计位移、置信度和失败暂停。
- Draw3 Viewport 实现墨迹跟随。
- 主栏 ViewAdjust 按钮控制开始、结束及窗口穿透。

但人工测试发现，正常速度下很容易跟丢，反向滚动、局部滚动区域和白纸黑字文档也容易导致失败。

目前尚不能充分确定每次失败的具体原因。

因此，本次任务的目标是：

**建立一个属于 Inkeys 主程序的独立 Win32 实时调试窗口，直观显示 OpenCV 究竟观察到了什么、匹配了什么、为什么接受或拒绝某一帧，以及墨迹实际跟随的运动估计。**

这不是正式用户功能，而是后续改进 OpenCV 算法的诊断基础。

### 重要限制

1. **本次不修改任何追踪算法的判断结果。**
2. 不调整角点数量、光流参数、覆盖率、置信度、暂停条件等。
3. 不实现新的滚动区域自动识别算法。
4. 不修改 Draw3 的 GPU Renderer、Viewport、UInk、窗口穿透语义。
5. 不引入另一套屏幕捕获方式。
6. 不创建独立调试 EXE，不使用 `cv::imshow()`。
7. 调试窗口必须在 Inkeys 进程中，由 Inkeys 自身创建和管理。
8. 关闭调试功能后，正常 Scroll Follow 的行为及性能应与现有版本一致。
9. 不需要为了调试窗口提升整个程序的 Windows 最低版本要求。

允许为了获得调试信息，对 `VisualMotionTracker` 增加只读的诊断数据输出，但不能因此改变原来的算法决策。

---

## 二、首先调查当前实现

请先阅读最新分支，不要按照旧版本的设计文档直接假定当前实现。

重点调查：

### 1. OpenCV

- `Inkeys/Inkeys/CV/VisualMotionTracker.cpp`
- `VisualMotionTracker.h`
- `InkeysHeadlessTests/visual_motion_tracker_tests.cpp`

确认实际流程：

`GFTT → LK Forward → cornerSubPix → LK Backward → FB Filtering → Motion Consensus → NonTranslation / Ambiguity / Coverage → Anchor Consistency → Feature Refill`

调查各阶段有哪些现成中间数据，哪些失败路径会提前 `return`，需要在哪里提取信息才能呈现拒绝之前的真实匹配结果。

### 2. 捕获与产品集成

- `Inkeys/Inkeys/Graphics/FreezeCapture.cpp/.h`
- `Inkeys/IdtMagnification.cpp`
- `Inkeys/Inkeys/Business/ScrollFollow.cpp/.h`

重点确认 Mag 帧的生命周期、WorkArea ROI、Source Rect、时间戳、Capture Sequence、PrintWindow 耗时，以及 Tracker 是如何在原 MagnifierThread 回调中执行的。

### 3. Inkeys 窗口与 UI

调查现有：

- Window Service
- `WindowRole`
- UI3 窗口创建与渲染管理
- 主栏工具状态
- `BuildCurrentFilterList()`
- `MagSetWindowFilterList()`
- `Draw3.DiagnosticOutput`

选择一个与现有窗口架构一致、依赖最少、不会占用 Draw3 渲染线程的实现方式。

调试窗口可以使用轻量 Win32/GDI、Direct2D 或项目现有的合适 UI 绘制能力，但不要为了它新建复杂的渲染框架。

请先创建 Trellis 任务，简短记录调查结论，再分阶段实现。

---

## 三、实现独立的 Inkeys 调试窗口

建议窗口名称：

**Inkeys — Scroll Follow Debugger**

### 基本行为

通过新的命令行参数启用：

`--scroll-follow-debug-window`

现有 `--scroll-follow-console` 和 `--scroll-follow-verbose` 继续独立工作，不被替换。

窗口要求：

- 由 Inkeys 自身创建，具有独立 HWND。
- 支持调整大小，内部内容自动适应。
- 不需要始终置顶。
- 不影响 Inkeys 主栏和画布的操作。
- 可以在 Tracking / Paused 状态持续查看诊断。
- 关闭窗口只停止可视化诊断，不退出 Inkeys，不影响正在进行的 Scroll Follow。
- 可多次创建、关闭，不泄漏 GDI、D3D 或窗口资源。
- 没有启动参数时，不创建窗口或运行新的可视化任务。
- 不修改普通用户的窗口配置及启动行为。

优先复用现有 Window Service 的所有权、创建、销毁及线程规则。

如果新增 `WindowRole::ScrollFollowDebug` 合适，可以使用该方式；不要为调试功能另造与 Window Service 并行的窗口生命周期系统。

### 最重要：Magnification API 排除

**调试窗口必须加入 Inkeys 的 Mag 排除列表，不能出现在自己的截图中。**

要求：

1. 调试 HWND 创建后，能够被 `BuildCurrentFilterList()` 正确识别。
2. 每次 Mag 更新过滤列表时包含当前有效的调试窗口 HWND。
3. 调试窗口关闭或重建后，不保留失效 HWND。
4. 不能只根据进程名判断排除，因为窗口角色和生命周期必须准确。
5. 不修改现有定格窗口和画布的排除规则。
6. 不通过 `WDA_EXCLUDEFROMCAPTURE` 等影响系统录屏的全局办法实现。
7. 明确验证：即使调试窗口覆盖在浏览器正文上，Mag 原始截图仍不会包含调试界面。

如果过滤机制不能可靠排除新增窗口，必须优先解决此问题，不能以缩小或移动调试窗口作为最终替代方案。

---

## 四、调试窗口整体布局

希望采用比较专业、紧凑的开发者诊断界面，而不是简单的控制台文字堆叠。

建议采用可调整比例的布局：

```text
┌──────────────────────────────────────────────┐
│ Inkeys — Scroll Follow Debugger              │
├──────────────────────────────────────────────┤
│ Capture: 6.8 FPS  │ Tracking │ Frame #102    │
│ Motion: (0,-38.5) │ Total: (0,-524)          │
├─────────────────────────────┬────────────────┤
│                             │ Tracking       │
│     真实 Mag 图像            │ Accepted/Reject│
│                             │ Confidence     │
│   + 角点 / 光流 / 网格       │ Feature Count  │
│   + 候选运动范围            │ Spatial Support│
│                             │ Coverage       │
│                             │ Relay          │
│                             │ Timing         │
├─────────────────────────────┴────────────────┤
│ Recent events / Frame diagnostics            │
├──────────────────────────────────────────────┤
│ View options  │ Freeze Preview │ Save Debug  │
└──────────────────────────────────────────────┘
```

具体布局可以依据现有 UI 技术和实现成本调整，但应保留三个核心区域：

**实时图像预览、跟踪详细信息、最近的决策事件。**

### 窗口体验

建议默认将图像预览作为最大区域。

图像必须保持原始宽高比，不能非等比拉伸。窗口缩放时，所有特征点和箭头也应使用相同的坐标映射。

可以加入基本的缩放与平移，以便查看细小特征。

调试界面不要求与 Inkeys3 主栏完全一致的复杂视觉效果，清晰、稳定、实用优先。

---

## 五、实时图像预览：必须使用真实 Mag 帧

预览中显示的是**真正提交给 VisualMotionTracker 的图像**，而不是使用其他截图接口重新截取屏幕。

注意：

- `Capture::Frame` 的 BGRA 数据是借用缓冲，只在同步回调内有效。
- 不允许调试窗口保留其原始指针。
- 若需要异步绘制，必须复制到有独立生命周期的图像缓冲。
- 调试窗口不能额外调用一次 Mag 或 PrintWindow 取图。
- 不得在 Mag 回调内执行 UI 绘制或等待窗口消息。
- 可以采用有界双缓冲或 Latest-Only 的调试帧队列。
- 调试消费落后时可以丢弃**调试预览帧**，但不能影响正式 Tracker 的处理顺序。

明确区分原始捕获完整区域、WorkArea 裁剪区域和 OpenCV 半分辨率处理区域。

所有图像标记必须正确映射回当前预览的像素坐标，注意裁剪偏移和降采样比例，不得重复进行 DPI 换算。

### 1. 可视化特征点

显示：

- 当前有效追踪点。
- Forward LK 成功点。
- FB 校验失败点。
- Motion Inlier。
- Motion Outlier。
- 当前接受的主运动。
- 与主运动竞争的其他运动。
- 新加入的特征点（Feature Refill）。

建议采用不同颜色：

- 绿色：当前主运动有效内点。
- 灰色：静止或背景特征。
- 橙色：其他运动候选。
- 红色：匹配失败或被拒绝的特征。
- 青色：新补充的特征点。

颜色只用于诊断显示，不参与算法判断。

不必将每个失效角点长期显示在画面中，可以根据当前帧的分类进行渲染。

### 2. 光流箭头

显示每个特征从上一可信参考位置到当前图像位置的位移向量。

箭头长度和方向应当反映真实计算结果。

可以有一个仅用于显示的箭头倍率选项（例如 1× / 2× / 4×），帮助观察小位移，但数值统计仍使用原始位移。

用户应能够直接观察：

- 为什么缓慢向上滚动时出现负 Y 位移。
- 为什么某些固定区域没有移动。
- 反向滚动时，方向是否正确变化。
- 某些特征是否错误匹配到了另一行文字。
- 是否存在两组相互矛盾的运动。

### 3. 网格与运动支持范围

当前 Tracker 使用 8×6 网格，请提供可开关的网格显示。

至少显示：

- 当前有特征的网格。
- 有 FB 有效点的网格。
- 支持主运动的网格。
- 支持其他运动的网格。
- 内点位置的实际外接范围。

这里有一个重要要求：

**当前代码还没有真正实现“自动锁定滚动窗口”。因此不要将特征点的外接矩形标注成已经识别的滚动客户区。**

应该明确称为：

- Main Motion Support Bounds
- Candidate Motion Region
- Inlier Coverage

或者相应中文名称。

它只表示已有证据支持的范围，并不等于真实 Word 纸张或浏览器客户区的边界。

对于运动候选，如果可以在不修改算法决策的情况下根据现有 Matches 推算运动簇，可仅用于调试显示；不能将调试聚类结果反馈给生产运动估计。

---

## 六、右侧统计与状态

希望提供足够详细的信息，以便人工测试时立即知道发生了什么。

### A. 会话与采集

显示：

- Scroll Follow Session ID。
- Capture Session ID。
- Capture Frame Sequence。
- Tracker Reference Sequence。
- 当前状态：Off / Initializing / Tracking / Paused / Stopping。
- 当前采集区域及尺寸。
- 实际 Capture FPS。
- 相邻实际采样间隔。
- PrintWindow Read 耗时。
- Mag 捕获是否有效。
- Capture Accepted / Rejected / Skipped。
- 最近一次有效截图距离现在多久。

注意：Capture Accepted 与 Tracker Accepted 是两个概念，必须分开显示。

如果 Mag 没有成功送来新帧，窗口不能继续把最后一张旧图像标成实时画面。应保留最后图像供观察，同时显示 Stale / Capture Failure 及实际原因。

### B. OpenCV

显示：

- Attempted / Forward / Backward / FB Valid。
- Inlier Count。
- `pointInlierRatio`。
- `spatialInlierRatio`。
- `survivalRatio`。
- `fbMedian / fbP95`。
- `residualMedian / residualP95`。
- `runnerUpRatio`。
- `affineVariation / affineResidual`。
- `coverageX / coverageY`。
- `inlierCells / interiorCells`。
- `referenceCells / occupiedCells`。
- `initialSurvivors`。
- `added / retained / retired`。
- `consecutiveRejects`。
- 当前累计 Total Motion。
- 当前接受或拒绝的 Delta Motion。

提供最近一帧的：

`MotionReasonName(result.reason)`

例如：

`InsufficientCoverage`

`ForwardBackward`

`InconsistentTranslation`

`AmbiguousMotion`

`NonTranslation`

`JumpTooLarge`

### C. 显示实际拒绝条件

这是本次最重要的诊断能力之一。

不能只显示：

`Confidence = 0`

而应该显示：

```text
Rejected: InsufficientCoverage

Inlier Cells       12 / 16 required
Interior Cells      7 / 8 required
Coverage X        0.42 / 0.50 required
Coverage Y        0.51 / 0.40 required
```

或者：

```text
Rejected: InconsistentTranslation

FB Valid             143
Inliers               91
Spatial Support     0.61
Required Support    0.65
```

只展示该帧实际计算得到的统计，不要把没有执行到的检查结果显示成零并误称其失败。

必要时为 Debug Snapshot 增加：

- 最终通过的检查阶段。
- 第一个失败的检查名称。
- 该项的实际值与门限。
- 其他已经完成的检查结果。

**诊断信息必须来自生产算法当时的实际决策，而不是 UI 事后重新实现一份判断逻辑。**

### D. 性能统计

复用已有 `MotionStatistics` 的分段耗时：

- Preprocess
- OpticalFlow
- Fit
- Refill
- Total Tracker
- Capture Read

另外尽量显示图像捕获时间到调试窗口实际显示的年龄。

不要让调试窗口的帧率被误认为原始捕获帧率。

---

## 七、暂停与最后失败帧观察

当前 Tracker 连续几次拒绝后进入 Paused，后续不再自动恢复。

调试窗口应该专门支持观察这个状态。

### 要求

1. 进入 Paused 后不要清空图像与特征。
2. 保留最后一次失败的真实输入帧和相关调试信息。
3. 显示最后一次可信变换。
4. 显示暂停原因及连续拒绝次数。
5. 支持“Freeze Preview”，只冻结调试窗口显示，不停止真实 Inkeys 追踪。
6. 支持查看最近几条决策事件。

最好保留一个小型有界诊断历史，例如最近 20–30 个结果的数值摘要。

不需要保留几十张完整分辨率图片在内存中。

如果要保存最近图像，可先使用经过适当缩放的调试预览，避免内存占用明显增加。

### 诊断信息导出

建议提供一个显式操作：

**Save Debug Snapshot**

保存当前图像预览和对应 JSON/文本统计到用户选择的目录，方便后续给开发者分析。

如果实现简单且不会影响本次主目标，也可以加入短时录制，例如用户主动记录几秒的实际原始帧和 Tracker 结果，供原有回放器使用。

但不要让完整录制系统拖延实时调试窗口交付。

默认不自动保存任何屏幕图像，用户主动导出时才落盘。

---

## 八、线程与性能要求

调试功能不能使原本就偏慢的跟踪进一步明显恶化。

目前 Trellis 记录的真实 Mag 捕获大约为 6–7 FPS，并没有达到设计的 20 FPS。

因此本次应注意：

**不要在原 MagnifierThread 中同步完成图像绘制、窗口更新、磁盘保存或复杂的数据格式化。**

建议：

- Tracker 调用时在可选 Debug Sink 中记录必要的中间数据。
- 中间数据复制到独立的 `TrackerDebugSnapshot`。
- Mag 帧需要展示时，复制到有界预览缓冲。
- 独立窗口/UI 线程消费最新快照。
- 图像绘制频率可以低于 Tracker 频率。
- Debug 队列满时丢弃调试预览，而不阻塞正式算法。
- 复用缓冲区，避免每帧多次全屏分配。
- 没有开启诊断窗口时，不构造大规模特征调试数组。

建议保留原来 `VisualMotionTracker` 的独立性：不要让 CV 核心模块直接依赖窗口 HWND、UI3 或 Debug Window 类型。

如果需要增加新接口，应采用可选的数值诊断结构、Sink 或等价机制。

尤其注意：`MotionResult` 的拒绝路径可能在前向光流、FB、运动一致性、覆盖率等不同阶段提前返回。调试快照必须有合理的阶段完整性标识，避免访问不存在的中间数据。

---

## 九、分阶段实施

请按以下阶段顺序推进，不要一开始就同时修改窗口、算法、截图和全部测试。

### P0：调查与最小诊断设计

完成源码调查和 Trellis 任务初始化。

明确：

- HWND 创建和所有权。
- Mag Filter List 如何排除新窗口。
- Debug Snapshot 从哪里取得。
- 图像借用和复制规则。
- 哪些现有诊断数据可以直接复用。
- 哪些提前 Reject 需要增加只读诊断点。

输出简短结论后继续实施。

### P1：只读 OpenCV Debug Snapshot

先只实现诊断数据采集，不接入图形窗口。

验证：

- 初始化成功。
- 正常 Tracking。
- LK / FB Reject。
- Coverage Reject。
- AmbiguousMotion Reject。
- Paused。
- 特征接力。

确认开启或关闭 Debug Sink 时，原 `MotionResult` 的状态、位移、原因和累计结果完全一致。

不允许为了提供中间诊断而改变判断顺序或重新执行另外一份估计。

### P2：新建 Inkeys 窗口与 Mag 排除

创建独立 HWND、基本布局、生命周期和 Window Service 集成。

先通过最小窗口内容验证 Mag 排除是否有效。

保证不会因窗口显示而改变底层采集像素，也不会在正常使用时影响工具栏与画布穿透。

### P3：可视化图像、匹配与统计

接入真实 Mag 图像：

- 图像预览。
- 光流箭头。
- 角点分类。
- 网格。
- 主运动支持范围。
- 竞争运动。
- 状态和详细原因。
- 统计与最近事件。
- Freeze Preview。

这一步完成后，应该已经能由用户自行使用该窗口诊断正常滚动失败。

### P4：最小验证和交付

只做必要的定向测试：

1. Debug Snapshot 不改变算法结果。
2. Debug 关闭时没有额外截图和大量分配。
3. 新窗口成功加入 Mag Filter List。
4. 窗口关闭、重建不留下失效 HWND。
5. 诊断结果与真实 Frame Sequence 对应。
6. 发生 Reject 时保留实际信息。
7. Paused 状态仍可查看最后的有效诊断。
8. 运行和关闭调试窗口不影响 ViewAdjust 工作状态。

安排一次简短的真实 GUI 冒烟验证。

如果无法自动完成真实视觉验证，请保留可运行代码并明确列出需要人工完成的测试步骤，不要用 Headless 测试假装 GUI 已验证。

**不要重复进行数百秒的完整 OpenCV 长序列或全部 UInk 保存测试，除非本次确实修改了相关正式逻辑。**

如需完整构建，遵守仓库 `AGENTS.md` 的 MSBuild、ARM64 和相关依赖规则。

---

## 十、特别要求：控制任务范围与测试成本

本任务的核心交付是实时可视化诊断能力，而不是继续修复滚动算法。

请严格遵守：

- 不修改正式追踪模型和阈值。
- 不添加新的匹配算法或自动 ROI 检测。
- 不重构 Mag Capture。
- 不修改主栏现有 ViewAdjust 按钮行为。
- 不为调试窗口创建第二套渲染框架。
- 不运行大量重复、收益不明确的长测试。
- 不为了测试修改产品算法。
- 不创建无必要的大型测试夹具。
- 不自动推送、发布 PR 或合并分支。

优先复用现有 Trellis 测试与 `MotionStatistics`。

完整测试应在需要发布或合并前集中执行，本次重点保证调试窗口正确、稳定、可观察。

---

## 十一、最终验收目标

最终，我应该可以进行这样的操作：

1. 启动带 `--scroll-follow-debug-window` 参数的 Inkeys。
2. 打开 Edge 或 Word。
3. 启用主栏“视图调整”，开始穿透。
4. 看到新的 Inkeys 调试窗口实时显示 Mag 采集图像。
5. 滑动网页时，看到绿色/灰色/橙色等实际运动特征和光流箭头。
6. 看清哪些区域静止、哪些区域正在移动。
7. 如果跟踪失败，能够明确知道是 LK 失败、FB 失败、空间支持不足、覆盖范围不足，还是其他具体条件导致。
8. 长距离滚动时，可以观察旧特征退出与新特征补充。
9. 发生 Paused 后，仍可查看最后失败帧和详细原因。
10. 关闭调试窗口，不影响 Inkeys 的现有批注、穿透、定格和保存功能。

### 最终交付报告

请提供：

- 新增/修改文件。
- HWND 与 Window Service 集成方式。
- Mag 排除窗口的实现方式。
- Debug Snapshot 数据结构及线程所有权。
- 每种图像标记的含义。
- 各项统计与拒绝条件的来源。
- 调试窗口打开与关闭的使用方法。
- 实际测试结果及无法验证的部分。
- 性能影响的有限对照结果。
- 后续算法改进可直接利用的诊断数据。

**本次完成标准不是追踪性能提升，而是让我能够通过 Inkeys 自己的窗口，直观、准确地观察 Scroll Follow 内部发生的一切关键过程。**

请以此为目标完成实现。
