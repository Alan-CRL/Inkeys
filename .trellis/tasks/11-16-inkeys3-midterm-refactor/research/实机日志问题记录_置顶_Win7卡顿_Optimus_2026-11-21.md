# 实机日志问题记录：置顶校验、Win7 切换卡顿、Optimus 独显未选用（2026-11-21）

来源：用户转交的三份实机材料（Wine 启动日志 `Logs.zip`；Win7 用户日志 `2026-11-21T20-55-03.log`；Win7 SP1 WARP 机器的 `LatencyDiag-ADMIN-PC-20261121-220738.zip`）。代码基准 `257cf4c5`，行号不可视为永久有效。

本文件只记录现象、证据和调查结论，不包含实施；各项的处理时机见末尾“处理安排”。

---

## 1. Wine 启动失败：置顶组 Z 序复核不通过（归入置顶专项）

### 现象

- 环境：Wine，报告版本 `os=10.0.22000`（与此前验证通过、默认报 19045 的 Wine 环境不同）；Magnifier 按设计在 Wine 下禁用。
- 两次启动结果一致：
  - `[Window] Inkeys.Window command failed: operation=RefreshTopmost role=0 error=31 hwnd=0x0`（窗口线程上的首次刷新）
  - `[Draw3Topmost] phase=initial result=failed cause=root-refresh role=Root ... Mag={hwnd=0 ...} Freeze={valid=1 exstyle=0x80800A8} Presentation={owner=Freeze valid=1 exstyle=0x80800A8} Drawpad={owner=Presentation valid=1 exstyle=0x8200088}`
  - `[CRIT][Main] Draw3 Host 初始化失败`

### 调查结论

- 失败点是 `Window.cpp` 的 `RefreshOverlayGroup`：重排两轮后，用 `Window.ZOrderPolicy.h` 的 `ZOrderSnapshot::Matches` 加 `GroupMatches` 做严格复核，要求：
  - 组成员在 `EnumWindows` 序列中连续，不夹外来窗口；
  - owner 链正确，`WS_EX_TOPMOST` 与策略一致；
  - 被拥有者排在 owner 之上。
- 两轮都不满足时，代码自己写入 `ERROR_GEN_FAILURE`（日志中的 `error=31`）。`IdtMain.cpp` 的 `PrepareDraw3WindowTopmost` 据此判 `root-refresh`，Host 启动直接失败。
- 日志里的句柄、owner 链和 TOPMOST 位都正确：Mag 缺失时 Freeze 作根，这条路径没问题。失败的只是 Z 序复核。更可能是该 Wine 的窗口管理器或虚拟桌面上报的 Z 序与 Windows 语义不同（成员不连续，或 owned 窗口不在 owner 之上）。
- 现有日志只有总错误码，**无法判断是哪一条复核失败**。
- `role=0 hwnd=0x0` 是因为刷新命令固定以 `MagnifierHost` 作为命令角色提交；Mag 缺失时日志里的 hwnd 就是 0，与失败原因无关。

### 关联

- 用户另述：此前 Win7 下也出现过置顶问题，一并纳入置顶专项（具体现象待专项开工时向用户补充确认）。
- 既定方向（2026-11-16 用户确认，见白板窗口化讨论）：层级（Z 序/owner/topmost/band）严格校验属于多余判断。方向是“窗口出现即可，层级交给周期置顶刷新兜底”，不再新增层级校验。本问题即该类严格校验在非标准环境下变成启动致命错误的实例。

---

## 2. Win7 用户反馈“切换状态后卡住一分钟左右”（待日志完善后复测）

### 证据（Info 级日志，构建 `buildTime=Nov 21 2026 19:16:29`）

- 这份构建尚不含 Win7Dwm 合成对齐：启动行没有 `win7DwmAlignSetting`，所以卡顿与该功能无关。
- 21:05:24.5 到 21:06:24.7 之间没有任何日志，正好约 60 秒。
- 恢复后第一条 UI3Diag：主栏 `commitGapMs(raw/active)=60805.488/71.165`，但 `batchMs max=71.159`。也就是说 UI3 线程没有卡在批次里，这一分钟内没有任何请求让主栏重绘。由此推测卡住的更可能是窗口/输入所在线程，但 Info 级日志无法确认。
- 同机每次选择 ↔ 画笔切换，Draw3Gate 的 `wait → ready` 都要 1.2～1.6 秒；同类操作在 LatencyDiag 那台 WARP 机器上约 10ms。
- 该机 UI3 同为 WARP 且较慢：主栏单帧 drawMs 约 68ms，设置页每帧 25～50ms；设置页打开期间 UI3 线程每秒约 900～1000ms 都在绘制。

### 待办

- 日志完善后，请用户以 Debug 级复现，并说明“切换状态”具体指哪个操作、卡住时鼠标和其他程序是否可用。
- Debug 级可见 `DrawpadSurface`、`Draw3.Input`、窗口命令等记录。

---

## 3. Optimus 笔记本未选用 NVIDIA 独显，Draw3 与 UI3 落到 WARP

### 现象（LatencyDiag 环境与 Inkeys 日志）

- 机器：Acer Aspire 4752，Win7 Ultimate SP1。
  - `adapter0` Intel HD Graphics Family（HD 3000，FL10_1）；
  - `adapter1` NVIDIA GeForce GT 630M（Fermi，FL11_0）。
- 两个适配器通过 `CheckInterfaceSupport` 查到的 UMD 版本相同：都是 `9.18.13.3235`，即 NVIDIA 332.35。这说明 Intel 适配器的 UMD 已被 NVIDIA Optimus 分流层接管，也就是 Optimus 机制在这台机器上存在且生效。
- Inkeys 日志：`[Draw3Diag][device] driver=Hardware levels=11_1,11_0 attempt=preferred hresult=0x887a0004`，之后 `Falling back to WARP`。0x887A0004 即 DXGI_ERROR_UNSUPPORTED：默认适配器（Intel）达不到 11_0。

### 调查结论（代码）

- Draw3 `Draw3.GraphicsInitialization.cpp` 的 `CreateCompatibleDevice`，以及 UI3 `RenderPipeline.cpp` 的设备创建，都调用 `D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, ...)`，也就是**始终使用系统默认适配器**。
- 产品代码里没有任何适配器枚举或选择（`EnumAdapters*` / GPU 偏好）。
- `Inkeys.exe` 也没有导出 Optimus 的 `NvOptimusEnablement`，或 AMD 的 `AmdPowerXpressRequestHighPerformance`。
- 因此在 Optimus 机器上，若用户没有在 NVIDIA 控制面板为 Inkeys 指定独显，驱动会把默认适配器交给 Intel 核显。核显只有 FL10_1，硬件设备创建失败，最终回退 WARP。独显完全没有被尝试。
- 11_0 下限是设计约束（激光粒子、渲染器等要求 11_0），不是本问题的原因。原因是没有向 Optimus 请求独显。

### 候选方案（未定案，需实机验证）

1. **先做零代码验证**：在该机 NVIDIA 控制面板“管理 3D 设置 → 程序设置”中，把 Inkeys.exe 设为“高性能 NVIDIA 处理器”，然后确认三点：
   - 画布信息显示 Hardware 与 NVIDIA 适配器；
   - Win7Dwm 透明呈现正常（Optimus 会把帧复制到核显输出）；
   - 用 LatencyDiag 复测延迟与功耗，判断复制开销是否可接受。
2. **exe 导出请求符号**：导出 `NvOptimusEnablement = 1`（NVIDIA 驱动 302+）和 `AmdPowerXpressRequestHighPerformance = 1`。
   - 风险 1：静态导出对所有混合显卡机器生效，Inkeys 常驻期间独显一直通电，影响笔记本续航和发热。
   - 风险 2：Win10+ 混合显卡上，透明窗口的跨适配器合成可能增加拷贝延迟。
   - 需要评估是否只在“默认适配器不满足 11_0”时才需要独显；但静态导出做不到按条件生效。
3. **运行时显式选卡**：默认适配器创建失败或不满足 11_0 时，枚举其他硬件适配器，选支持 11_0 的建设备。
   - ULW 路径是读回 CPU 再 UpdateLayeredWindow，不依赖跨适配器呈现，理论上可直接受益。
   - Win7Dwm 的 HWND 交换链在 Win7 上跨适配器呈现能否成立，取决于 Optimus 驱动，必须实机验证。
   - UI3 RenderPipeline 要同步处理。

---

## 处理安排（用户 2026-11-21 决定）

- 第 1 项：**画布架构重置后**单独开“置顶专项”处理，连同此前 Win7 置顶问题一起。当前不修改。
- 第 2 项：日志完善后再请用户复测，届时依据 Debug 日志定位。
- 第 3 项：已记录并完成代码调查；方案待实机验证（先做候选方案 1）后再定。
- 第 3 项后续（2026-11-21 用户决定并已实现，未提交）：
  - 采用候选方案 3：性能页新增“GPU 偏好”下拉框，选项为自动（推荐）、优先核心显卡、优先独立显卡，对应配置键 `Experimental.Inkeys3.Draw3.AdapterPreference`。
  - 自动按 DXGI 枚举顺序尝试，0 号是主显示器适配器；优先某类时，先试该类全部适配器，再试另一类。第一个能建出 11_0 设备的即采用，最后才回退 WARP。
  - 核显/独显的判断：Win8+ 读 D3DKMT 混合显卡标志；没有该标志时，独占显存 ≤ 512 MiB 判为核显。
  - 合同见 `spec/native-desktop/draw3-integration.md` 与 `settings-performance-and-drawing.md`。
  - 仍需在该 Optimus 机器上实机确认：自动档能否选中 GT 630M，以及选中后 Win7Dwm 透明呈现是否正常。
- 第 3 项实机结果与调整（2026-11-22）：
  - GT 630M 上可以运行，但帧率与延迟都远不如软件加速（WARP 跑在 i5-2450M + HD 3000 上）。
  - 原因分析：GT 630M 没有显示输出，DWM 在 Intel 上。Win7（WDDM 1.1）没有跨适配器共享资源机制，Win7Dwm 的 SEQUENTIAL 交换链每次 Present 都要由 NVIDIA 驱动私下把整帧经 PCIe 中转到核显的窗口表面，可能同步阻塞帧循环；硬件路径还多出 GPU 排队与拷贝时间，Win7dwm 合成对齐按 CPU 提交耗时估算会系统性错过合成；独显在轻负载下停在低频。Inkeys 每帧渲染很轻，独显算力优势体现不出来。ULW 走标准 GPU 回读、只传脏区，预计好于 Win7Dwm，但仍要等 GPU 完成，轻负载下难以超过 WARP。
  - 独显直连（核显被屏蔽、显示器接在独显上）时 DWM 与渲染同卡，无跨卡拷贝，不受此影响。
  - 用户决定：“自动”与原逻辑完全一致，只用默认适配器（`D3D11CreateDevice(nullptr, HARDWARE)`，即 0 号，尊重用户在系统/驱动中选的高性能档位），失败即回退 WARP，不再尝试其他显卡；手选“优先核心显卡/优先独立显卡”保持依次尝试。
