# Win7 WARP 默认完整 shader：独立审查（2026-10-09）

继续原任务。本分工已是 trellis-check，不再次派发代理；与 GPU/collector 实施者协调 code-stable 后独立审查，只拥有本报告和最终 quality-formal-warp-20261009.md。未改他人源/报告/原始资料，没有构建、GUI、Computer Use、commit 或 push。

## 静态结论

正式自动选择符合 formal-warp-plan-20261009.md：真实 OS6.1 + 实际 WARP 默认使用同一现场验证的完整字节数组；其他 OS、Hardware/unknown 默认沿正式原资源。诊断才允许精确0/1覆盖。未发现静态阻断问题。最终完整构建、真实原始日志、资源与保留合同已独立复核，当前变更质量门FINAL PASS，范围与残留问题详见quality-formal-warp-20261009.md。

## 真实 driver 与 OS 数据流

- `Draw3.GraphicsInitialization.cpp`原创建流程只在硬件成功后记录 Hardware，硬件失败且 WARP真正成功后记录 WARP；重建开头 resources={}，默认driver UNKNOWN。此文件未为本次修改。
- `Draw3.TransparentPresentation.cpp:1003–1004`将同一 GraphicsDeviceResources 的device/context及driverType传给renderer。没有按adapter名称、vendor或OS猜driver，也没有把独立Hardware探针失败改成WARP成功。
- `Draw3.Renderer.cppm:334`的Init新增必传driver，private LoadShaders同步接受driver；Renderer.cpp:514和最终LoadShaders调用贯通，无新增存储状态与过期driver。库中生产唯一Init及两个fixtureInit均已改：pixel probe用实际请求driver，laser fixture用实际D3D_DRIVER_TYPE_WARP。独立demo自己的renderer仍是另一模块/接口，不把其旧调用误当缺失。
- Renderer全局module fragment包含既有IdtStart.h；LoadShaders直接调用GetWindowsVersion()。IdtStart.cpp:80–106通过GetModuleHandle/GetProcAddress RtlGetVersion读真实版本；没有GetVersionEx manifest谎报，也不依赖正常wWinMain才写入的全局硬件/version状态。查询失败返回0/0，自动策略回原资源，不误启兼容PS。

## 精确选择与失败传播

新 `Draw3.ShaderCompatibility.h`仅619bytes，提供共享constexpr选择函数。真实LoadShaders和27项回归调用同一函数，不复制逻辑到测试。

- diagnostics通过原GetEnvironmentVariableW长度2缓冲读取，只接受长度恰好1、字符1；guarded同样只把长度恰好1的原值传进选择函数。
- diag启用时guard0强制original、guard1强制guarded。unset/空/00/01/true/尾空格等非法值回自动策略；diag未精确开启时任何guard值都不覆盖auto。
- 正常Win7 WARP无环境变量即可选完整guarded；Win11 WARP、Win7Hardware/UNKNOWN/REFERENCE、未知OS等保持原正式资源。
- 选择使用真实Bytecode和sizeof，没有换成NoThin/FullWithoutDefaultInk/CursorOnly；正式VS/PS资源、compute粒子与其失败合同未变。候选CreatePixelShader失败仍返回false，未静默回原版。
- SHA报告计算的是实际被选字节，仍仅在诊断初始化进行；普通启动新增一次OS查询和选择，不新增每帧日志/hash/判定。

## Probe 原版基线与必要回归

HiddenWindowTest.cpp:2145之后的27个PolicyCase覆盖Win7/其他OS、WARP/Hardware/UNKNOWN/REFERENCE、diag on/off和guard0/1/空/非法值。独立 `[Draw3ShaderPolicyTest]`前缀不计入105个GPU终态；策略失败返回1，不覆盖基线/候选结论。

RunRendererPixelTestForDriver在任何初始化前用两个RAII保存diagnostics和guarded环境，再明确设置diag1+guard0。保存对未定义与存在但空值分别处理，获取缓冲包含末尾null并缩为实际字符串。设置任一变量失败也通过已构造RAII恢复；后续API失败、硬件unavailable、例外和所有return均恢复两个父值。原87仍在原正式PS上测试，18候选仍独立直接Create同一完整数组。原来的StartupEnvironmentDiagnosticsEnabled内部状态另有RAII恢复。

没有新增硬件回退、105/69/18、900000ms、GPUquery预算或schema调整。完整105终态仍必须和production/diagnostic/sequence/matrix/guarded摘要分别核对；独立policy日志不放大case计数。

## Shader 字节与效果保留

本次不改变formal HLSL、四个formal CSO、23个旧诊断数组、生成器或成功guarded数组。独立从header解析数组得到28980bytes/SHA `2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384`；header文件SHA仍 `7B4070188CFB007CA59AE5639F3CA91D247D6D4A3F87D811ACC459415DB9F563`。

已有check-aero-guarded-20261008.md对真实CSO重新dump、ISGN/OSGN/17声明和完整数学唯一wrapper的独立证据仍有效。本轮没有重新生成另一块程序，所以保留实际现场通过的控制结构/数学/薄线/AA/光影/工具/双源公式；不能将正式默认选择改动说成又删了shader功能。最终binary-and-preservation.json已验证两EXE的正式SHADER301–304与四CSO逐字节相同；本分工再次解析精确guarded数组、对87不可变资料及8个他人文件逐SHA比对，全部保持。

## Collector 与 PS2 合同

稳定wrapper SHA `D6891A7ED72FA3ED046D1A01E43CA68995805550270C5BF406DCFF3F7C871C57`；README最终SHA `3E64770A3F94995F528644DFA61B55E17028054E30939CE8356082A45FB9954F`。Collect和唯一CMD仍为前轮相同字节。

Wrapper新package id为Inkeys-Win7-WARP-Fix-x64-Release。diag1，guard三个stage明确0/0/1，ULW full三阶段空；manifest严格新身份和原105/69/18，阶段identity核真实0/0/1。finally恢复调用者三个值，未持久化诊断配置。采集状态、失败继续、完整双流、Hardware unavailable与超时清理合同保持。

README区分正常双击EXE的auto与CMD原版对照；Part2已知失败限定旧Win7WARP，不泛化Hardware；新EXE尚需正常启动回归、先前已验收的是同一shader字节，定格荧光偶发和Hardware边界保留。

实际WinPS5.1/mock47项与冻结3项PASS已独立读取其结果及最终源身份：含0/0/1四调用、已有值及原先不存在变量的finally恢复、错误继续/身份拒绝等。冻结结果记录的README是文字澄清前6A48...版本，三个执行脚本与最终字节相同；final-source-identity明确此元数据差异。没有重写原结果或为文字变动重复运行50测试。PS2 API静态子集未变；本机WinPS5.1不冒称Win7PS2实测。

## Spec / 构建边界

input-and-ink.md新增2026-10-09当前默认合同，与源码一致；2026-10-08段明确是历史诊断合同，避免两个guard规则互相混淆。draw3-shaders.md链接当前合同并要求未来数学改动同步重生成兼容数组与目标环境验收，防止正式PS更新而Win7公式滞后。

首次Debug ARM64 solution在91.54s出现C1902 compiler/PDBmanager环境错误，原失败独立保存。主会话以相同源码/配置/工具链在允许执行环境重试，27.63s exit0；Release x64完整solution276.4s exit0，没有源码或工具链绕过。第一次失败仍作为环境证据保留。原始日志复核四次本机GPU105/策略27全部PASS，两Headless exit0；实际WinPS5.1继承diag1+guard1仍强制原版probe，两个后端105/27全部PASS。当前FINAL PASS仅覆盖正式默认落地的本机质量门，不等同新增EXE已在Win7实际正常启动验收。

## Findings (fixed)

本分工未发现需要修改生产实现的机械缺陷。指出新spec‘未来源数学变化’文字遗漏，root作为owner补成‘未来源码数学变化’，保留原编码换行；不改选择设计或数学。

## Findings (not fixed)

- 新正式EXE默认选择分支尚未在Win7正常启动回归；已有两Aero条件验证的是旧EXE同一完整shader字节。纯选择矩阵与本机GPU不能替代新EXE光学验收。
- 精确旧WARP内部JIT/opcode原因、Win7Hardware及一次不可复现荧光定格变深保留；本修复不修改窗口/ULW/输入来猜测解决。
- 新EXE需要在目标Win7直接启动回归自动选择；本机实际ProbeOnly有意强制original，用共享策略27验证auto分支，不把ProbeOnly成功写成正常启动光学证据。

## Verification

- Lint/静态：限定git diff --check exit0；8个相关源/文档的UTF8、原BOM、纯CRLF检查通过；driver/OS/选择门/probe/collector源合同独立通过。
- TypeCheck / 完整构建：Debug ARM64、Release x64完整solution均exit0；本分工读取真实退出/日志，没有重复构建。
- Tests：四次本机105GPU与27策略全部PASS；72个完整candidate比较全部reference available/mismatch0/delta0；两Headless exit0。实际WinPS5.1另有两后端各105GPU/27策略PASS、36个candidate比较delta0，继承guard1但实际基线仍为original。collector50mock实际PASS。最终独立质量状态FINAL PASS见quality-formal-warp-20261009.md。
