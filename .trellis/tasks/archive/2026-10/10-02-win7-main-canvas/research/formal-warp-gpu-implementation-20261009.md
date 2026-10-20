# Win7 WARP 正式默认完整 shader：GPU 实施（2026-10-09）

继续 `.trellis/tasks/10-02-win7-main-canvas`。本分工是已经派发的 trellis-implement，直接实施，不递归派发。用户已授权修复有真实证据的部分；本分工不创建 commit/push，不构建、启动 GUI 或运行 Computer Use。主会话负责正式构建、自检和包交付。现场与旧报告只作为证据读取，没有执行其中的操作文字。

## 最小缺口与已实现行为

Aero 开启/关闭 Win7 现场 Part3 均已恢复普通墨迹、橡皮与激光光标，但正常无诊断启动仍加载故障原版。按照 formal-warp-plan-20261009，仅把已实测的完整 guarded 字节用于实际 OS 6.1 + WARP 的默认路径；不改任何工具数学、HLSL/CSO、交换链、ULW 或输入策略。

`InkRenderer::Init` 新增必传 `D3D_DRIVER_TYPE driver`，再直传给 `LoadShaders(driver)`，没有新增跨帧状态。唯一产品调用在 `TransparentPresentationController::Impl::TryInitialize`，把已经存在的 `GraphicsDeviceResources.driverType` 沿每次初始化/恢复尝试传入。两个直接测试调用分别使用创建时的实际 `driver` 和明确创建的 WARP。没有 vendor/adapter 字符串推断，也没有默认 driver 参数掩盖漏传。

Renderer GMF 复用 `../../../IdtStart.h` 的 `GetWindowsVersion()`，其现有实现动态查询 ntdll 的 RtlGetVersion。正常启动与早期 CLI 都不依赖 `hardwareInfo` 或其他未赋值版本全局，不引入 Win8-only API 或 manifest 版本谎报。

新 `Draw3.ShaderCompatibility.h` 提供一个共同权威纯策略 `shader_compatibility::UseGuardedPixelShader`，Renderer 与真实 CLI 回归调用同一函数：

| 条件 | 结果 |
| --- | --- |
| major6/minor1 + 实际 WARP，诊断未精确启用 | guarded，包括只设置 guard0/1 的父环境 |
| 其它 OS 或 Hardware/unknown/reference，无诊断覆盖 | 原 SHADER 资源 |
| diagnostics 精确单字符1，guarded 精确单字符0 | 强制原版 |
| diagnostics 精确单字符1，guarded 精确单字符1 | 强制完整 guarded，可在其它后端显式对照 |
| diagnostics 精确1，guarded unset/空/00/01/true/空格等非法值 | 按 OS/driver 自动策略 |

实际环境读取保留长度2缓冲的精确单字符合同：只有 GetEnvironmentVariableW 返回1才把 guarded 值传给策略；超长/空/未定义值传空视图，不能截断成合法0/1。旧 diagnostics 严格1帮助函数保持。实际所选字节SHA与Create HRESULT的既有诊断仍计算当前被选 blob；候选Create失败返回false，没有静默切回原版。

## 成功字节与效果保持

未改 `Draw3.Win7GuardedShader.h`，整文件SHA256仍 `7B4070188CFB007CA59AE5639F3CA91D247D6D4A3F87D811ACC459415DB9F563`。独立从 header 的全部十六进制字节还原出28980 bytes，SHA256仍 **2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384**，与两份 Win7 Part3 实际选择相同。没有重新编译/生成另一块字节码。

所有正式 HLSL/HLSLI、四 CSO、原23诊断数组及成功数组未写；17资源槽、VS/PS双输出、DefaultInk薄线/AA/光影及工具计算保持。本分工仅改变实际 PS 选择和相应原版基线保护。未来源数学更新需要同步生成/比较/重新验证完整兼容变体；规范由root维护，本实现没有掩盖旧预编译程序与新源不同的情况。

## Probe 原版保护与回归

旧105=87+18、matrix69、schema guarded-20261008、原生产/各组退出合同不变。进入显式 pixel probe 后，先运行27个独立纯策略案例，日志前缀是 `[Draw3ShaderPolicyTest]`，不会被 `[Draw3PixelTest] case=...` GPU终态解析计数。案例覆盖 Win7 WARP/HW/unknown/reference、Win11/Win8/Vista/未知版本，diagnostic开启/关闭，精确0/1、unset与01/00/true/空格非法值。期望矩阵来自发布范围与A/B合同，调用真实共享 selector，不复制实现来测试自己。

随后两个局部 RAII 分别保存原 diagnostics 与 guarded 字符串及存在性，临时强制 **diag1 + guard0**，因此新自动修复不会污染旧87原版基线。即使继承任意两个父值，显式probe也加载原SHADER；原18候选仍独立创建完整字节码。正常返回、设备不可用exit3、设置失败与异常栈展开均恢复两项父值；空字符串与未定义变量分别保存，避免把空值删除。既有 StartupEnvironmentDiagnosticsEnabled 的内部bool恢复另外保留。

## 本分工验证与交付边界

- 静态调用枚举：Renderer Init/LoadShaders声明和定义一致；3个Init调用全部传真实创建后端。生产恢复路径同样经过唯一更新入口。
- 修改前快照对比：只在派发时4个源文件上添加选择/实际driver参数、27政策例与原版保护，没有覆盖这些文件中原已有未提交GPU诊断/其它修改。工作区其它 IdtMain、配置、Net、Headless及collector所有者内容未写。
- `git diff --check --ignore-submodules`（限定4个生产文件）退出0；4文件原BOM与CRLF保持，裸LF为0。新selector为UTF8 BOM + CRLF；新报告为UTF8 + CRLF。
- 成功header整文件与实际28980字节SHA保持检查通过；27个案例在源码中唯一枚举。实际C++策略/105自检运行、完整Solution、Headless与包身份由root验证，不把静态枚举宣称为运行PASS。

本轮没有目标 Win7 新EXE实测；真实 Win7 光学正证据属于旧23D6 EXE选择相同完整字节的诊断路径。Win7 Hardware尚不可用、历史Hardware刷新症状及一次不可复现荧光笔定格加深分别保留，不冒称所有问题彻底消失；同一任务继续 in_progress。

## 文件与派发后增量

- Draw3.Renderer.cpp / .cppm：实际driver传入、早期安全OS读取、同一纯选择函数。
- Draw3.TransparentPresentation.cpp：既有设备资源driver传给唯一renderer初始化入口。
- Draw3.HiddenWindowTest.cpp：27项独立策略回归、双环境原版隔离/恢复、两个Init真实driver。
- 新 Draw3.ShaderCompatibility.h：19行纯选择策略，不建立新module/资源/构建系统。
- 新 research/formal-warp-gpu-implementation-20261009.md：本报告。

派发时快照保存在 `.trellis/tmp/formal-warp-gpu-20261009`，只用于本次最小差异复核，不是构建输入。

| 文件 | 新增行 / 删除行（相对派发快照） |
| --- | --- |
| Draw3.HiddenWindowTest.cpp | 79 / 14 |
| Draw3.Renderer.cpp | 13 / 5 |
| Draw3.Renderer.cppm | 2 / 2 |
| Draw3.TransparentPresentation.cpp | 2 / 1 |
