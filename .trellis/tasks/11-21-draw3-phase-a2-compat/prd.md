# 阶段 A2：调度回退、自动 Win7dwm 与兼容性修复

## Goal

阶段 B 开始前的一组小修改，共四项：
- GPU 调度类请求失败时逐级回退。
- 画布初始化信息显示当前的调度类与电源节流状态。
- 自动呈现方式在 Win7 上加入 Win7dwm。
- 阶段性兼容性检查：修复 WinPE 因 Magnification.dll 无法启动、放大 API 不可用时定格按钮保持禁用、核对 RTS 失败后回退到鼠标兼容（Wine）。

本任务属于中期重构 `11-16-inkeys3-midterm-refactor`，排在阶段 A（`archive/2026-11/11-20-draw3-gpu-stress-latency`）之后、阶段 B 之前。

## 现状（2026-11-21 调查，代码位置均已核对）

### 1. GPU 调度类
- `Helper.ProcessScheduling.cppm` 中的 `ApplyProcessScheduling` 只调用一次 `D3DKMTSetProcessSchedulingPriorityClass(档位类)`，失败时只记日志，不回退。
- 读回值 `priorityReadBack` 只写进日志，界面上看不到。
- 电源节流的结果（`throttlingApiAvailable`、`throttlingApplied`、`throttlingError`）同样只写日志。
- 调用点有两处：启动时 `IdtMain.cpp:3419`，设置页即时调用 `Setting.cpp:4801`。

### 2. 画布初始化信息
- 文本由 `Draw3.Host.cpp` 的 owner 在状态边界拼接（`UpdateCanvasInformation`）。
- 设置页 `Setting.cpp:5193` 取同一个字符串，用于显示（`RenderEdidCard`）和复制（`CopyButton`）。
- 进程调度是进程级状态，不属于画布 owner。

### 3. 自动呈现方式
- `Draw3.TransparentPresentation.cpp` 中，`FirstProductMode`（`:110-120`，带 static_assert）和 `Initialize`（`:2282-2289`）把自动链固定为 DComp → ULW。头文件 `.cppm:25` 写明“自动候选只包含 DComp/ULW”。
- 这条规则来自 `archive/2026-11/11-19-win7-dwm-presentation` 中用户批准的决定（prd:14，提交 `4a7c4680`）。现在 Win7dwm 兼容性实测已通过，本任务按用户新决定推翻它。
- **Win7 上自动链实际不会真正尝试 DComp**，原因如下：
  - Win7 没有 `dcomp.dll`。`ShouldPreconfigureNoRedirectionBitmap()` 返回 false，所以 HWND 创建时就不带 `WS_EX_NOREDIRECTIONBITMAP`。
  - `allowDirectComposition` 为 false，`TryInitialize` 也会在改窗口样式之前用 API 探测拒绝 DComp。
  - 因此在 Win7 上，“DComp 失败 → Win7dwm”时窗口还是干净的，不存在 DComp 残留，也不需要重建窗口。
- 每次尝试之间的清理已经统一：`ReleaseAttempt` 依次处理 DComp 退休等待、Dwm 关闭模糊、释放交换链，离开 Win7dwm 时还会 Flush；退休超时会中止整条链。`ConfigureWindow` 每次都会显式设置或清除 NOREDIRECTIONBITMAP 和 LAYERED 两个位。
- Win8+ 上 Win7dwm 会在 `CheckWin7Dwm` 的版本阶段被拒绝，此时还没有改动任何窗口状态。
- 运行期恢复 `RecoverFromRuntimeFailure`（`:2335-2430`）：
  - 手选 Win7dwm 时，运行期失败一律降到 ULW，之后不再回到 Win7dwm，并且有“无进展”保护。
  - 如果只改初始化链，自动来源的 Win7dwm 会出现两个问题：非设备丢失时会在同一 HWND 里反复重进 Win7dwm，而且没有无进展保护；设备丢失时会被悄悄丢到 ULW。
- `IdtMain.cpp:4043` 的 `retryWithLegacyWindows` 只对 DComp 首选和手选 Win7dwm 为 true，自动且在 Win7 上时为 false。
- 测试：`Draw3.HiddenWindowTest.cpp:1286-1290` 断言“自动只选 DComp 或 ULW”。

### 4. 兼容性
- 工具集基线符合约定：`Inkeys.vcxproj` 的 6 个配置、`InkeysHeadlessTests.vcxproj` 的全部配置都是 v143 + stdcpp20，静态 CRT（/MT）。
  - Release Win32/x64 的 `MinimumRequiredVersion` 为 6.01，ARM64 为 6.02。
  - 未定义 `_WIN32_WINNT`，编译期不会拦截误用的 Win8+ API。
- 自有代码调用的 Win8+ API 全部经 `GetProcAddress` 动态获取，包括 DComp、`GetDpiForMonitor`、`GetPointer*`、`SetProcessInformation`、`GetSystemTimePreciseAsFileTime` 等。
- 唯一构建产物 `Build\ARM64\Debug\Inkeys.exe` 的 dumpbin 结果：
  - 静态导入都是 Win7 已有的 DLL。
  - `WaitOnAddress`（api-ms-win-core-synch-l1-2-0）来自 ARM64 的静态 STL，**允许保留**：Windows on ARM64 最低就是 Win10，Win7 SP1 根本没有 ARM64 版本，ARM64 的 STL 本来就以 Win8+ 为目标（Release ARM64 的 `MinimumRequiredVersion` 为 6.02 也与此一致）。x86/x64 Release 还没有核对。
- **WinPE 无法启动的根因**：`IdtMagnification.h:6` 有 `#pragma comment(lib, "magnification.lib")`，而 `DelayLoadDLLs>Magnification.dll` 只写在 3 个 Debug 配置里（vcxproj:249/326/407）。所以 3 个 Release 配置静态导入 Magnification.dll，缺少这个 DLL 的 PE 加载时直接失败。
- 运行时防护已经存在，延迟加载后即可生效：
  - `PrepareMagnifierWindow`（`IdtMagnification.cpp:252-278`）先探测 DLL，Wine 环境直接返回 false，再调用 `MagInitialize`。失败时 Window Service 跳过放大镜窗口。
  - 后续所有 Mag* 调用都要求 `magnificationCreateReady` 为真。
  - 探测用的是裸 `LoadLibrary`，存在 DLL 劫持风险。
- **定格按钮**：
  - `Bar.Button.cpp:1048` 的 `state` 只看 `Freeze::IsAvailable()`，这个函数只判断 PPT 或白板工作区位。
  - 放大 API 不可用时按钮仍然可点。点击后定格先激活，再因 `InvalidTarget` 自动撤销，表现为按钮闪一下。
  - `IsAvailable()` 同时决定按钮是否可见以及 A2 区布局，所以不能把放大 API 状态并进去。
- **RTS 回退**：
  - 以 HRESULT 报告的 RTS 失败都会落到下一个后端：RTS 扩展 → 基础 XY → WM_POINTER（6.2+）→ 鼠标兼容。Wine 没有注册 RealTimeStylus 类，`CoCreateInstance` 失败会走这条正常回退。
  - Wine 默认报告的版本是 Win10 19045，自动模式首选 Pointer。Pointer 模式本身也会开启 Win32 鼠标路径。
  - 以下情况按现有设计不回退：
    - C++ 异常：`RtsStartup.h:233-239` 有意 rethrow，注释为“意外异常不降级”，结果是 Host 启动失败。
    - 部分初始化后停产失败：直接走 `FailUnprovenProducerStop()` 终止进程，这是安全设计。
    - RTS 启用成功但从不送包：没有运行期看门狗。

## Requirements

- **R1 调度类逐级回退**：
  - 第一个尝试的就是设定档位对应的类，失败才向下逐级尝试，**当前一级成功就不再尝试下一级**：

    | 设定档位 | 尝试路线 |
    |---|---|
    | 高性能 | REALTIME → HIGH → NORMAL |
    | 均衡（默认） | HIGH → NORMAL |
    | 标准 | 只尝试 NORMAL |

    例：高性能下 REALTIME 成功即结束；REALTIME 失败、HIGH 成功即结束，不再请求 NORMAL。
  - 失败判定：`D3DKMTSetProcessSchedulingPriorityClass` 返回的 NTSTATUS 不成功（`< 0`）。
  - 遇到第一个成功的请求即停止。全部失败时保持系统原状，并记录日志。
  - 日志每次尝试一行，或在汇总行中带上尝试序列（设定类、各次 status、最终类、读回）。
  - 设置值不变：配置仍保存用户所选档位，不因回退改写。
  - `ProcessSchedulingResult` 增加“实际成功请求的类”字段。
- **R2 画布初始化信息显示调度与节流**：
  - 在“设置 → 软件调试 → 画布初始化信息”文本中增加两行：
    - `GPU 调度类：HIGH (4)`，值取最近一次读回。读回失败时显示最近一次成功请求的类并注明“未读回”；从未成功时显示“不可用”。
    - `电源节流：已关闭执行速度节流`。其他状态分别为“系统默认”、“不支持（需 Windows 10 1709+）”、“设置失败 (错误码)”。
  - 只显示当前状态，不显示回退过程（同其他条目）。
  - 显示与复制必须是同一个字符串。数据取自 `ApplyProcessScheduling` 缓存的上次结果（新增线程安全的只读快照函数），不逐帧调用系统 API。
- **R3 自动呈现方式加入 Win7dwm**：
  - 自动链：Win8+ 为 DComp → ULW（不变）；Win7（版本低于 6.2）为 DComp → Win7dwm → ULW。
  - Win7dwm 只在满足先决条件时尝试：Win7 SP1、DWM 合成开启、着色不透明度检查通过，即现有 `CheckWin7Dwm`。
  - 手选各模式的行为不变。
  - 每一步失败都必须经过 `ReleaseAttempt` 完整清理，才能进入下一步。清理失败（DComp 退休超时）时中止整条链，这一点不变。
  - 去掉“fallback to UlwDirtyRect”这类写死的日志，改为输出实际的下一候选。
  - 运行期恢复：自动来源的 Win7dwm 与手选一致，运行期失败或设备丢失都降到 ULW，不再回到 Win7dwm，并纳入现有的无进展保护。判定依据是“当前或本会话进入过的模式是自动来源的 Win7dwm”，不能依赖 `preferredMode`。
  - 自动且在 Win7 上时，`retryWithLegacyWindows` 与手选 Win7dwm 一致设为 true。
  - 自动回退不产生呈现方式警告（沿用现有规则）。设置页提示规则不变，自动模式不显示提示。
- **R4 兼容性**：
  - **R4.1 基线核对（只读）**：
    - 确认 v143 / stdcpp20 / 静态 CRT 不变。
    - 构建 Release Win32 和 Release x64，对 exe 运行 `dumpbin /imports`，确认没有 Win8+ 才有的导入（如 `WaitOnAddress`、`CreateFile2`、`CopyFile2`、`GetSystemTimePreciseAsFileTime` 等），并确认 Magnification.dll 位于延迟导入。
    - ARM64 不在 Win7 兼容要求内：ARM64 产物中标准库带来的 Win8+ 导入（如 `WaitOnAddress`）属于正常，不算问题；ARM64 只需确认 Magnification.dll 位于延迟导入。
    - 若发现 Win8+ 导入，先报告再定修法。不在本任务内定义 `_WIN32_WINNT`。
  - **R4.2 Magnification.dll**：
    - 3 个 Release 配置补 `<DelayLoadDLLs>Magnification.dll;%(DelayLoadDLLs)</DelayLoadDLLs>`，与 Debug 一致。
    - `PrepareMagnifierWindow` 的探测改用 System32 绝对路径加载，复用现有的 `LoadSystemLibrary` 或同等实现。
  - **R4.3 定格按钮保持禁用**：
    - `Inkeys.UI.Freeze` 新增独立的能力位（如 `SetSupported` / `IsSupported`），在放大镜窗口创建结果确定后设置，窗口组重建时先清除。
    - 不支持时：按钮按原规则显示且布局不变，状态固定为 `Disable`（灰色，不响应悬停和点击）；`Toggle()`、按钮路径和快捷键路径都直接拒绝，不再出现“先激活再撤销”的闪烁。
    - 能力位变化后调用 `RefreshToolState()` 刷新主栏。
  - **R4.4 RTS 回退**：
    - 核对并以测试证明：RTS 创建、配置、启用失败（以 HRESULT 报告）时，最终能落到鼠标兼容。
    - 异常与停产失败的语义保持不变（Q3）。

## Acceptance Criteria

- [x] 调度回退：单元测试覆盖档位到尝试序列的映射、首个成功即停、全部失败、标准档不回退。
- [x] 启动日志能看到尝试序列（`attempts=类:status,...`）。
- [x] 画布初始化信息显示调度类与电源节流，复制内容与显示一致，设置页切换档位后即时更新（代码路径 + 格式化测试；未开 GUI 目视）。
- [x] 自动呈现链：无窗口测试或 static_assert 覆盖 Win7 和 Win8+ 两种候选序列。
- [x] 隐藏窗口测试的断言改为“自动只选 DComp、Win7dwm（仅 Win7 且可用）或 ULW”。
- [x] 运行期恢复中，自动来源的 Win7dwm 降到 ULW 后不再重进（代码审查 + Codex 审查确认；无 Win7 运行期测试）。
- [x] Release Win32/x64 的 dumpbin 结果里 Magnification.dll 位于延迟导入，并且没有 Win8+ 导入（若有，已报告）；Release ARM64 的 Magnification.dll 位于延迟导入（标准库的 Win8+ 导入允许）。
- [x] 放大 API 不可用时，定格按钮显示为灰色且不可点击，快捷键无效；可用时行为不变。
- [x] RTS 以 HRESULT 失败时能落到鼠标兼容，有测试证明。
- [x] 完整 `InkeysRepo.sln` Debug|ARM64 构建通过；相关无窗口测试和自检通过。
- [x] spec 同步：`draw3-integration.md`、`native-desktop/index.md`、`native/platform-and-resources.md`、`guides/cross-layer-thinking-guide.md` 中关于自动链的描述；`settings-performance-and-drawing.md` 的调度条目；画布初始化信息条目。
- [x] 需要用户实机验证的项：Win7 SP1+KB2670838 上自动选中 Win7dwm 与失败回退、WinPE 启动、Wine 基础书写（2026-11-21 用户实机验证通过）。

## 已确认的决策（2026-11-21 用户确认）

- **Q1 调度失败判定**：只看 NTSTATUS；读回值只用于画布信息显示。
- **Q2 自动 Win7dwm 运行期策略**：与手选一致，运行期失败与设备丢失都降到 ULW、不再重进，纳入无进展保护。
- **Q3 RTS 异常与停产失败**：保持现有设计（异常不降级、停产失败终止进程）；本任务只为 HRESULT 失败 → 鼠标兼容补测试，并加 Wine 实机验证。
- **Q4 WinPE 范围**：本任务只处理 Magnification.dll；其他静态依赖是否缺失取决于 PE 镜像，按实测反馈另行处理。

## Out of Scope

- 阶段 B（抬笔整屏清除、形状中间层、激光烘焙）和阶段 C（L2 快照）。
- 定义 `_WIN32_WINNT`、调整 vcxproj 其他设置、升级工具链。
- RTS 运行期看门狗（启用成功但不送包时的重新选择）。
- 设置页呈现方式提示的文案调整。

## Notes

- 分工沿用阶段 A：Claude 规划与实现；如需 Codex，只用于调查和独立审查。
- 源文件改动保持 UTF-8 BOM + CRLF。

## 实施记录（2026-11-21）

- 分工：Claude 实现呈现链、定格能力位、设置页接线、构建验证与 spec 同步；Codex（`gpt-6.2-sol` High，default）实现调度回退核心与格式化、对应单元测试及 RTS 回退测试，并做独立审查与复核。
- 改动：`Helper.ProcessScheduling.cppm`（`SchedulingFallbackRoute`/`RunSchedulingFallback`/`LastProcessSchedulingResult`/`FormatProcessSchedulingInformation`/`NextCurrentSchedulingClass`）；`Setting.cpp` 画布信息末尾追加两行；`Draw3.TransparentPresentation.cpp`（`BuildProductModeChain` + static_assert、`automaticWin7DwmEntered`、失败日志写实际下一候选）；`IdtMain.cpp`（自动且 Win7 时启用窗口链重建重试、发布 `Freeze::SetSupported`）；`Inkeys.vcxproj` 三个 Release 配置补 `DelayLoadDLLs`；`IdtMagnification.cpp` 从 System32 加载并保留模块；`Freeze.cppm`、`Bar.Button.cpp`、`IdtState.cpp` 定格能力位；测试见下。
- 验证：
  - Debug|ARM64 完整构建；`InkeysHeadlessTests.exe --no-window` 通过；`Inkeys.exe --state-mode-production-test`、`--bar-eraser-offscreen-test`、`--draw3-control-fence-test`、`--draw3-device-policy-test` 通过；`--draw3-hidden-test` 退出 127，与基线一致（既有问题），日志显示本机自动仍选 DComp。
  - 反证：去掉按钮入口的 `IsSupported` 检查 → 状态自检 `freeze-unsupported-button-keeps-clickthrough-mode-and-revision` 失败；高性能路线去掉 HIGH → headless 2 项失败；还原后全部通过。
  - Release Win32 / x64 / ARM64 完整构建并 `dumpbin /imports`：三者 `MAGNIFICATION.dll` 均为延迟导入；Win32/x64 无 Win8+ 导入（静态 DLL 均为 Win7 已有）；ARM64 仅多出标准库的 `api-ms-win-core-synch-l1-2-0.dll`（允许）。
- Codex 审查 4 个 P2 的处理：
  - 调度失败后诊断丢失上次成功的类 → 新增 `currentClass`（`NextCurrentSchedulingClass`），有测试。
  - `--bar-eraser-offscreen-test` 定格夹具 → 作用域内置为支持并恢复。
  - 状态自检对入口检查不敏感 → 快捷键从批注模式、按钮从穿透模式验证，已反证。
  - DComp 退休超时后候选链可能继续：改动前两步链同样存在；尝试“立即中止”会丢失原失败截止与诊断，复核后撤回，保持原语义，记为既有问题（见下）。
- 既有问题（未处理）：DComp 失败且释放（退休）超时后，若退休在下一候选前完成，链会在同一 HWND 继续尝试下一模式；正确修法需要让 release 失败携带原截止并单独结束链，涉及 failed-cleanup 截止语义，另行处理。
- 实机验证（2026-11-21 用户确认通过）：Win7 SP1+KB2670838 上自动选中 Win7dwm 与失败回退；WinPE 启动与定格按钮禁用；Wine 基础书写（鼠标兼容）。
