# 阶段 A 技术设计

对应 `prd.md` 的 R1–R9。证据与实测数据见 `research/gpu-stress-latency-experiments.md`（第一至六轮）。行号以 `refactoring/draw` 当前 HEAD（`cf5160ed`）为准，实施前由开工核对复核。

## 1. 总体结构

| 部分 | 需求 | 主要位置 | 生效方式 |
|---|---|---|---|
| A. 进程调度（档位 + 电源节流） | R1–R4 | 新模块 `Inkeys/Inkeys/Helper/Helper.ProcessScheduling.cppm`；配置 `Other.Config.cppm`；设置页 `UI/Setting/Setting.cpp`；启动 `Inkeys/IdtMain.cpp`；i18n `Inkeys/src/i18n/zh-CN.jsonc` | 启动时应用一次；设置变更即时应用 |
| B. 熔断判据 | R5 | `Draw3.FrameLatencyGate.h`、`Draw3.TransparentPresentation.cpp`（`PrepareFrameLatency`） | 运行期 |
| C. 关闭等待用普通链 | R6 | `Draw3.TransparentPresentation.cpp:1884` | 创建交换链时（沿用“重启生效”） |
| D. L0 只清脏区 | R7 | `Draw3.Renderer.cppm/.cpp`、`Draw3.DrawingController.cpp:23666-23670`、L0 写入点 | 运行期 |
| E. 诊断包 | R8 | 新目录 `Scripts/LatencyDiag/` | 手动工具，不进解决方案 |

## 2. A：进程调度

### 2.1 配置

在 `Experimental.Inkeys3.Draw3` 组（性能页现有配置所在组，`NoUpload`）新增：

| 键 | 类型 | 默认 | 含义 |
|---|---|---|---|
| `GpuSchedulingPriority` | `IdtAtomic<int>` | `1` | 0 = 标准（NORMAL），1 = 均衡（HIGH），2 = 高性能（REALTIME）；越界按 1 处理 |
| `DisablePowerThrottling` | `IdtAtomic<bool>` | `true` | true = 显式关闭执行速度节流；false = 交还系统默认 |

### 2.2 模块接口（`Helper.ProcessScheduling.cppm`）

```cpp
export namespace Inkeys::Helper
{
	enum class GpuSchedulingTier : int { Standard = 0, Balanced = 1, Performance = 2 };
	struct ProcessSchedulingResult
	{
		bool priorityApiAvailable = false; NTSTATUS priorityStatus = 0;
		int priorityReadBack = -1;         // D3DKMTGetProcessSchedulingPriorityClass 读回
		bool throttlingApiAvailable = false; bool throttlingApplied = false; DWORD throttlingError = 0;
	};
	// 线程安全（内部互斥）；不抛异常；重复相同参数时跳过系统调用但仍返回上次结果。
	ProcessSchedulingResult ApplyProcessScheduling(GpuSchedulingTier tier, bool disablePowerThrottling) noexcept;
	int GpuSchedulingClassForTier(GpuSchedulingTier tier) noexcept; // 纯函数，供测试
}
```

- `D3DKMTSetProcessSchedulingPriorityClass` / `D3DKMTGetProcessSchedulingPriorityClass`：`GetModuleHandleW(L"gdi32.dll")` + `GetProcAddress`，作用于 `GetCurrentProcess()`。类值 NORMAL=2、HIGH=4、REALTIME=5（`D3DKMT_SCHEDULINGPRIORITYCLASS`）。不包含 d3dkmthk.h，模块内自行声明函数指针类型和常量（注明来源）。
- `SetProcessInformation`：`GetProcAddress(kernel32)`（Win8+ 才导出）。`PROCESS_POWER_THROTTLING_STATE{ Version=1, ControlMask=EXECUTION_SPEED, StateMask=0 }` 为关闭节流；关闭开关时传 `ControlMask=0, StateMask=0`，交还系统决定。Win10 1709 之前返回失败，记录错误码即可。SDK 已有的结构体/常量直接使用，缺失时自行定义（Win7 工具集兼容）。
- 日志（`Log::Module::Config` 或现有性能/Draw3 模块，开工核对时确定）：每次实际调用输出一行：请求档位、对应类值、节流开关、两项 API 是否可用、返回码、读回类值、系统版本号。相同参数重复调用不重复输出。
- 不在 Draw3 绘制线程内调用；系统调用耗时极短，可在 UI 线程直接调用。

### 2.3 调用点

- **启动**：`IdtMain.cpp` 配置加载完成（约 3413 行“配置信息初始化完成”）之后、Draw3 Host 启动之前调用一次。必须早于画布第一帧，避免先以降级状态运行。
- **设置页**：性能页“渲染”节，放在“渲染加速”之后：
  - `Design::ComboRow("##gpu-scheduling-priority", 名称, 说明, 图标, tier, {标准, 均衡（推荐）, 高性能}, i 提示)`：变更时写配置、立即调用 `ApplyProcessScheduling`、`QueueConfigWrite()`。i 提示用于“未关闭电源节流时可能不生效”，不再显示“重启生效”。
  - `Design::ToggleRow("##disable-power-throttling", 名称, 说明, 图标, value)`：同样即时应用。
- 文案（zh-CN 草稿，实施时按现有文案风格精修，其他语言用 `pwsh Scripts/i18n.ps1 sync` 同步后补译）：
  - 档位名称：`GPU 调度优先级`；说明：`其他程序占用显卡时，提高 Inkeys 获得显卡时间的优先级，减少笔迹滞后。`
  - 选项：`标准` / `均衡` / `高性能`；高性能附加说明（选中时的 Notice 或说明文字）：`可能挤占录屏、演示等程序的显卡时间，录屏时不建议使用。`
  - i 提示：`未关闭电源节流时，Windows 可能把 Inkeys 视为后台程序并降低其优先级，所选档位可能不会生效。`（不点名具体档位：Win11 实测只有高性能不受影响，其他系统和环境未验证。）
  - 节流开关名称：`关闭电源节流`；说明：`避免 Windows 把 Inkeys 当作后台程序降低性能。Windows 10 1709 及以上生效。`
- 设置页结构改动会影响 `setting_design_tests` 中依赖性能页元素顺序/首个 i 的用例，实施时一并核对和调整。

## 3. B：熔断判据（R5）

现状：`PrepareFrameLatency`（`TransparentPresentation.cpp:2539`）在等待前取 `gpuIdle`，策略只按可见性/交接决定 `Count` 或 `Exclude`。

修改：
- `FrameLatencyGate::Acquire` 的超时策略改为**在等待返回超时之后**才确定。新增一个重载或参数：传入一个 `TimeoutPolicyResolver`（可调用对象，返回 `FrameLatencyTimeoutPolicy`），仅在 `Signaled` 以外的超时分支调用一次。原有直接传策略的调用点保持行为不变。
- `PrepareFrameLatency` 传入的解析逻辑：不可计数状态 → `Exclude`（不变）；可计数状态下，超时返回后查询 `PreviousFrameGpuIdle()`：GPU 仍忙 → `Neutral`（既不计数，也不打断已有连续计数）；GPU 已空闲 → `Count`。（实施后修订：GPU 忙改为独立策略 `GpuBusy`，欠账上限 32、溢出熔断、5 秒兜底，等待期间转隐藏按 `Exclude`；见 `draw3-integration.md` 与 research 第七轮。）
- 补等（还账）超时路径不变（本就不计入熔断）；`Failed` 路径不变（立即熔断）。
- 诊断：`[Draw3Diag][latency]` 每秒汇总增加 `gpuBusyTimeouts=`，熔断行增加该计数，便于老设备诊断。
- 测试：在现有 FrameLatencyGate 单元测试位置补充用例：连续 GPU 忙超时不熔断；GPU 忙与空闲交替时连续计数只累计空闲超时；Failed 仍熔断；原有策略调用行为不变。

## 4. C：关闭等待时创建普通链（R6）

- `TransparentPresentation.cpp:1884` 的条件改为：`!IsDirectCompositionMode(mode) || frameLatencyGate.Tripped() || !options.enableWaitableWait || !TryCreateWaitableSwapChain(...)` → 走普通 FLIP 链。
- `TryCreateWaitableSwapChain` 内 “waitable wait disabled by setting” 分支随之不再可达，删除并在创建处输出一行 `[Draw3Diag][latency] waitable disabled by setting; using ordinary swapchain`。
- 普通链仍由设备级 `IDXGIDevice1::SetMaximumFrameLatency(1)`（`GraphicsInitialization.cpp:161`）限流；熔断恢复路径不变。
- 设置项保留“重启生效”提示（交换链创建时读取）。
- 规范：`draw3-integration.md` 第 496 行“开关关闭 = 保留 waitable 交换链但不等待”改为新约定，并记录实测（waitable 链不等待 400→1840 ms，普通链 47–133 ms）。

## 5. D：L0 只清脏区（R7）

### 5.0 S0 开工核对后的修订（Codex `report-S0.md`，已逐条读码核实）

- **记账改为“先声明、后绑定”**：布尔标志在“绑定后报告”模式下会被后一次报告清掉前一次未报告写入（多指、形状会连续绑定 L0），批次上传中途失败也无法兜底。改为：写入点在绑定前调用 `NoteOperatorWrite(layer, bounds)`，记入包络并“预约”下一次绑定；`SetOperatorTarget(L0)` 若没有预约，则置 `contentUntracked = true`，有预约则消耗预约。包络先于绘制并入，批次中途失败也只会多清。
- 记账放在 `InkRenderer` 内（按 L0 层对象识别），因为绑定/清除接口接收 `const OperatorLayerResources&`。L0 纹理重建（`RebuildActiveLayers`、Resize、设备恢复）时记账置为“未知”，下一次清除退回整屏。
- `ClearOperatorLayerContent` 判定顺序：先判断“自上次清除以来无任何写入”（包络空且无未知写入）→ 不清；再判断未知/无 `ID3D11DeviceContext1`/面积超 50% → 整屏；否则局部 `ClearView`。参照 `ClearL2Tile` 先解绑 SRV。
- 通用形状的 L0 实际写入矩形比 `RuntimeShapeBounds`/`betweenShapeBounds` 大 1 画布像素（`RendererShapes.cpp` 合成矩形 `floor(lo-1)..ceil(hi+1)`），局部清除外扩 2 px 已覆盖；声明时不再额外外扩。
- 形状预热的零视口绑定、通用形状空返回不影响正确性（前者只在启动/设备恢复，会让下一次清除退回整屏一次）。
- R5：`PreviousFrameGpuIdle()` 只把 `S_FALSE` 视为忙，设备移除等错误按空闲处理，避免熔断判据永久失效；等待前采样（`pacingWaitMs`）沿用同一函数，只在设备错误时口径变化。帧外补等（`Neutral`）收到 `Failed` 时于下一帧首熔断，属现状，不改。
- R1：`GpuSchedulingPriority` 读取后按 0–2 校验，越界按 1。
- R8：Inkeys 配置实际路径为程序根目录 `Inkeys\Config\main.json`；`Start-StressLoad` 只支持 ARM64/x64，x86 包需补 StressLoad 的 x86 构建入口。
- Win7：工程未定义 `_WIN32_WINNT`（SDK 默认 0x0A00），节流结构与常量可直接使用；`SetProcessInformation` 必须 `GetProcAddress`，否则引入静态导入使 Win7 无法启动。日志用 `Log::Module::Draw3`。

### 5.1 内容包络记账

（以下为原设计，记账方式以 5.0 为准。）

- `OperatorLayerResources` 增加记账字段（只用于 L0，其他层不受影响）：`RECT contentBounds{}`（自上次清除以来已知写入的并集）与 `bool contentUntracked = false`（有过未报告范围的写入）。
- `InkRenderer::SetOperatorTarget(layer)`：若目标为 L0，置 `contentUntracked = true`（保守假设：会写入但范围未知）。
- 新增 `InkRenderer::NoteOperatorWrite(layer, RECT bounds)`：并入 `contentBounds` 并清除 `contentUntracked`。三个 L0 写入点在绘制后调用：
  1. `DrawL0LiveComposite`（`StrokeGeometry.cpp:2736`）：`stroke.currentL0Rect`；
  2. `DrawActiveShapePrimitives`（`DrawingController.cpp:6103`）：各活动形状的 `RuntimeShapeBounds`；
  3. 形状构造预览（`ShapeRuntimeActions.inl:750`）：预览包络（开工核对确认来源）。
  开工核对须确认：这三处的包络已包含线宽、抗锯齿、笔尖锥形和预测段；不足时在报告点外扩，而不是修改现有脏区语义。
- `ClearOperatorLayer(layer)`（整屏清除，所有现有调用点不变）：清空记账（`contentBounds = {}`，`contentUntracked = false`）。
- 新增 `InkRenderer::ClearOperatorLayerContent(layer)`：
  - `contentUntracked` 为真，或没有 `ID3D11DeviceContext1`，或包络面积超过画布 50% → 退回 `ClearOperatorLayer`（整屏，保证不留残影）；
  - 包络为空 → 什么都不做；
  - 否则包络外扩 2 px 并裁剪到画布，对 Add RTV 用 `ClearView(…, {0,0,0,0}, &rect, 1)`、对 Retain RTV 用 `ClearView(…, {1,1,1,1}, &rect, 1)`，然后清空记账。
  - `ID3D11DeviceContext1` 在 Renderer 初始化时 `QueryInterface` 一次并缓存（可参考 `InkHistoryGpu.cpp:712-742` 的现有写法）。

### 5.2 调用点

- 只改书写每帧那一处：`DrawingController.cpp:23670` 的 `ClearOperatorLayer(layerL0)` → `ClearOperatorLayerContent(layerL0)`。纯橡皮/纯激光接触时 L0 没有记录写入，包络为空，不做任何清除。
- 其余全部 `ClearOperatorLayer(layerL0)` 调用点（抬笔、重建、恢复、形状等）本阶段不改（阶段 B 处理），它们自动重置记账。
- 合成与 Present 的脏区计算不变：`frameDirty` 已包含 `lastL0Rect ∪ currentL0Rect`。

### 5.3 不变量与验证

- 不变量：L0 在 `contentBounds` 之外处处为 Add=0、Retain=1。
- 保险：任何未报告范围的 L0 写入都会让下一次清除退回整屏。
- 验证：笔、荧光笔、预测开/关、多指、形状构造、形状编辑、橡皮、激光、抬笔后立即落笔等场景截图比对无残影；可增加 headless 用例：对 L0 写入两个不相交区域后局部清除，回读确认像素复原。

## 6. E：诊断包（R8）

### 6.1 组成（`Scripts/LatencyDiag/`）

| 文件 | 作用 |
|---|---|
| `LatencyDiag.cpp` | 单文件原生工具，C++20，`/MT` 静态链接，目标 Win7 SP1（`_WIN32_WINNT=0x0601`），新 API 全部动态加载 |
| `Start-LatencyDiag.cmd` / `.ps1` | 参照 `Start-StressLoad`：用 vswhere + VsDevCmd 编译 x86 / x64 / arm64，产物在 `Build/Tools/LatencyDiag/<arch>/` |
| `Pack-LatencyDiag.ps1` | 为指定架构打包：诊断工具 + 同架构 StressLoad + 使用说明，输出到 `Build/Tools/LatencyDiag/package-<arch>/`；可选参数指定一份 Inkeys 构建目录一并复制 |

### 6.2 运行流程（目标机上双击或命令行运行）

1. **环境报告**：系统版本/构建号、CPU、内存、DXGI 适配器与驱动版本、显示分辨率/刷新率/DPI、DWM 合成状态、电源计划与电池状态、两类 API 是否可用、Inkeys 版本与相关配置（读 `Config/main.json`）。
2. **前置**：提示用户启动 Inkeys 并切到绘制模式（一次手动操作，避免依赖工具栏坐标）。工具按进程路径查找 Inkeys（排除带参数的自检进程），试画一笔确认能检测到墨迹，失败则给出提示并退出。
3. **测量矩阵**（外部切换，无需重启 Inkeys）：
   - 负载：无负载；压力工具（GPU，自动置前台）；可选“自备负载”（提示用户打开 PPT 放映/录屏后按回车）。
   - 配置：标准、均衡、高性能 × 电源节流（系统默认 / 关闭；系统不支持时跳过并记录）。
   - 每格画 3 个圆，各 5 次截屏，记录可见延迟 p50/最大；同时读回 Inkeys 的调度类。
   - 墨迹检测基于与参考帧的差异，不依赖笔颜色。
4. **可选 WPR**（仅 Win10+ 且存在 `wpr.exe`）：经用户确认后提权录制两段约 10 秒的轨迹（无负载书写、压力书写），供带回本机解析 ChangePriority 与包排队时间。
5. **收集**：复制本次会话期间的 Inkeys 日志；输出 `LatencyDiag-<机器名>-<时间>/`（`report.txt`、`results.csv`、`env.json`、`logs/`、可选 `trace-*.etl`）。系统有 `Compress-Archive` 时顺带打包 zip。
6. **收尾**：恢复 Inkeys 原有调度类与节流状态，结束压力工具。总时长约 8–12 分钟，压力只在测量时运行。

### 6.3 约束

- 不修改 Inkeys 配置，不写注册表，不安装任何东西；输出只写到工具所在目录。
- Win7 上无 `SetProcessInformation`、无 WPR：相关项跳过并在报告中注明。
- x86 构建用于 32 位 Win7；本机先用 arm64 构建完整跑通一次。

## 7. 兼容性与风险

| 风险 | 处理 |
|---|---|
| REALTIME 挤占录屏/演示 | 非默认；说明文字提示；诊断包记录对照数据 |
| 关闭节流增加电池耗电 | Inkeys 空闲时线程休眠，影响有限；开关可关；文档说明 |
| D3DKMT 调用在某些系统需要权限或返回失败 | 动态加载、失败只记日志 |
| L0 包络遗漏写入导致残影 | 未报告写入自动退回整屏清除；场景截图比对；Codex 开工核对逐个写入点 |
| `ClearView` 不可用（Win7 未装平台更新） | 退回整屏清除，行为与现状一致 |
| R5 掩盖真实信号丢失 | 只有 GPU 仍忙才视为中性；GPU 空闲时照常计数；Failed 仍熔断 |

## 8. 回退

各部分相互独立：A 可通过“标准 + 关闭开关”回到现状；B/C/D 均为局部改动，可单独回退对应提交。
