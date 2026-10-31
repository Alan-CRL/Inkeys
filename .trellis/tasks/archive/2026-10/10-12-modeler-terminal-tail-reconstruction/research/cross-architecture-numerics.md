# 阶段 2 跨架构数值调查（只读）

## 范围与结论

2026-10-12，仅检查已完成的 ARM64 Release / x64 Release / Win32 Release 报告、实际 CL tlog、link map 和保留的对象汇编。未修改源码、编译选项、ABI 或测试阈值，未重新构建/运行 GUI。本文件只解释当前覆盖矩阵中的跨架构数值变化，不建立所有输入的跨架构位一致保证。

四配置同架构的原 19 case / 128412 assert 与阶段 1 源码基线逐字段精确一致；原 13 源 / 18 头 SHA 未变（root 的 frozen-baseline 证据）。新的 96 窗口每配置 116105 assert / 0 fail。当前完整新 fixture 矩阵没有在阶段 1 历史 EXE 上重跑，因此不能把新增 stylus 字段差异直接宣称为“历史报告已证明的继承差异”。

ARM64 ↔ x64 的 136 个变化叶字段全部是 corridor 派生 double 诊断，最大绝对差 1.1102230246251565e-16 / 1 double ULP。ARM64 ↔ Win32 的 9730 个变化叶字段按下表拆分；不能笼统称为全部 1 ULP。

## ARM64 ↔ Win32 的完整分类

输入：`Build/modeler-baseline/stage2-evidence/arm64-vs-Win32-differences.json`，以及 `stage2-source/{ARM64-Release,Win32-Release}/tail-replay.json`。

| 分类 | 变化叶字段 | 最大绝对差 | ULP / 说明 |
|---|---:|---:|---|
| 原 Modeler pressure / tilt / orientation | 2172 | 4.76837158203125e-7 | 1086 个 float，每个同时记录 bits/value |
| 新补丁 raw_fraction | 3112 | 4.107825191113079e-15 | 1556 个 double，每个同时记录 bits/value；最大 37 double ULP |
| library_diagnostics double | 2818 | 2.7039441852330867e-10 | 近零 acos 角度存在数值敏感性，不能用统一 1 ULP 描述 |
| 其余物理指标 double | 1628 | 2.842170943040401e-14 | 最大 2 double ULP |

原 stylus 字段进一步按实际 float bits 分类：

| 字段 | 不同 float 个数 | 最大 float ULP | 最大绝对差 |
|---|---:|---:|---:|
| orientation | 242 | 1 | 4.76837158203125e-7 |
| pressure | 352 | 2 | 5.960464477539063e-8 |
| tilt | 492 | 13 | 5.960464477539063e-8 |

最大 tilt ULP 样例：`fixtures[5].all_original_model_results[171]`，`3cbf0dc0` → `3cbf0dcd`，0.023321986198425293 → 0.023322010412812233。最大 ULP 和最大绝对差未必是同一个样例。

完整差异路径清单没有 raw_inputs、request 位置/参数、original Modeler 位置/速度/加速度/时间、patch 位置/caller_source/raw_segment、status/reason/count/range/token 的变化。对 fixture 主矩阵及 9 组 sweep 的补充计数为：1920 次 candidate/control 调用、16566 个已发布补丁点，位置/来源/段号全部相同，状态/原因/长度不同均为 0；1556 个 raw_fraction 有 double 位差异。这里的调用计数不包括独立 edge cases。uint64 来源精确性由完整差异清单和 C++ CLI 的来源检查支撑，不对来源 ID 做浮点运算。

## 实际构建和汇编证据

原 `stylus_state_modeler.cc` 与新 `terminal_tail_reconstructor.cc` 的 CL.command tlog 均为 `/O1 /Ob1 /MT /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0 /U_DEBUG /EHsc /GR /fp:precise /std:c++20`。Win32 额外 `/Oy` 是原有合同；没有 `/fp:fast`、`/GL`、PCH 或新的 ABI 选项。工具链是实际安装的 v143 14.44.35207，未升级。

对象检查使用实际工具：`VC/Tools/MSVC/14.44.35207/bin/Hostarm64/{x86,arm64}/dumpbin.exe /nologo /disasm:nobytes`，对象来自对应 `stage2-source/.../inkStrokeModelerTestTests/obj/`。两次 dumpbin 均 exit 0。

原 `ink_modeler_utils.obj` 的 `InterpResult`（源为原版 `internal/utils.cc/.h`）在 pressure/tilt 标量路径上有明确不同的舍入位置：

```text
Win32 pressure，相对 InterpResult 地址：
  00000110 subss xmm0,[...]       ; float 的 end-start
  0000012C fmul  dword ptr [...]
  00000130 fadd  dword ptr [...]
  00000134 fstp  dword ptr [...]  ; 乘与加后才存为 float
Win32 tilt：00000196 fmul / 0000019A fadd / 0000019E fstp dword

ARM64 pressure：00000208 fmul s17,s17,s18 / 0000020C fadd s17,s17,s19
ARM64 tilt：00000260 fmul s16,s16,s17 / 00000264 fadd s16,s16,s18
```

Win32 的 scalar 乘加经过 x87，再由 `fstp dword` 舍入到 float；ARM64 的两条 `s` 寄存器指令各执行 FP32 运算。另一方面，Win32 的 `Interp<Vec2>` 是 `subss / mulss / addss`，这与几何字段逐位一致、标量 stylus 有小差异的观察相符。这里只陈述实际指令，未读取运行时 x87 control word，不能擅称每一步一定使用某个特定扩展精度模式。

可复现的具体样例：`straight/stroke_end` 的 original result[18]，raw segment[1,2]，同一 float 投影比例 0.7556930184364319（bits `3f417519`）。只读使用已记录位置、速度和 raw 值重新算此直线投影及两种舍入序列：

| 字段 | FP32 分步乘加预测 = 实际 ARM64 | 延后 float store 预测 = 实际 Win32 |
|---|---|---|
| tilt | 3db3c870 | 3db3c871 |
| orientation | 3e33c870 | 3e33c871 |
| pressure | 3e935883 | 3e935883 |

这直接验证至少一组旧标量字段的架构舍入机制。复杂曲线的每一个 2/13 ULP 样例未逐一重放内部 projection，不能声称全体差异都由这一个乘加位置单独造成；原 `GetStrokeNormal` / float `hypot` 也参与属性投影。

## 新模块 double 数值链与边界

实际 `ink_modeler_terminal_tail_reconstructor.obj` 的 `Distance`：Win32 `call _hypot`，ARM64 `b hypot`。Win32 的 Arc 路径在调用后出现 `fadd qword` / `fstp qword`；Angle 路径在两个 hypot 后出现 `fmul qword` / `fstp qword`，同时其它运算使用 SSE double。ARM64 对应为 double 寄存器路径。link map 证实两架构分别链接各自 `libucrt:hypot.obj`、`libucrt:acos.obj`、`libucrt:sqrt.obj`；Win32 还包括 `acos_impl.obj` / `sqrt_impl.obj`。没有混用另一架构的 CRT。

因此实际二进制确有架构相关 CRT 实现、调用返回和中间舍入路径。弧长/长度影响投影分数及派生 double 诊断；本矩阵最终转换为 float 的补丁位置均相同。这是证据支持的数值传播解释，而不是对全部单个 double 差异的唯一根因证明；没有逐项捕获 hypot / dot / length 的内部寄存器值。

最大角度诊断差异发生在 `reverse/stroke_end` 160 ms 的 `NoChange/no_improvement` 候选：8.210526295874296e-7 → 8.213230240059529e-7 rad，差 2.7039441852330867e-10。反算 cos 为 0.9999999999996629 与 0.9999999999996627，差 2.220446049250313e-16（该区间 2 double ULP）。acos 在接近 1 时导数很大，足以解释“小的余弦变化对应较大的角度 ULP”；这里的 cos 反算用于展示敏感性，不是捕获到的求解器内部余弦。该样例没有发布 patch，所有架构状态和门禁结果一致。

## 验收解释与限制

- 同架构 old stage1 → new stage2 原 API 仍严格逐字段精确对照；本调查没有放宽或替换该门禁。
- 新模块 cross-arch 状态、来源、几何和预算矩阵一致；double raw_fraction / 诊断未达到跨架构逐位一致，公开记录，不隐去。
- 不把 9730 leaves 当成 9730 个几何错误，也不称所有差异只有 1 ULP。bits/value 对一个实际标量的重复记录需区分。
- 未重建历史 backend 来执行新增 96 个 stylus fixtures，未进行运行时 FPU control-word 捕获、逐指令执行、全输入确定性证明、GUI/设备测试。若未来要求跨架构 double 位确定性，需要单独设计并验收数学实现合同；本阶段不为此变更既定 ABI 或工具链。
