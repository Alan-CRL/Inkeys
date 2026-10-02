# Implementation Plan: Win7 主画布恢复

## Phase 0 — Preserve and Identify

- [x] 复核当前 HEAD、branch、tracked/untracked 状态；保留已有 research 文件和 `inkStrokeModelerTest/` 四个 `.cso`。
- [x] 记录候选输入的 SHA-256、文件版本、PE 架构、配置及修改时间。将 `G:\Inkeys` 当前 EXE 与本机旧 x64 Release 同 hash 作为“二进制相同”记录；未获得可验证的 Git SHA 前不写成“由 f420 构建”。
- [x] 选取可隔离的新构建输出目录并检查 `PptCOM.csproj` 的 `ZhjOutputDir` 和 Draw3 shader `ObjectFileOutput`；确认不覆盖旧测试包、既有输出或四个 Demo `.cso` 后再构建。

## Phase 1 — Trace and Reproduce

- [x] 复核 `IdtMain.cpp` 窗口创建、Draw3 启动/首帧检查和启动预览边界；复核 `Window.cpp` 生命周期/双窗显隐，以及 `IdtState.cpp` Primary/Presentation/Hidden 决策和重试门控。
- [x] 复核 `Draw3.Host.cpp` Graphics/Presenter/Controller/FirstFrame stage，`Draw3.GraphicsInitialization.cpp` Hardware/WARP 尝试，以及 `Draw3.TransparentPresentation.cpp` 普通回退、PrimaryDrawpad 与 SelectionUlw 两套 ULW presenter 和最终参数。
- [x] 使用当前产品路径可用的 headless/非交互测试，对主窗生命周期、书写态可见、选择态穿透、目标切换和首帧失败分类做修复前验证。先保存任何自动失败输出。
- [x] 对照 Win7 附件判定：当前启动已到达普通窗口/线程初始化；硬件失败 HR/阶段、Draw3 主 target 可见/提交结果和状态机显隐尚缺。不要将 waitable `0x887a0001` 或 Bar 成功帧记为根因。

## Phase 2 — Minimal Resolution

- [ ] 若失败可稳定复现且因果路径已证实，修改最少生产代码/相应测试，保留修复前失败证据，并以同一自动检查确认恢复。当前环境未满足该条件。
- [x] 若当前机器不可复现，增加有限 Draw3-only 诊断：构建 identifier；Hardware/WARP 初始化失败阶段/HRESULT；两窗身份与生命周期/状态转换；首帧及目标/门控状态；实际主画布 presenter 参数、HRESULT/GetLastError 和成功/失败结果。
- [x] 日志只在 startup、surface/target 状态变化、首帧 attempt、失败/恢复记录；复用现有接口，不记录每帧、不新建通用诊断层。
- [x] 修改/审查期间保证交换模式、DComp/ULW、HTTP(S)、旧 EXE 兼容、PPT 数据保护、画质/动画设置和四个 Demo `.cso` 均符合 PRD。

## Phase 3 — Verify and Handoff

- [x] 先确认本机构建系统及 ARM64 MSBuild 可执行路径；按根 `AGENTS.md` 在同一 PowerShell invocation 规范化 `PATH`，构建完整 `InkeysRepo.sln Debug|ARM64 /m:1`，超时至少 5 分钟。尽量使用隔离 `OutDir`/`IntDir` 并重定向 PptCOM 输出；若无法保证不覆盖现场，停止该构建并报告具体冲突。
- [x] 运行适用的 `InkeysHeadlessTests.exe --no-window` 与受影响现有 Draw3 测试；只运行不要求人工关闭交互窗口的检查。
- [x] 查看 `git diff` 与状态：四个用户留存的 Demo `.cso`、既有 research 文件、旧测试包及未提交文件须原样保留；确认没有无关格式/源码变化。
- [x] 生成交付记录：当前完整源码 SHA / working tree 状态、EXE SHA-256、PE 架构、配置、diagnostic build identifier、构建/测试退出码。
- [x] 给用户最短实机步骤：Win7 SP1 + 仅 KB2670838 启动当前书写模式；确认主画布出现并画一笔；切换选择确认桌面可点击；定格开启/关闭后回到书写并续画；有限确认既有 PPT 流程。附期望现象、需回传的完整 IDT log 与 console output，并逐项标注“待用户实测”。
- [x] 只有发现受影响的 PPT 连接点时才运行对应有限回归；不重新展开整套 PPT 重构。当前未发现可将隐藏 fixture 失败归因到本轮主画布诊断的连接点。

## Stop Conditions

- 已交付经当前环境构建/回归的诊断候选；仍需 Win7 现场反馈才能定因。
- 不 commit、push 或发布。

