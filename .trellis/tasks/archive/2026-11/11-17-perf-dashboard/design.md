# 第2步-C 设计：设备性能看板

> 需求与已确认决策见 `prd.md`。本文件的行号以 HEAD `11cf0164` 为准，开工核对（implement 步骤 0）时复核。
> 缩写：C = `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`，S = `Inkeys/Inkeys/UI/Setting/Setting.cpp`。

## 1. 总体结构与数据流

```
绘制线程（帧尾）                        账本 DevicePerformanceLedger（Host 持有）            设置页（UI 线程）
──────────────                        ─────────────────────────────────                ─────────────────
每个计数帧：tally.Add(mask, workMs)                                                       每帧 Report(revision)
   │ 只做常数次加法，无锁无分配                                                           ├ 版本没变：沿用缓存
   ▼                                                                                     └ 变了：短锁复制报告
进入空闲等待前：ledger.Submit(tally, signature)
   │ 短互斥锁；合并进 “今天 × 签名” 日桶  ──►  重算报告缓存（revision+1）
   │ 清零 tally                              通知写盘线程（防抖 30 s，退出时立即）
   ▼                                              │
                                                  ▼
                                   <根目录>/Inkeys/Memory/device-performance.json
                                   （临时文件 + MoveFileEx 原子替换）
```

- **新模块** `Inkeys.Drawing.Draw3.device_performance`（`Draw3.DevicePerformance.cppm/.cpp`）：账本、签名、日桶、窗口汇总、JSON 读写、写盘线程。
- **纯逻辑头** `Draw3.DevicePerformanceModel.h`（无 Windows/D3D 依赖，仿 `Draw3.CompositionClock.h`）：`PerformanceTally`、`PerformanceDimension`、工具到维度映射、签名规范化、日桶合并与“最近 7 个使用日”汇总、档位与帧率建议。单测直接包含它。
- Host 持有账本（与 `DesktopAutoSaveService autoSave` 同级，Host.cpp:377），Controller 通过配置拿到裸指针（生命周期：账本先于绘制线程启动、在绘制线程 join 之后停止并落盘）。
- UI 只通过 `Host::DevicePerformance(uint64_t knownRevision, DevicePerformanceReport& out) -> bool` 读取（版本未变返回 false，不复制），与 `CanvasInformation()` 同样是“owner 发布、getter 短锁复制”。

## 2. 计数帧与维度

### 2.1 维度

```cpp
enum class PerformanceDimension : uint8_t { Writing, Highlighter, Eraser, Laser, Shape, Pan, Count };
```

| 维度 | 来源 |
| --- | --- |
| Writing | 活动 runtime 的 `tool` 为 `DrawingTool::Pen / HardPen` |
| Highlighter | `DrawingTool::Highlighter` |
| Eraser | `DrawingTool::Eraser`（固定、速度、手掌橡皮都在此） |
| Laser | `DrawingTool::Laser`；以及无接触时激光 `Hold/Fade` 阶段仍在出帧 |
| Shape | `SolidLine / DashedLine / OutlineRectangle / FilledRectangle / Shape` |
| Pan | `canvasPan.Active()`（拖动、缩放、惯性）；不含画布调试 probe |

- 维度掩码每帧由 `active` 列表现算（通常 1–几个 runtime），加上 Pan / 激光尾段两个布尔。映射函数放纯逻辑头，单测覆盖每个 `DrawingTool`。
- 一帧可带多个维度位（例如左手漫游、右手书写），各维度都计入；“总体”每帧只计一次。

### 2.2 计数条件（全部满足）

1. 本帧 `presentSucceeded`，且不是画布调试板自己的刷新帧（复用 C:23690 附近的 `debugOnlyFrame` 判定）。
2. 维度掩码非空（纯悬停光标、拒收接触节拍帧、空闲重绘不计）。
3. 不是选择模式（`selectionMode`）、本帧没有 `outputTargetChanged`、没有设备恢复（`graphicsRecoveryPending_` 处理帧）。这些是一次性的重帧，会把平均拉偏。
4. 画布调试可视化未开启（`GetCanvasDebugVisual()`；调试板与标记自身有可观光栅开销）。仅开调试数据不画标记时照常计数。
5. 记录开关开启（若 R3 批准；见 §7）。

### 2.3 工作耗时口径

与画布调试同一个值：`workMs = max(0, canvasFrameElapsedMilliseconds − frameLatencyWaitMs_)`，在 C:23697 `canvasDebugOverlay->OnFrame` 同一位置取得。`frameLatencyWaitMs_` 已含 DComp waitable 等待、ULW 即时模式 `Present` 内等 GPU（`ConsumePresentLatencyWait`）和 C1/C2 的 `Map` 阻塞；帧截止睡眠发生在此之后，天然不计入。

超预算帧：`workMs > 1000 / target_fps`，单独计数，用于档位修正（§5.1）。

## 3. 绘制线程累加（R1：逐帧累加）

```cpp
struct PerformanceCounter { uint64_t frames = 0; double workMs = 0; uint64_t overBudget = 0; };
struct PerformanceTally
{
    std::array<PerformanceCounter, 7> counters; // [0] 总体，[1..6] 各维度
    bool Empty() const noexcept { return counters[0].frames == 0; }
    void Add(uint32_t dimensionMask, double workMs, bool overBudget) noexcept; // 常数次加法
};
```

- tally 是 `Run()` 的局部变量（约 170 字节），绘制线程独占，**无原子、无锁、无分配**。
- **为什么不按原预告“每段只记前几帧”**：只记前几帧需要按段计数、判断截断，同样是每帧一次分支和写入；而逐帧累加只多几次浮点加法，开销同级甚至更低。截断反而让短笔与长笔权重相同、丢掉长时间书写中后段（GPU 升温、页面笔迹增多）的真实负载，“平均”就不再是用户实际感受到的平均。若用户坚持截断，也只需在 `Add` 前加段内计数判断，不影响其余设计。
- **移交点**：主循环进入真正空闲等待之前（C:22287 `completeUlwBeforeIdle()` 之后、C:22329/22349 `WaitForWake` 之前），`!tally.Empty()` 时调用 `ledger->Submit(tally, signature)` 并清零。另外 `Run()` 正常退出前移交一次。
- **兜底**：tally 帧数超过 1,000,000（约 2.3 小时 120 FPS 不停顿）时下一次移交前不再累加，防止 double 精度问题；实际不会触发。
- 在 Submit 内部任何异常（`bad_alloc`）都被吞掉并丢弃本段，记一条 Warn 日志；绘制线程不受影响。

## 4. 设置签名、日桶与窗口

### 4.1 签名（R4）

```cpp
struct PerformanceSignature
{
    uint16_t frameRate;          // 启动时生效的画布帧率 30/60/120/240
    uint8_t device;              // Hardware / Warp（实际，不是偏好）
    uint8_t presentation;        // DComp / Ulw / 其他实际模式（开工核对确认枚举）
    uint8_t waitable;            // 仅 DComp 有意义，其余模式规范化为“不适用”
    uint8_t ulwAsyncReadback;    // 仅 ULW 有意义
    uint8_t ulwCompositionAlign; // 仅 ULW 且异步读回开启时有意义
    uint8_t zhaoxinCompatibility;
};
```

- 由 Controller 在 Run 开头与每次呈现器初始化/恢复/回退后生成（实际模式可能因回退在运行中变化）；变化时先把当前 tally 以旧签名移交，再换新签名。
- **规范化**：与当前模式无关的选项写成“不适用”，避免切换一个不起作用的开关就把数据拆开。
- JSON 中签名保存为可读键（例如 `fps120-hw-dcomp-wait1-zx0`）加各字段，便于人工排查。
- 不包含：界面帧率（只影响设置/主栏）、“绘制时鼠标移动不受画布帧率限制”（即时生效，只改变鼠标光标节拍，不改变每帧工作量）、显示器与分辨率。

### 4.2 日桶

```cpp
struct PerformanceDayBucket
{
    int32_t date;                 // 本地日期 yyyymmdd，Submit 时 GetLocalTime 取得
    std::array<PerformanceCounter, 7> counters;
    int64_t updatedUnixSeconds;   // 最近一次合并时间
};
```

- 每个签名保存最近 **7 个使用日**的日桶（有计数帧才会产生日桶，自然就是“使用日”）；合并出第 8 个日期时淘汰最早的一个。
- 跨午夜的 tally 整段计入 Submit 当天（段通常只有几秒，误差可忽略）。
- 系统时间被往回调：日期比已有最新日桶更早时仍按日期插入并排序，超出 7 个淘汰最早；不报错。
- 签名上限 8 个，超出淘汰 `lastUsed` 最旧的签名。文件最大约 8 × 7 × 7 个计数器，几十 KB 以内。

### 4.3 报告（看板读取的结果）

```cpp
struct PerformanceFigure { bool available; uint64_t frames; double loadPercent; double unlimitedFps; double overBudgetRatio; bool lowSample; };
struct DevicePerformanceReport
{
    uint64_t revision;
    enum class Source : uint8_t { None, CurrentSignature, PreviousSignature } source;
    PerformanceSignature signature;   // 报告所属签名（含测得时的帧率）
    PerformanceFigure overall;
    std::array<PerformanceFigure, 6> dimensions;
    uint32_t usageDays;               // 1–7
    int32_t firstDate, lastDate;      // 统计范围
    int64_t evaluatedUnixSeconds;     // 最近一次合并（评估时间，精确到秒）
    uint32_t coverageMask;            // 已覆盖维度
    bool recordingEnabled;
    uint32_t displayRefreshHz;        // 当前会话画布显示器模式刷新率（不入盘，见 §6.5），0 = 未知
};
```

- 计算：`loadPercent = (ΣworkMs / Σframes) / (1000 / signature.frameRate) × 100`；`unlimitedFps = Σframes / (ΣworkMs / 1000)`；跨日按帧数加权（直接对计数器求和）。
- 选择签名：当前签名有数据 → `CurrentSignature`；否则取 `lastUsed` 最新的其他签名 → `PreviousSignature`；都没有 → `None`。
- `lowSample`：总体 < 600 帧或维度 < 120 帧（约 5 s / 1 s @120 FPS）时为 true，UI 显示“样本较少，仅供参考”。
- 报告只在 Submit / 加载完成 / 清除时重算，UI 不做聚合。

## 5. 档位与帧率建议（纯函数，单测矩阵）

输入：报告的 `avgWorkMs`、`overBudgetRatio`、测得帧率 `measuredFps`（签名帧率）、配置中的画布帧率 `configuredFps`、画布所在显示器刷新率 `refreshHz`（UI 线程读取，见 §6.4）。

### 5.1 档位（R2 建议值）

| 档位 | 负载 L | 颜色 | 结论文案（示意） |
| --- | --- | --- | --- |
| 充裕 | L < 50% | 绿 | 设备非常轻松，当前帧率下还有大量余量 |
| 适中 | 50% ≤ L < 80% | 灰 | 设备负载适中，当前帧率运行稳定 |
| 偏紧 | 80% ≤ L < 100% | 黄 | 设备负载偏高，复杂场景下可能偶尔掉帧 |
| 超载 | L ≥ 100% | 红 | 设备跟不上当前帧率，书写可能卡顿或延迟 |

修正：超预算帧占比 > 10% 时档位至少为“偏紧”，并追加“平均负载不高，但约 x% 的帧超时，可能偶尔卡顿”。

### 5.2 建议规则

预设档 `P ∈ {30, 60, 120, 240}`；预计负载 `L(P) ≈ avgWorkMs × P / 10`（%，按每帧工作量不变近似，文案用“约”）；`matchFps` = 不超过刷新率的最大预设（刷新率未知按 60）。

按顺序取第一条命中：
1. **建议降低**：L ≥ 80% → 在低于 `measuredFps` 的预设中取 `L(P) ≤ 80%` 的最大者：“建议降低到 P FPS”；若没有：“即使最低帧率也较吃力，请确认已开启硬件加速，或关闭占用 GPU 的程序”。
2. **超时帧修正**：L < 80% 但超时帧 > 10% 且 `measuredFps > matchFps` → 建议降到 `matchFps`。
3. **建议升高**：`measuredFps < matchFps`，在 `(measuredFps, matchFps]` 中取 `L(P) ≤ 70%` 的最大者：“可以升高到 P FPS，与显示器 R Hz 一致时书写最流畅”。
4. **已匹配且余量大**：`measuredFps == matchFps` 且上一档 `L(P) ≤ 50%`：“已与显示器刷新率一致。也可以升到 P FPS：画面不会更流畅，但墨迹会更精确、平滑”。
5. **高于刷新率**：`measuredFps > matchFps`：“当前帧率高于显示器（R Hz），画面不会更流畅，但墨迹更精确、平滑；设备负载允许保持”。
6. 其余：“当前帧率设置合适”。

附加说明：`configuredFps ≠ measuredFps` 时追加“画布帧率已改为 X FPS，重启后生效并重新积累数据”；`source == PreviousSignature` 时标题下注明“设置变更前的数据”。建议永远不超过 240，且“升高”永远不超过 `matchFps`（与用户要求“最高建议与显示器一致”一致；规则 4 只是说明可选，不是建议）。

## 6. 看板界面

### 6.1 位置与绘制方式

- 插入点：S:4627 `PageHeader("性能")` 之后、S:4628 第一个 `SectionHeader(FrameRateSection)` 之前；整张卡片包在 `PageBlockScope("##perf-dashboard")`（`Setting.Motion.h:65`）里，参与页面依次入场动画。性能页不调用 `PageContentStart()`，保持现状。
- 新文件 `UI/Setting/Setting.PerformanceDashboard.cpp/.h`：`void RenderPerformanceDashboard()`，Setting.cpp 只加一行调用，避免继续膨胀 6000 行的 Setting.cpp。
- 卡片宽度 = 页面内容宽（最大 1040 DIP）。背景用 draw list：`ChannelsSplit(2)`，内容画在通道 1，`EndGroup` 后取组矩形，回到通道 0 画 `AddRectFilled(..., tint, Pixels(8))` + 1 px `AddRect` 描边，再 `ChannelsMerge`。这样展开动画改变高度时背景自动跟随，无需预估高度。圆角与描边沿用 `HeroBackground`（`Setting.Pages.cpp:100`）风格；不做渐变（四档纯色更易辨认）。
- 内边距 20 DIP；行距沿用 `RowGap`/`SectionGap`；卡片与下方第一节之间 `SectionGap`。

### 6.2 收起态内容（自上而下）

```
┌──────────────────────────────────────────────────────────────────────────┐
│ 设备性能   [充裕]                                              [详情 ⌄]   │  Subtitle 20 + 档位胶囊（accent 色） + 展开按钮
│                                                                          │
│ 42 %        平均负载                                                     │  数字 Display 68 DIP，“%” Title 28
│             无限制帧率 约 285 FPS · 测得于 120 FPS                        │  Body，次要色
│                                                                          │
│ 设备非常轻松，当前帧率下还有大量余量。                                    │  结论：Body 加粗
│ 已与显示器刷新率（120 Hz）一致。也可以升到 240 FPS：画面不会更流畅，      │  建议：Body，自动换行
│ 但墨迹会更精确、平滑。                                                    │
│                                                                          │
│ 评估于 2026-11-18 14:03:27 · 最近 5 个使用日（11-12 至 11-18）            │  Caption 12，次要色
│ 已覆盖：书写 · 橡皮 · 激光笔          [样本较少，仅供参考]                 │
└──────────────────────────────────────────────────────────────────────────┘
```

- 大数字测量用 `CalcTextSize`（`Metrics()` 不含 Display/TitleLarge 行高，见 `Setting.Controls.cpp:360`）。
- 数字变化时用 `NavigationTween` 在 0.35 s 内从旧值过渡到新值（`AnimationEnable` 关闭时直接跳变）；显示取整数百分比，≥ 1000% 显示“999+%”。
- 来源为上一签名时，标题行右侧（展开按钮左边）显示“设置变更前”小标签，结论下追加说明。
- 无数据：中性卡片，数字位置显示“—”，结论改为“画几笔后，这里会显示这台设备绘制画布的轻松程度”，不显示展开按钮。

### 6.3 展开态（动画）

- 展开按钮：`IconButton` 风格，文字“详情 / 收起” + 箭头；箭头随展开进度 0→180° 旋转（复用 `RenderChevronLerp` 思路；该函数是文件内静态，必要时在 `Setting.Controls.h` 导出最小接口）。整行标题区域也可点击。
- 展开内容放在 `ConditionalBlock("##perf-dashboard-details", expanded)`（`Setting.Motion.h:86`）中：已有高度动画、裁剪、淡入、折叠时禁用输入与滚动修正，时长随“动画速度”设置；中途反向从当前高度继续。
- 展开状态仅本次设置窗口会话内保存（`ImGui::GetStateStorage`），默认收起。
- 内容：分隔线后 6 行，每行：

```
书写        ███████░░░░░░░░░░░░   38 %    约 315 FPS
荧光笔      ████░░░░░░░░░░░░░░░   21 %    约 570 FPS
橡皮        ████████████░░░░░░░   64 %    约 187 FPS
激光笔      暂无数据
图形        ██████░░░░░░░░░░░░░   33 %    约 363 FPS
漫游        ███████████████████▌ 112 %    约 107 FPS    ← 超过 100% 条形填满并以红色显示
```

  - 条形：圆角轨道（次要填充色）+ 按该维度档位 accent 色的填充，长度 = min(L, 100%)；超过 100% 时整条填满、末端画一个缺口标记。条形长度变化同样做 0.35 s 过渡。
  - 维度样本较少时数值用次要色并加“·样本少”。
- 展开区底部（若 R3 批准）：`ToggleRow` “记录设备性能数据”（说明：“只记录绘制耗时等统计数字，保存在本机，不上传”）+ 右侧“清除数据”按钮（两段式确认：首次点击变为“确认清除”，3 s 内再次点击生效）。

### 6.4 配色

- `Palette`（`Setting.Controls.h:20-31`）新增两组 token：`performanceFill[5]`（None/充裕/适中/偏紧/超载 的卡片底色）与 `performanceAccent[5]`（胶囊、条形、档位文字）；浅色在 `Setting.Controls.cpp:17-56`、深色在 `57-96`；Acrylic 背景下与 notice 一样调 alpha（`Setting.Controls.cpp:2509-2527`）。
- 建议初值（实机再调）：

| 档位 | 浅色底 | 浅色 accent | 深色底 | 深色 accent |
| --- | --- | --- | --- | --- |
| 无数据 | 卡片默认底色 | 次要文字色 | 卡片默认底色 | 次要文字色 |
| 充裕（绿） | (235,246,238) | (15,123,15) | (39,58,41) | (108,203,95) |
| 适中（灰） | (241,241,243) | (110,110,118) | (48,48,52) | (170,170,178) |
| 偏紧（黄） | (255,244,206) | (157,93,0) | (64,56,31) | (252,225,0) |
| 超载（红） | (253,231,233) | (196,43,28) | (68,39,38) | (255,153,164) |

  绿/黄沿用现有 `notice` 背景色，accent 对齐 ImFluent `SystemFillSuccess/Caution/Critical`，保证与设置页其它提示一致。档位切换时底色用 `AnimateColorU32` 同款指数过渡。

### 6.5 数据读取与刷新率

- UI 每帧调用 `ProductHost().DevicePerformance(cachedRevision, cache)`；只有版本变化、配置画布帧率变化或刷新率变化时才重算档位与建议文案（§5 纯函数），文案用 `ConditionalBlock::RetainText` 以免折叠动画中文字跳变。
- `refreshHz`：由 Host 在报告中附带“画布所在显示器的模式刷新率”（呈现层初始化/Resize/显示变化时已用 `EnumDisplaySettingsW` 取得，见 `Draw3.TransparentPresentation.cpp:1479-1495`；优先显示模式频率而不是 DWM 实时周期，避免动态刷新率在空闲时报 60 Hz），取不到时 DWM，再取不到按 60。
- `configuredFps` 读 `config.Drawing.FrameRate`（设置页现有值）。

## 7. 记录开关与清除数据（R3，待确认）

- 配置键 `Drawing.DevicePerformanceRecording`（bool，默认 true，NoUpload，即时生效）：Host setter 写原子量，Controller 在移交点与 Run 开头读取；关闭时 tally 不累加，已有数据保留，看板注明“记录已暂停”。
- “清除数据”：`Host::ClearDevicePerformance()` → 账本清空内存与报告（revision+1），写盘线程删除文件；需二次确认（沿用设置页现有确认样式，若无则用按钮两段式“再次点击确认”）。
- 去掉原“周期长度可选”；窗口固定 7 个使用日。

## 8. 持久化

- 路径：`GetCurrentExeDirectory() + L"\\Inkeys\\Memory\\device-performance.json"`，由 `IdtMain.cpp`（C 侧 `autoSaveRoot` 旁）经 `HostStartOptions.devicePerformancePath` 传入；空路径 = 只在内存记录（测试用）。
- 格式（jsoncpp，Draw3 已在用）：

```json
{
  "schema": "inkeys.device-performance",
  "version": 1,
  "signatures": [
    {
      "key": "fps120-hw-dcomp-wait1-zx0",
      "frameRate": 120, "device": "hardware", "presentation": "dcomp",
      "waitable": 1, "ulwAsyncReadback": -1, "ulwCompositionAlign": -1, "zhaoxin": 0,
      "lastUsed": 1763450430,
      "days": [
        { "date": 20261118, "updated": 1763450430,
          "all": [frames, workMs, overBudget], "writing": [...], "highlighter": [...],
          "eraser": [...], "laser": [...], "shape": [...], "pan": [...] }
      ]
    }
  ]
}
```

- **写盘线程**：账本自带一个 `std::thread`，条件变量等待“脏 + 30 s 防抖”或 Stop；写到 `device-performance.json.tmp` 后 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`；目录不存在则创建。失败记 Warn，下次脏时重试。
- **加载**：写盘线程启动后先读文件，与启动后已 Submit 的内存数据按“签名 × 日期”相加合并（加法可交换，不需要等待加载完成才允许 Submit）；加载完成后重算报告。
- **坏文件**：解析失败、schema/version 不符、字段类型错误 → 重命名为 `device-performance.json.bad`（覆盖旧 .bad），从空开始，记 Warn；单个字段非法（负数、NaN、超范围日期）只丢弃该日桶。
- **退出**：Host Stop 在绘制线程 join 之后调用账本 Stop：最后一次 Submit 已在 Run 退出前完成，Stop 立即写盘再 join 线程。异常退出可能丢失最多 30 s 的合并结果，可接受。

## 9. 线程与生命周期

| 对象 | 所有者 | 访问 |
| --- | --- | --- |
| `PerformanceTally` | 绘制线程 Run 局部 | 仅绘制线程 |
| 账本内存数据、报告缓存 | 账本（`std::mutex`） | Submit（绘制线程空闲时）、写盘线程（快照复制后在锁外序列化）、UI getter（短锁复制） |
| 记录开关 | `std::atomic<bool>` | UI 写，绘制线程读 |
| 文件 | 写盘线程独占 | — |

- 锁内不做 I/O、不做序列化；写盘线程在锁内复制数据结构（几十 KB 内），锁外序列化与写文件。
- Host 未启动或画布初始化失败：账本仍可启动加载历史数据（看板显示历史），只是没有新数据；开工核对时确认 Host 生命周期允许这样做，否则看板显示空态。

## 10. 测试

- **纯逻辑单测**（`InkeysHeadlessTests/draw3_device_performance_tests.cpp`，`--device-performance-only`，在 `animation_tests.cpp` 注册）：
  - 工具/Pan/激光尾段 → 维度掩码；多维度帧总体只计一次。
  - 签名规范化：不相关开关不拆签名；相关开关拆签名。
  - 日桶：同日合并、跨日、第 8 个使用日淘汰最早、时间回拨、签名上限淘汰。
  - 报告：当前签名 / 回退到上一签名 / 无数据；加权平均；`lowSample`。
  - 档位与建议矩阵：§5 每条规则至少一例，含刷新率 60/120/144/未知、帧率已改未重启。
  - JSON 往返；坏文件改名后从空开始；单桶非法字段被丢弃；加载与启动后 Submit 的合并。
- **控制器侧**：在现有无窗口测试中可行则补一个“计数条件”测试（选择模式、恢复帧、悬停帧不计）；不可行则在 implement 中注明由实机验证。
- **不做**：GUI 自动化（遵守“不启动设置窗口”）。看板视觉、动画、深浅主题由用户实机验收。

## 11. 风险

- 签名中的“实际呈现模式”若在运行中频繁切换（选择模式用 SelectionUlw），只取主画布模式，选择模式帧本就不计数。
- 负载百分比是 CPU 侧墙钟工作耗时；GPU 重而 CPU 轻时，等待 GPU 已从工作耗时中扣除，看板可能偏乐观——与画布调试同口径，属已知局限，在文案中不承诺“GPU 占用”。
- 预计负载按每帧工作量不变线性外推，换帧率后实际值会有偏差；建议文案统一用“约”，并在切换后由新签名的真实数据纠正。

## 12. 开工核对后的实现调整（2026-11-18，以本节为准）

开工核对（5 路只读核对，结果存于会话临时目录）与用户中途要求带来的调整。上文与本节冲突时以本节为准。

### 12.1 文件与模块形态
- 不做 module：纯逻辑 `Draw3.DevicePerformanceModel.h`（维度、累加、签名、日桶、报告、档位与建议）+ `Draw3.DevicePerformanceJson.h`（jsoncpp 序列化/解析，纯字符串进出）+ `Draw3.DevicePerformanceLedger.h/.cpp`（账本与写盘线程，普通 TU）。Host.h 直接 include 纯逻辑头（传统头不能使用 module 类型）；Controller 在 cppm 全局片段 include 账本头。
- 账本 .cpp 同时编入 `InkeysHeadlessTests`，文件级测试用 `WaitLoaded` 与同步 `Stop` 做确定性断言。
- Controller 通过构造函数尾参数 `DevicePerformanceLedger*` 拿账本（仿 metrics/haptics），不放进 `StrokeModelConfiguration`。

### 12.2 计数（替代 §2.2、§3）
- 维度掩码在 `if (hasEndedStroke)` 之前、结束笔画移出 `active` 之前计算，跳过 `palmObservationOnly`、`cancelled`；否则最后一根接触的抬笔帧会漏计。
- **不排除 `selectionMode`**：白板移动模式本身就是选择模式，漫游只在其中发生。改为排除 `selectionUsesAuxiliary`（桌面/PPT 选择模式辅助输出）与 `outputTargetChanged`。也不得用 `forceFullPresent` 当排除条件（每个平移帧都为 true）。
- 一次性重帧：新增帧内局部 `performanceHeavyFrame`，在设备恢复、合成变化、Resize、共享层重建、画布命令出队处置位（`graphicsRecoveryPending_` 到帧尾已被清零，不能用）。
- Pan 位取 `navigationInProgress || canvasPan.Active()`（惯性末帧 Step 后已为 false）；激光 `Hold/Fade` 阶段补 Laser 位。
- `debugOnlyFrame` 提到 `if (canvasDebugOverlay)` 块外为 `canvasDebugOnlyFrame`，块内沿用。
- 空闲移交只有一处：循环顶部真正空闲分支 `completeUlwBeforeIdle()` 成功之后（覆盖激光 Hold 与完全空闲两个子分支）；Run 正常退出收尾处再移交一次。

### 12.3 签名来源（替代 §4.1 的“事件点生成”）
- 呈现层新增 `TransparentPresentationController::EffectiveState()`：实际模式、`warpDevice`、`waitableWait`（DComp 下句柄存在且 gate 启用）、`ulwAsyncReadback`（未熔断）、`ulwCompositionAlign`（且 DWM 正在合成）。Renderer 新增 `UsesZhaoxinCompatibleUpload()`（兆芯兼容实际生效值）。
- 每个计数帧读一次生效状态并与当前签名比较，不同则先以旧签名移交再切换。这样可以覆盖恢复、回退、waitable/ULW 熔断、设备丢失转 WARP 和合成变化，不依赖事件点。Run 开头先 `SetCurrentSignature`，未画任何一笔时看板也能区分“当前 / 设置变更前”。

### 12.4 刷新率（替代 §6.5）
- 呈现层在 DComp 下不计算刷新率，ULW 下以 DWM 周期为主。改由 `Host::Impl::PumpDisplayScale`（绘制 owner，启动与显示变化时运行）对画布所在显示器调用 `GetMonitorInfoW + EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS)` 取 `dmDisplayFrequency`，交给账本（报告版本 +1）；取不到发布 0，界面按 60 Hz 处理。
- `MatchFrameRate` 留 2 Hz 容差：59/119/239 Hz 视同 60/120/240。

### 12.5 写盘与记录开关（用户 2026-11-18 中途要求）
- 帧内只做计数器加法，绝不写文件。本段第一个计数帧调用 `MarkBusy()`（一次原子写，并返回当前清除代次作为本段代次）；空闲移交 `Submit` 时清除忙碌标志，并在锁内复查记录开关与本段代次，关闭记录或段内清除过数据时整段丢弃。写盘线程只在**不忙碌**、距上次写盘 ≥ 30 s 且记录开启时写；Stop 时（绘制线程已 join）立即写最后一次。
- 关闭记录：帧内不算维度、不累加、不移交（移交时若已关闭则丢弃本段，只清忙碌标志），写盘线程也不写（包括退出时）。已有数据保留，看板显示“记录已暂停”。“清除数据”是用户主动操作，仍会删除文件。
- 同一进程 Stop→Start（DComp 失败后以 ULW 重启 Host）时同一路径不重读文件，避免历史翻倍。加载遇到 I/O 错误（文件存在但读不出）时本会话禁止写盘，避免覆盖历史。账本停止后（关机时设置页晚于 Host）`Clear` 在调用线程同步删除文件。
- 原子写复用 `Inkeys::PptSettings::WriteAtomically`（唯一临时文件名，多实例安全）；坏文件改名 `.bad`。
- 程序目录取不到时传空路径（只在内存记录）。

### 12.6 设置页（替代 §6.1–§6.4 中的对应做法）
- `Setting.PerformanceDashboard.h/.cpp` 采用 Input/Events 模式，不依赖 Host。`Setting.cpp` 读取 `ProductHost().DevicePerformanceSnapshot(revision, report)`、配置帧率与记录开关，并传入 `navigationDelta`、`AnimationEnable`、`AnimationSpeedRate`。
- 卡片前调用 `PageContentStart()`；卡片后不另加 SectionGap（下一个 SectionHeader 自带）。
- 背景不用 `ChannelsSplit`（嵌套条件块会退回逐顶点淡化，违反 setting-motion.md），改为先画背景，内容画完后按真实高度平移背景下半部顶点（与 HeroBackground 改顶点同类）；高度取最后一个 Dummy 的 `GetItemRectMax().y`，不用 Begin/EndGroup。
- 底色过渡：`NavigationTween` 的 0→1 进度，在固定的起点色与目标色之间插值（不用 `AnimateColorU32` 那种逐帧指数逼近，它是 setting-motion.md 列出的反例）。数字、条形、箭头都用 `NavigationTween`，时长除以动画速率，并受动画开关控制。
- 展开按钮：透明 Button + 文字 + `DrawIcon` 顶点旋转（照 Shell.cpp 导航组箭头写法），不导出 Controls 内部静态函数。标题行其余区域不可点击（与 §6.3 不同，避免重叠项）。
- 卡片内记录开关用 `ToggleAction` 手动布局（ToggleRow 会画出卡中卡）；清除按钮为两段式（3 s 内再次点击生效，收起时复位）。
- Acrylic 下 `performanceFill` 与 heroFill/featureTint 一样调到 alpha 179；“无数据”底色运行时取 `GetPalette().card`。绿/黄/红底色与 notice 成功/警告/严重一致。
- 文案中不使用字体覆盖未经验证的符号（不用 “—”、“…”），无数据时数字位显示 “--”；格式化统一包 try/catch，模板出错时回退为原文。看板的数字模板已登记进 `IdtTest::RunI18nLocaleTests`。

## 13. 文案轮精简与调试预览（2026-11-18，以本节为准）

用户实机后要求删除大量说明文字，并在软件调试页加入预览。
- **删除帧率建议**：`Suggestion`/`Advice`/`Advise`/`MatchFrameRate`/`PredictLoadPercent` 及其常量与单测；Host 中仅为建议采集的显示器刷新率（`PumpDisplayScale` 的 `EnumDisplaySettingsW`）与账本 `SetDisplayRefreshHz`；报告的 `displayRefreshHz`、`sessionFrameRate`。§5 与 §6.5 作废。档位仍由 `ClassifyFigure`（含超时帧 > 10% 至少偏紧）决定，只是不再输出文字。
- **收起态**：标题与标签、大数字、“无限制帧率 约 {0} FPS”（去掉“测得于”）、一句档位结论。删除建议段、超时/重启/设置变更前说明段、评估时间行、统计范围、覆盖维度、“样本较少”提示。
- **展开态**：维度行样本少时只把数值置灰，不附“样本少”；记录开关去掉说明；评估时间只显示 `yyyy-MM-dd HH:mm:ss`，与“清除数据”按钮同行靠右（放不下时不画）。
- **i18n**：删除 `Advice.*`、`EvaluatedFormat`、`RangeFormat`、`RangeSingleFormat`、`CoverageFormat`、`LowSample`、`LowSampleShort`、`Recording.E`；`UnlimitedFormat` 改为单参数（其余语言取原译文“ · ”之前的部分）；格式化自检同步。
- **软件调试预览**：`Setting.PerformanceDashboard` 提供 `PerformanceDashboardPreview`（默认/无数据/充裕/适中/偏紧/超载/设置变更前）、`MakePerformanceDashboardPreview(preview, frameRate)` 生成虚拟报告（“适中”含两项暂无数据与一项样本少，“设置变更前”用 60 FPS 旧签名），以及进程内选择 `PerformanceDashboardPreviewSelection()`。调试页“画布”节在画布调试开关后用 `ComboRow` 选择；性能页在选择非默认时用虚拟报告替换显示用报告。不写配置，不触碰账本与绘制线程；预览期间看板里的记录开关与“清除数据”不生效（Codex 复核 P2）。
- 报告的 `usageDays`/`firstDate`/`lastDate`/`coverageMask` 只服务于已删除的统计范围与覆盖行，一并删除；7 个使用日窗口仍由日桶淘汰保证。

## 14. 帧率口径轮（2026-11-19，以本节为准）

起因：GPU 压力测试时看板仍显示“轻松”。旧口径的工作耗时扣掉了 `frameLatencyWaitMs_`（含 waitable 等待与 ULW 阻塞 Map），GPU 吃紧时变长的恰是这些等待；预算也只看画布帧率。经四轮实机修正，定稿如下（§2.2/§3 的计数与 §5 档位依据被本节替代）。

### 14.1 三个帧率
- **计算帧率**：帧循环推进速度。帧间隔 = 本轮开始到下一轮开始，只在本轮以帧率截止等待结束时有效（书写、漫游/激光淡出、拒收接触节拍三个分支调用 `markPendingFramePaced`）；无脏区不 Present 的轮次也算。
- **渲染帧率**：有脏区且 Present 成功的帧里，本轮有真正提交（`CommittedPresentCount()`：DComp 成功 Present1、ULW 成功 UpdateLayeredWindow）且提交完成时刻（`LastCommittedPresentMs()`）落进新 DWM 合成周期的帧数 / 这些帧的帧间隔之和。周期由 `RefreshSlotCounter` 按 `SampleDisplayTiming()`（约 250 ms 重读、周期保留小数）判定：不早于提交时刻的合成截止（垂直同步 − 2 ms），与上次计数的截止相差不到半个周期视为同一周期；没有合成时序时每次提交都算。
- **无限制渲染帧率**：渲染帧数 / Σ(进入帧率截止等待前的整帧耗时 − `framePacingWaitMs_`)。`pacingWaitMs` 只含 waitable 等待（帧内与 Present 前补等）中“开始等待时上一次 Present1 之后插入的 `D3D11_QUERY_EVENT` 已完成”的部分（`GetData(DONOTFLUSH)` 不等待、不自旋）；ULW 阻塞 Map、帧尾拷出是在等 GPU，计入。

### 14.2 负载与档位
- 耗时占比 = 平均无限制渲染耗时 / 画布帧间隔；缺口占比 = 画布帧率 / 计算帧率，> 103% 时取较大值（至少超载）。掉帧 = 帧间隔 > 1.5 × 画布帧间隔，占比 > 10% 至少偏紧。
- 渲染帧率与显示器只用于显示。

### 14.3 实机推翻的假设（不要再走回头路）
1. **waitable 会把帧循环压到刷新率**：DComp 以 `Present1(0, …)`（SyncInterval 0）提交，旧帧被替换后缓冲马上空出，waitable 立即发信号。实测画布 240、120 Hz 屏，帧循环与 Present1 都约 240/s，DWM 每次刷新只显示最新一帧。
2. **渲染帧率按刷新率封顶**：min(提交帧率, 刷新率) 只是由刷新周期推出的上限，且刷新率被取整，显示值永远是整数刷新率。改为按合成周期实测。
3. **渲染帧率低于显示器能力即超载**：ULW 异步读回 + 对齐、画布 120 实测计算帧率 120.4、耗时 19%，约 9% 的合成周期没有新内容（读回在即时/延后完成间切换时两次 UpdateLayeredWindow 落进同一周期），渲染 106，被误报超载。负载改为只看帧循环。另：按帧加权的 Σ限制间隔与按时间计的渲染帧率不可比（曾出现渲染 112.5 > 限制 104.3），限制帧率整体删除。

### 14.4 结算时机与存储
- 一轮的测量挂起在 `PendingFrame`，下一轮 `frameStartMs` 之后结算（那时才知道帧间隔与是否提交）；Run 正常退出前以帧间隔无效结算最后一轮。签名变化检测随之移到结算处；`MarkBusy` 只在一段第一个实际计入的帧调用。只有计算帧、没有渲染帧的段移交时不入账。
- 计数器 8 元数组 `[frames, costMs, timedFrames, displayedFrames, intervalMs, missed, computeFrames, computeIntervalMs]`，日桶 `display: [minHz, maxHz]`（四舍五入整数，只显示）；`kFileVersion = 4`，旧版本按 schema 不符改名 `.bad`（阶段 C 未发布，不兼容）。

### 14.5 界面与画布调试 HUD
- 看板：大百分比右侧为“平均负载”、“平均渲染帧率 约 N FPS”、小字“计算帧率 约 N FPS · 无限制渲染帧率 约 N FPS”；结论下方“当前显示器的帧率限制为 N FPS”（范围写 `60–120`）；工具行右侧为该工具的平均渲染帧率（取不到为 `--`）。新 i18n 键 `RenderFpsFormat`/`SecondaryFpsFormat`/`DisplayLimitFormat`（替代 `UnlimitedFormat`）。
- HUD：FPS = 每秒落进新合成周期的帧，新增 Calc（每秒按帧率推进的帧循环）与 Display（窗口内刷新率范围）一行，Unlimited 与 Load 改用同一耗时，Load 分母为画布帧间隔。

### 14.6 已知局限
- 多显示器时 DWM 时序为整个桌面合成（Win8.1 起 `DwmGetCompositionTimingInfo` 只接受空 hwnd），不一定是画布所在显示器。
- GPU 完成查询区分“等 GPU / 等显示节拍”是近似：GPU 吃紧时可能把少量垂直同步对齐也计入耗时。
- 按合成周期计数看不到 DWM 内部丢帧。
