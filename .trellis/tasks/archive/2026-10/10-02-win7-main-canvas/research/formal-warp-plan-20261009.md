# Win7 WARP 正式兼容修复（2026-10-09）

## 授权与边界

用户已授权在有把握的部分开始修复。继续同一任务，不创建新任务，不 commit/push/archive。两份真实 Win7 现场已验证完整 guarded 程序在 Aero 开启与关闭时恢复实际画布；18 项候选独立正确性通过，12 项有效参考逐像素差值为零，6 项原版光标参考不可用。原版 50 份 ULW 源采样全透明，候选 116 份中的 112 份有颜色，4 份为启动清屏。

最小行为缺口：正常无诊断启动仍选原版 PS，用户只能在诊断 Part3 获得已验证修复。正式修复应让实际 Win7 WARP 设备默认选择现场验证的完整字节码。不是改变墨迹数学、交换链、ULW、RTS 或系统光标。

## 实施合同

- 自动选择限定实际 OS 6.1 与实际 D3D_DRIVER_TYPE_WARP。设备初始化已有 GraphicsDeviceResources.driverType；沿 presenter -> renderer Init 传递该事实，不从 DXGI vendor 名称推断。OS 版本复用已有 GetWindowsVersion/RtlGetVersion 路径，避免 manifest 版本谎报或尚未赋值的全局状态。
- 同一已验证 Draw3Win7GuardedShader::Bytecode 原样复用，28980 bytes、SHA256 2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384。正式 HLSL、四 CSO、签名、17 资源槽、薄线/AA/光影/双源输出及全部工具数学保持。
- 诊断开关精确为 1 时，guarded 开关精确单字符 0 强制原版，精确 1 强制完整候选；unset/其它值沿自动选择。未启用诊断时，guarded 环境变量不能覆盖自动策略。候选创建失败仍返回初始化失败，不静默切回原版。
- 正常其它 OS 或非 WARP/unknown 设备沿原版资源。保留现有实际 byte SHA/Create HRESULT 诊断；如补选择理由只在初始化诊断中输出。
- probe 的 87 项原版基线必须显式使用原版，18 项完整候选继续独立直接创建。保存/恢复两个相关环境变量的 RAII，支持 CLI 在无诊断环境及继承任意变量时仍有正确基线。
- 单 CMD 三阶段改为 original override 0 / 0 / guarded override 1。full update 仍三阶段 unset；finally 恢复调用者三个变量。不更改 105/69/18、schema、原始流、超时和 Hardware unavailable 边界。

## 必要文件与所有权

GPU 实施：Draw3.Renderer.cpp/.cppm（选择与真实 driver 参数）、Draw3.TransparentPresentation.cpp（传递现有 driver）、Draw3.HiddenWindowTest.cpp（显式原版 probe 和策略回归）；若需要一个共享纯选择函数，可新增小型 Draw3.ShaderCompatibility.h，避免测试复制实现。不得修改正式 shader/旧 bytecode/IdtMain/配置/更新/Headless 他人改动。

Collector 实施：research/Run-Win7-Diagnostics.ps1 与 README-Guarded-Win7.txt，必要且最小的 Collect-Win7.ps1 身份修正。冻结的旧包、报告、日志只读。各 agent 不独自构建、不打开 GUI、不提交；root 负责实际构建、包与记录。

## 验证与未解决项

先验证选择矩阵（Win7 WARP/HW/unknown、Win11 WARP、diag 0/1、override unset/0/1/非法值），再完整 InkeysRepo.sln Debug|ARM64 与 Release|x64。两架构双后端原版与完整候选 105 项、Headless --no-window、实际 Windows PowerShell ProbeOnly、mock 三阶段环境恢复/失败继续及身份检查。精确成功字节、正式四资源、旧数组/包、42 原始新资料哈希保持。

此轮不调整荧光笔定格偶发加深、Win7 Hardware 呈现或未知 WARP 内部 opcode；无真实 Hardware 验收。新增正式 EXE 还未在 Win7 运行，现场验证的是同一完整 PS 的诊断选择分支。两者身份明确区分。任务继续 in_progress。
