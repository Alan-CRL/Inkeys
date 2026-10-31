# Win7 guarded 候选独立审核（2026-10-08）

继续 `.trellis/tasks/10-02-win7-main-canvas`；本审查由 trellis-check 执行。GPU 与采集器分别由其他实施者负责，未回退他人变更。当前审核只写本报告与对应 quality 报告，不修改正式 shader、探针旧数组或应用实现，不运行 GUI、不提交。

## 审核结论

静态部分通过。真实首轮回归发现并协调修正一个新 fixture 的机械断言错误，产品 shader 没有改动；修正后的两架构构建与双后端105项/Headless重测已通过。Win7 修复验收仍待用户实机执行原版/候选两阶段；本机与采集器的最终结果见冻结 quality 报告，包身份在ZIP后另核。

## 数学保持与实际编译字节码

独立读取正式 `Assets/inkPixelShader.hlsl`，按唯一允许的源变换重新构造整个临时源；与实际 `gpu-fxc/GuardedDefaultOutput.hlsl` 完全一致。变化只有将 type7 之后的 DefaultInk 输出尾部缩进到 `type==0 || type==3 || type<0 || type>20` 的正向 if，并添加不应到达的透明 identity fallback；prefix、helper、所有其他分支与该尾部内部所有数学逐字符保持。细笔的真实 SV_Position、双边距离、AA、smoothstep、宽度变化和双源 Add/Retain 都保留。

Unknown type 原先在 d=0 时执行 DefaultInk 尾部；新条件包含负值与大于20，保持该行为。其余已知 type1..20 全部有原提前返回，因此 fallback 不替代任何现有绘制功能。参考 `Generate-Win7-GuardedShader.ps1:41` 和 `Draw3.Renderer.cpp:645`。

真实候选用现有 SDK ARM64 FXC 10.0.26100.0 `/T ps_5_0 /E main /O3` 编译为 **28980 bytes**，SHA256 **2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384**。新 header 数组逐字节等于这个 CSO，hash 常量一致。ISGN/OSGN payload 与正式 PS 相同，17 条实际资源声明与完整正式源码重编译对照相同。879 slots/temp8；DefaultInk 的两导数、thin 分支与两个 discard 的实际 ASM 栈包含尾部正向 if，而非仅凭 HLSL 文本推定编译结果。

该候选与此前仅覆盖0/3的静态草案不同：这次同时保留 unknown type。正常应用没有使用 NoThin、FullWithoutDefaultInk 或精简 Cursor shader。没有承诺编译器改变控制流之后所有驱动的数值结果自动相同，需要局部像素对照及 Win7 实机。

## 选择门与失败传播

`Draw3.Renderer.cpp:49` 读取长度2缓冲并要求环境变量实际长度1且字符1；`LoadShaders:645` 仅在 diagnostics 与 guarded 两值均精确等于单字符1时使用新数组。正式 VS、正式 PS 资源编号、Compute shader 与无诊断流程不改变；候选 CreatePixelShader 失败返回原失败路径，没有隐式回退。

`ReportSelectedPixelShader:56` 对实际被选 bytes 用 BCrypt SHA256 计算 hash，并记录 original/guarded、真实字节数及 Create HRESULT。失败时 hash 明确 unavailable；不能把生成时预期常量冒充当前加载身份。诊断未开不做 hash；固定4096-byte object 缓冲避免 noexcept 路径动态分配。

显式105 probe 先保存并清除 guarded 环境，结束 RAII 恢复（`HiddenWindowTest.cpp:2165`）；之后独立创建候选 shader。这样旧87全部使用原版，不被继承的候选开关污染。边界是：105 probe 验证候选 PS 的实际 GPU 执行，但不执行正常应用 LoadShaders 的 guarded 选择分支。本机禁止交互 GUI，不能声称正常应用的候选选择已实机验证；wrapper env/source 合同明确，Win7 两应用阶段将保留实际 ShaderSelection 日志供核对。

## 新18项验证的数据流与诚实判定

`HiddenWindowTest.cpp:2875` 后新增9场景×backbuffer/offscreen。每项先在同 fixture 画原版，再画完整候选；使用现有正式 VS、绘制调用、上传字段及实际双源 resolve blend，并核 OMGetBlendState 对象和 sample mask。既有87、matrix69、旧23 shader 数组保留。

场景含 cursor production/noBlend/nonzero retain，以及真实 type0 round、r=.35 thin、r=.35→3 varying radius、type3 highlighter、nonzero destination erase/draw retain。透明目标核预乘比例、中心、四角、覆盖与 fractional AA；nonzero destination 为 RGBA(64,128,192,255)，验证中心与底图边角和 erase 的部分保留。

这种隔离 fixture 暂时以 resolve blend 直接验证 `Add + Retain * D`，不是完整产品的 operator-MAX/MIN 累积/history/snapshot 回归；后者需两应用阶段实际操作，不将该部分包入局部像素通过结论。

回读仍用 canonical RTV identity、staging CopyResource、EVENT/Flush/3秒有界wait、DO_NOT_WAIT Map、RowPitch；新增紧密 BGRA 整幅 snapshot。原版独立正确性失败则 `reference_status=unavailable / comparison=unavailable`，候选只可报告自己的正确性，不报告与坏基线等价。原版正确时全图每通道差≤1，超过即失败。每项终态唯一，guarded summary 与旧 production/matrix 分开（`HiddenWindowTest.cpp:3028`、`:3056`、`:3070`）；CLI仍保留原版失败，同时纳入 guarded18完整性与失败数。

## 采集器与包合同

已复核冻结 source：Collect SHA256 **13B3A7B27D8733D5FEDC317E63E0B5783C055C858A27FEFD6E97E7C410EC639F**；wrapper SHA256 **D00102B5C238CF87BE5FCB9E1AFC878CE7C9ECCB1CC06292092D63822DF6BEED**。语法保持现有 Windows PowerShell2 API 子集，真实 WinPS5 mock 不能冒称 Win7 PS2 运行通过。

每后端900000ms，有界 wait/owned child kill/dispose；170旧query最坏510秒加36新EVENT最坏108秒，共618秒只是 query 预算，不保证驱动/JIT整体无阻塞。只停止当前创建的 child，不按进程名终止其他实例。

Schema要求 guarded-20261008、105/69/18；driver identity/header唯一。guarded18完整终态、独立 summary 和失败/不可比较数互相核对，缺失/重复/不一致算 incomplete，不能从一句 summary 生成 PASS。硬件 exit3且有效身份明确 unavailable，不能当像素PASS。原版 FAIL 仍诚实保留并继续两个应用阶段。

Wrapper 三stage为01-pixel-probe、02-original-shader、03-guarded-shader；diagnostics=1全部，guarded off/off/1，ULW_FULL unset全部。finally恢复父进程 diagnostics/full/guarded原值。新版 pkgid Inkeys-Win7-Guarded-x64-Release、capture win7-guarded-1、expected105/69/18与manifest/hash同步。普通阶段 complete 仅为身份及原始资料完整，并非可见通过。

## 待最终核验

Debug ARM64 / Release x64完整Solution最终exit0，两架构独立WARP/Hardware各105终态全PASS、18对图像delta0、36独立公式检查PASS，Headless各exit0；实际WinPS5.1最终EXE ProbeOnly双驱动105全PASS，真实actual original hash与正式CSO一致；采集器47mock+3冻结复验+owned-child timeout均PASS。见冻结quality-guarded-20261008.md。后续最终manifest/ZIP身份另核，Win7 WARP恢复与效果、VMware硬件刷新症状都保留实机边界。

## Findings (fixed)：光标描边 oracle

首次实际 ARM64 WARP/Hardware 原始记录各有4个新光标检查失败：两个目标的 cursor.production/noBlend。原版与候选都读到848覆盖、正确128中心与透明角，唯一失败为396个整图 premult_formula_errors。原断言将整幅 Circle 当成白色，而固定 fixture 的 outline 是184/255灰色、宽0.5，合法描边被误判。

独立审查发现后立即通知 GPU 文件所有者，由其在冻结协调下修正 `HiddenWindowTest.cpp:2961`；未修改 shader。新 oracle 按像素中心、圆 signed-distance、outer/inner线性覆盖、灰描边/白填充计算全部四通道 Add 和非零底图 Retain，容差1.01字节。与正式 HLSL `CursorCoverage:144`、cursor `:269` 的固定场景逐项核对；32×32圆和0.5opacity/outlineWidth均一致。仍保留中心、覆盖、四角以及原版/候选整幅≤1差值检查，并非简单放宽为 RGB≤Alpha。

首轮4份失败双流保存在 `ARM64-Debug/tests/first-cursor-predicate-failure/`，不删除失败证据。修正后的 HiddenWindowTest SHA256为 **4A87B344B9BEA01F6CF4ECAC0F99496B42272CDC2E82E791D4805EA08914DF2C**，沿用无BOM UTF8+CRLF；候选CSO、header和shader数学身份保持。

## Final package audit（2026-10-08，质量报告冻结后）

独立只读核验最终 ZIP 通过，未改 frozen quality 报告，因此 manifest 的 source hash 不形成循环。

- 文件：win7-guarded-20261008/Inkeys-Win7-Guarded-x64-Release.zip；**29300415 bytes**，SHA256 **07B744726DE1B3BA8E04AE20F026AF01B2FAE67B03BBAFEB80573DD9D65902A4**。
- ZIP testzip/CRC通过；9个entry与输出目录对应文件逐字节相同，恰好唯一 Run-Win7-Diagnostics.cmd。无旧Full-ULW/PixelTest CMD混入。
- manifest schema guarded-20261008、capture win7-guarded-1、expected105/matrix69/guarded18正确；8个包文件的bytes/hash全部匹配，全部source_hashes与当前源逐一匹配。
- frozen quality SHA **FD14F3EB66259860EE970216B76D7856FA6272DA8E7D53E2C14708C0B3D13ADD** 在manifest中一致，未在包后重写。
- ZIP与目录中最终EXE均为 **23D6E5F811821A4E27CFDCD18A00BDF1B996886F2DE1AAE48154D40D63F0C547**；28980byte完整候选blob实际存在于该EXE，SHA2171...5384与header/manifest一致。正式资源保持证据同上。
- manifest明确win7_status=pending、verification environment为Win11/WinPS5.1并说明Not Win7；本机质量门不冒称Win7修复。

本次质量门与单CMD交付身份PASS；未执行GUI、commit/push/archive。下一步用户同包两Win7实测，任务继续in_progress。
