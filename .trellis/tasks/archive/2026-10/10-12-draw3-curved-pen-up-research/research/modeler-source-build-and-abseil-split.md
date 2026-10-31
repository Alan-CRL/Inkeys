# Research: Modeler 源码参与编译与 Abseil 静态库分离

- Query: 能否把 Ink Stroke Modeler 的运行 `.cc` 纳入现有 MSBuild 工程，仅保留 Abseil 二进制，并在原算法旁增加专门的 Up 尾段计算？本报告只研究构建、链接、来源与验证计划，不设计修正算法。
- Scope: mixed；主产品、独立 demo/tests、当前三个架构的合并库、原始源码目录与官方构建/ABI 文档。
- Date: 2026-10-12

## Findings

### 1. 结论与证据边界

**有较强的结构证据支持这一迁移，但尚未实际分拆库或验证源码编译。** 三个架构的合并库都包含 165 个 COFF 对象；其中确切的 13 个对象定义 `ink::stroke_model::*`，剩余 152 个对象没有该命名空间的已定义外部符号。这与本地 13 个 Modeler 运行 `.cc` 的文件集合逐一对应。剩余对象属于 Abseil 对象集合，其中还带一个 Abseil 测试辅助对象。

建议分为两个独立验证阶段：先完成“源码 Modeler + 新名称 Abseil-only 库”的正常行为基线，再添加终态模块。不要同时迁移依赖、改 spring/Kalman 参数、修改常规 Move/Predict 或切换 Debug CRT。原 `merge.lib` 保留，供**另一个基线可执行文件**和工程回退使用；迁移后的同一个最终链接不能再使用原合并库补齐 Modeler。

### 2. Files found / 当前接入位置

以下 `O` 表示原始可读目录 `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler`。

| 文件 / 位置 | 一行说明 |
| --- | --- |
| `Inkeys/additional/ink_stroke_modeler/README.md:5` | 记载原始源码仓库；`:6` 是项目来源 revision `8d04529`；`:8` 说明 Modeler/Abseil `.cc` 均未参与产品编译。 |
| `Inkeys/Inkeys.vcxproj:116` | Win32/x64/ARM64 的 `InkStrokeModelerLibraryDirectory` 指向 `inkStrokeModelerTest/lib/lib32、lib64、libArm64`。 |
| `Inkeys/Inkeys.vcxproj:338` | ARM64 Debug 定义 `NDEBUG`；`:339` 取消 `_DEBUG`；`:342` 使用 `/MT`；`:347` 为 C++20。其他架构的 Debug 同样使用 Release CRT ABI。 |
| `Inkeys/Inkeys.vcxproj:354` | 启用 module dependency scan；Modeler 头文件已作为 `ClInclude` 登记（例如 `:853`），没有 Modeler `ClCompile .cc`。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp:17` | 产品通过 `#pragma comment(lib, "ink_stroke_modeler_merge.lib")` 产生隐式默认链接项。 |
| `inkStrokeModelerTest/draw3/ink_prediction.cpp:16` | 独立 demo 也通过同名 pragma 链接原合并库。全仓源码检索仅找到这两处。 |
| `inkStrokeModelerTest/inkStrokeModelerTest.vcxproj:104` | demo 各配置使用 C++20、`/MT`、`NDEBUG`，库目录按三架构区分。 |
| `inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj:66` | 真实模块源码测试工程使用 C++20；`:67` 包含 demo `additional`；`:94`/`:95` 的 Debug 使用 `NDEBUG`、`/MT`。 |
| `inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj:204` | 直接编译 demo `ink_prediction.cpp`；加入新 Modeler 源码时，不能继续让这个调用方包含旧版 Modeler class 头。 |
| `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:70` | Debug 使用 `/MTd`，Release 使用 `/MT`；当前只纳入 Draw3 Bridge/Contact 等部分文件，没有 `Draw3.InkPrediction.cpp`。 |
| `Inkeys/additional/absl/base/options.h:100` | `ABSL_OPTION_USE_STD_STRING_VIEW=2`；`:127` 的 std ordering 为 2；`:151-152` 启用 `lts_20250512` inline namespace。 |
| `Inkeys/additional/absl/base/config.h:120` | LTS 日期 `20250512`、patch level 0；`:73` 使用 `_MSVC_LANG` 判断 MSVC 的 C++ 方言。 |
| `Inkeys/additional/ink_stroke_modeler/numbers.h:20` | 已有本地 MSVC/旧 `__cplusplus` fallback；迁移时保留，不能无意覆盖成上游头。 |
| `Directory.Build.props:3` | `.vcxproj` 默认集成现有 vcpkg；新增子项目也会继承，必须考虑已有依赖安装配置。 |
| `.github/workflows/build-windows.yml:461` | 现有发布流程使用 MSBuild 对 Release 三架构构建 native；仅本机 ARM64 成功不能外推三架构。 |

### 3. 必须迁移的确切运行源码集合

原始目录中共有 14 个 `.cc`；**以下 13 个是运行实现**。唯一排除项 `internal/type_matchers.cc:17-18` 包含 GMock/GTest，属于测试辅助，不能按 `**/*.cc` 全部加入产品。

| `O` 下的运行源码 | 原合并库的确切 archive member 名 |
| --- | --- |
| `stroke_modeler.cc` | `stroke_modeler.dir\MinSizeRel\stroke_modeler.obj` |
| `params.cc` | `params.dir\MinSizeRel\params.obj` |
| `types.cc` | `types.dir\MinSizeRel\types.obj` |
| `internal/internal_types.cc` | `internal_types.dir\MinSizeRel\internal_types.obj` |
| `internal/loop_contraction_mitigation_modeler.cc` | `loop_contraction_mitigation_modeler.dir\MinSizeRel\loop_contraction_mitigation_modeler.obj` |
| `internal/position_modeler.cc` | `position_modeler.dir\MinSizeRel\position_modeler.obj` |
| `internal/stylus_state_modeler.cc` | `stylus_state_modeler.dir\MinSizeRel\stylus_state_modeler.obj` |
| `internal/utils.cc` | `utils.dir\MinSizeRel\utils.obj` |
| `internal/wobble_smoother.cc` | `wobble_smoother.dir\MinSizeRel\wobble_smoother.obj` |
| `internal/prediction/kalman_predictor.cc` | `kalman_predictor.dir\MinSizeRel\kalman_predictor.obj` |
| `internal/prediction/stroke_end_predictor.cc` | `stroke_end_predictor.dir\MinSizeRel\stroke_end_predictor.obj` |
| `internal/prediction/kalman_filter/axis_predictor.cc` | `kalman_filter.dir\MinSizeRel\axis_predictor.obj` |
| `internal/prediction/kalman_filter/kalman_filter.cc` | `kalman_filter.dir\MinSizeRel\kalman_filter.obj` |

与 ABI 有关的代码模式：`O/stroke_modeler.cc:25-26` 使用 `absl::Status/StatusOr`；`O/params.cc:19-21` 使用 Status、string_view、substitute；`O/types.cc:7-10` 使用 Status/StatusOr 与格式化；`O/internal/internal_types.cc:5` 使用 str_format；`O/internal/position_modeler.cc:7-9` 也包含 Status/StatusOr/substitute。因此 Abseil 不是“只需头文件”，不能仅去掉原合并库而不提供 Abseil 实现。

### 4. 三架构 archive 库存、hash 与核查方式

本轮通过 `vswhere -latest -products * -property installationPath` 定位当前安装，然后从 `VC/Tools/MSVC` 中选择已安装工具目录的 `bin/Hostarm64/arm64/lib.exe`、`dumpbin.exe`。没有硬编码 VS 版本。调用的工具目录版本为 `14.51.36231`；没有启动 GUI、执行编译、提取对象或写新库。

只读调用：

```text
lib.exe /nologo /list <merge.lib>
dumpbin.exe /nologo /headers <merge.lib>
dumpbin.exe /nologo /directives <merge.lib>
dumpbin.exe /nologo /symbols /archivemembers <merge.lib>
```

每个工具调用检查退出码，三个架构均为 0。`/symbols /archivemembers` 输出按 `Archive member name` 分组，统计非 UNDEF 的 `SECT... External` 定义；用 `@stroke_model@ink@@` 识别 Modeler、用 `lts_20250512@absl` 等识别 Abseil。没有把符号引用误计为定义。`/list` 的顺序与 `/archivemembers` 展示顺序不同，所以使用**成员名**匹配，不能按输出顺序删对象。

| 当前仓库库路径 | 字节数 | COFF machine / 成员数 | 定义 Modeler 的对象 / 剩余对象 |
| --- | ---: | --- | --- |
| `inkStrokeModelerTest/lib/libArm64/ink_stroke_modeler_merge.lib` | 17,016,718 | AA64 / 165 | 13 / 152 |
| `inkStrokeModelerTest/lib/lib64/ink_stroke_modeler_merge.lib` | 18,379,338 | 8664 / 165 | 13 / 152 |
| `inkStrokeModelerTest/lib/lib32/ink_stroke_modeler_merge.lib` | 13,243,876 | 14C / 165 | 13 / 152 |

SHA-256（`Get-FileHash`；原始仓库相同架构库与当前仓库逐一完全相同）：

```text
ARM64 30C04541061EBBA69F8A347FEEB92B812DE72F964481B77A9087289C5C6F6F0D
x64   7C12BC222729589E3040E3B5EA21605AE2D49C5EA15A28F0D969DE3DDCB84819
Win32 58679FE5C95A3108A6BA24539D15FCE9208F4C09C3A27FD4781F02C59F799D3A
```

三个库的对象路径均标为 `MinSizeRel`。165 个对象都有 `/DEFAULTLIB:LIBCMT`；152 个对象带 `/FAILIFMISMATCH:RuntimeLibrary=MT_StaticRelease` 和 `_ITERATOR_DEBUG_LEVEL=0`，152 个带 `_MSC_VER=1900`。后者是库存中的 ABI 检测字符串，**不能据此认定原库用了 VS2015 或还原完整编译器版本**。ARM64 单独定位 `stroke_modeler.obj`、`position_modeler.obj`、`params.obj`，均确认了这些 Release CRT/iterator directives。

全部三个架构符号中只有 `lts_20250512` 这一 LTS namespace。ARM64 的 `StrokeModeler::Update/Predict/ProcessUpEvent` 使用这个 namespace 的 Status，部分 Abseil函数签名使用 `std::basic_string_view`，与当前 C++20 / options 2 具有一致线索。当前与原始目录 `absl/base/options.h`、`config.h` 字节 hash 相同；这**仍不能证明全部 Abseil 头、宏、方言与原二进制完全一致**。

剩余 152 个对象中，151 个 basename 能匹配原始 Abseil `.cc`；唯一未匹配项是 `random_internal_distribution_test_util.dir\MinSizeRel\distribution_test_util.obj`，官方 Abseil 20250512.0 中确实有同名测试辅助源码。剩余对象不存在 Modeler namespace 定义，并不要求每个对象都带 Abseil C++ namespace，例如平台实现/C 符号可不带 namespace。这个测试辅助对象不属于 Modeler，应保留在第一版 Abseil-only 衍生库里；本任务不顺手裁剪 Abseil 的依赖闭包或删除测试辅助对象。

### 5. 如何分离，及尚缺的 provenance

Microsoft 的 [LIB 管理文档](https://learn.microsoft.com/en-us/cpp/build/reference/managing-a-library?view=msvc-170) 明确支持用 `/REMOVE:<object>` 从输出库排除特定对象，并用 `/OUT:<new-name>` 保留输入库。由此建议在实施阶段，从固定 hash 的每个 `merge.lib` 生成**新名称**的 `abseil_only.lib`，明确列出上述 13 个 `/REMOVE` 成员。这个方法不重编 Abseil、不会引入新的依赖版本，理论上保留原 Abseil 对象内容。

这是一项待实施的构建动作，本轮没有执行。要验收输出库：成员恰为 152、machine 不变、无 Modeler namespace 的已定义外部符号、LTS namespace 不变；并按成员内容 hash 或等效方式证明其余 COFF 载荷与输入相同。脚本必须检查输入 SHA-256 和全部期望成员，不允许靠泛化 basename/glob 在未来不同的库上删错对象。原库不覆盖。

未找到原始仓库的库生成/合并脚本，未找到独立 `absl*.lib`/Abseil-only 库。搜索范围为原始仓库根和 `inkStrokeModelerTest` 文件（排除 Git、生成缓存和 vcpkg 供应商树）；对原始和当前 `VcpkgInstalled` 也按库名检索，没有结果。原始 Abseil 目录有 206 个 `.cc`，产品 Abseil 目录有 0 个 `.cc`，且原始目录未找到完整 CMake/BUILD 构建描述。这说明源码可读，但不能据目录还原原合并库的完整生产命令。

Google 上游固定 SHA 的 [CMake 目标](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/CMakeLists.txt) 与 [internal 目标](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/internal/CMakeLists.txt) 可用于核对源码依赖，但不是本机原库生产脚本。本项目仍复用已有 MSBuild，不为这次迁移引入一套 CMake 构建系统。

### 6. 工程归属和链接风险

**优先采用现有工程直接登记普通 `.cc`，需要被多个目标使用时再提取共享 MSBuild items 清单。** 选择单一物理源码和头文件来源，例如产品已有 `Inkeys/additional/ink_stroke_modeler`，保留 upstream 目录结构、版权头、原编码及换行；头文件中的既有本地兼容差异单独记录。不要复制算法修改到两个 Modeler 目录里长期维护。

这些文件是传统 C++ TU，不应为了当前 Draw3 module 风格把它们转换为 `.cppm`。登记 `ClCompile`，普通 `CompileAsCpp`，核对 PCH（当前读取的项目级定义没有启用 PCH，新增文件可明确 NotUsing），保留 C++20、已有 CRT/宏和 includes。对应 `.filters` 同步登记。现有 C++ module 调用方继续在 global module fragment 包含头，库中定义参与普通链接即可。

如果选择新静态 `.vcxproj`，优点是隔离第三方 flags 和只编译一次，缺点是需要新增 solution mapping/project dependencies、继承 vcpkg 设置、管理六种配置的输出/链接路径，以及确认 static library 的 Abseil 依赖是否由最终 EXE 正确解析。对 13 个文件的第一次验证，新增项目没有明显必要；共享 items 同样可以统一文件集合而不改变工程依赖关系。长期需要多个稳定消费者时再单独评估静态项目。

必须避免以下问题：

- **重复或隐式混链：** `.cc` 全部加进来但旧 pragma 仍在，链接器可能只不拉取重名旧对象，所以没有 LNK2005 也不能证明没有旧实现；漏登记一个 helper 时旧库可静默补齐；`/WHOLEARCHIVE` 等情况下则可能直接重复定义。不要用 `/FORCE:MULTIPLE`、忽略错配或链接顺序去掩盖。
- **旧头/新实现的 ODR 和类布局错配：** 产品和 demo 目前各有 Modeler/Abseil 头副本。若修改 `StrokeModeler` 成员或参数，所有同一可执行文件中的调用方必须包含同一新头。已有 tests 使用 demo `additional`，仅编译产品 `.cc` 而不统一头来源可能造成分配大小与实现不一致。不能只给新测试文件换 include 路径而让同 EXE 的旧 module继续包含旧类定义。
- **MT/MTd 与 iterator：** Modeler `.cc` 参与编译不代表 Abseil 已换成 Debug ABI。产品现有 Debug 保留 `/MT`、`NDEBUG`、取消 `_DEBUG`；无需把整工程改成 `/MTd`。`InkeysHeadlessTests` 当前 Debug 为 `/MTd`，不能直接添加这个 Abseil-only Release 库而期待安全混用；应在已有 `/MT` 的测试目标或有相同 ABI 的专门无窗口目标验证，不能无关地修改整测试工程 CRT。
- **Abseil 版本/配置：** 保留现有 LTS 与 options；不从其他 vcpkg 版本再混入另一套 Abseil。精确方言、exceptions/RTTI、DLL/static 宏、`NDEBUG`、hardened、iterator 和 sanitizer 配置需要确认。archive namespace 与 string_view 线索不是完整 ABI 证明。[Abseil 20250512.0 FAQ](https://github.com/abseil/abseil-cpp/blob/20250512.0/FAQ.md) 对预编译库的方言、宏和配置一致性作了专门解释；[官方兼容说明](https://abseil.io/about/compatibility) 不承诺跨版本的 ABI 稳定。
- **架构与 object 输出名：** 每个平台只能消费对应 machine 的新库。显式登记 nested `.cc` 时核对 obj 输出名和现有 `.cpp` 是否碰撞；需要时仅为新供应商文件设置专属 `ObjectFileName`。不跨平台复用缓存。Win32、x64、ARM64 Debug/Release 的 flags 与路径需分别审查。
- **模块缓存：** 修改头和 class 布局后，包含旧声明的 BMI/obj必须重建；旧二进制、旧 BMI、旧目录优先级都可能使“构建成功”失去意义。只清理此次必要的目标缓存，不做全仓清理重构。

### 7. 最小验证/回退顺序

1. **记录迁移清单：** 固定来源、13 个源码/对应头 hash、本地兼容差异、三个合并库 hash、当前 flags。原始库和 baseline输入/输出留存。
2. **分离依赖：** 生成新名称 Abseil-only 库，检查 152 个对象/machine/载荷/无 Modeler定义。此时不修改算法、不裁剪 Abseil。
3. **原算法源码接入：** 直接或共享清单登记13个 `.cc`；迁移目标去掉旧merge pragma，显式链接新 Abseil-only。统一同一目标的头来源，按需要重建 affected module。
4. **证明实现归属：** 用链接日志和 [Microsoft `/VERBOSE:LIB`](https://learn.microsoft.com/en-us/cpp/build/reference/verbose-print-progress-messages?view=msvc-170) 检查实际搜索库/拉入对象路径；map或符号证据表明 Modeler来自新编译 obj而不是merge库；新增终态符号也来自新源码。仅看链接退出码不足够。
5. **正常行为 parity：** 用两个分开的无窗口执行目标回放同一输入，一个只用原merge，另一个只用新源码+Abseil-only；比较 Down/Move、Predict（不提交）、Up、Reset/错误/Save-Restore等正常路径。预编译对象是 MinSizeRel，而产品Debug可能不优化，浮点输出应设合理数值容差，不能预先承诺bitwise相同。
6. **新增终态模块：** 正常路径验证通过后才加入 Up-only的源码计算与调用协议，避免把迁移差异当作算法收益；随后按主任务定义验证完整生命周期与 L0/L1/Stored合同。
7. **构建矩阵：** 本机按用户当前规则用 ARM64原生MSBuild构建完整 `InkeysRepo.sln Debug|ARM64`，至少5分钟预算；同一PowerShell invocation先规范化PATH、设置 `MSBUILDDISABLENODEREUSE=1`。工程迁移还需覆盖Release/其他架构的编译链接证据；当前设备未运行的平台不能称运行通过。只执行授权的无窗口验证。
8. **回退：** 用工程选择回到未改的原merge并移除新源码链接，保持同一目标只有一套实现；baseline和迁移版可并存为两个EXE。算法回退开关与依赖迁移回退分别定义，避免一个布尔值把新头旧binary拼在一起。

### 8. 哪些有把握，哪些尚无把握

| 判断 | 当前把握 / 难点 |
| --- | --- |
| 库能按对象边界拆成13个Modeler与152个Abseil侧对象 | 较高；三架构成员名/符号都核查，官方工具支持另存排除，但实际生成输出尚未执行。 |
| 13个普通 `.cc` 可在现有MSBuild体系登记 | 较高；文件和Abseil依赖都可读，工程已C++20。真正编译还可能暴露MSVC或宏差异，不能声称已通过。 |
| 保留当前Abseil二进制后完全ABI一致 | 尚不能保证；LTS/options/CRT/iterator线索一致，缺少原生产命令、完整flags/headers provenance。需要先完成分离、编译和parity。 |
| 所有正常Move/Predict行为在新源码中保持 | 需数值回放；源码版本与预编译实现没有可复现生产闭环，且优化/浮点差异可能影响结果。 |
| 三架构Debug/Release都构建成功 | 本轮没有验证；ARM64能定位native工具，不能外推Win32/x64或所有发布配置。 |
| 源码迁移后即可让库回写曾经发出的点 | 不成立；编译归属改变不会自动改变 API、外部尾段所有权或稳定前缀边界。该难点由主算法/集成设计处理。 |

## External References

- Microsoft LIB documentation：[/REMOVE、/OUT、/LIST](https://learn.microsoft.com/en-us/cpp/build/reference/managing-a-library?view=msvc-170)。只支持工具能力和计划；不是声称本次已分拆。
- Microsoft LINK documentation：[/VERBOSE:LIB](https://learn.microsoft.com/en-us/cpp/build/reference/verbose-print-progress-messages?view=msvc-170)。用于后续实际链接归属核查。
- Abseil固定LTS：[20250512.0 FAQ](https://github.com/abseil/abseil-cpp/blob/20250512.0/FAQ.md)、[官方兼容说明](https://abseil.io/about/compatibility)、[distribution_test_util.cc](https://github.com/abseil/abseil-cpp/blob/20250512.0/absl/random/internal/distribution_test_util.cc)。保持当前依赖，不升级。
- Google Ink Stroke Modeler固定SHA `f2388813b0b25bc3e33d143d369a8367ab2e30c8`：[顶层Modeler CMake目标](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/CMakeLists.txt)、[内部目标](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/internal/CMakeLists.txt)、[BUILD.bazel](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/BUILD.bazel)。用于来源和依赖核对，不新增构建系统。

## Related Specs

- `.trellis/workflow.md`：当前planning/research；不可在此研究角色修改工程或库。
- `.trellis/spec/native/modules-and-code-style.md`：第三方补丁与自研改动分离、维护来源和本地差异；保持源文件编码换行。
- `.trellis/spec/native/quality-and-validation.md`：合并库的Release ABI、`/MT`/`NDEBUG`、主产品与demo验证边界；纯研究可不编译。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：MSBuild既有Solution/平台入口、第三方边界、三架构声明与验证区别。
- `.trellis/spec/native-desktop/cpp-conventions.md`：module/传统头文件边界、工程登记、最小差异。
- 用户提供的当前AGENTS规则优先：完整 `InkeysRepo.sln Debug|ARM64`、native MSBuild、至少5分钟、PATH规范化、禁止默认GUI/commit/push。若旧Spec描述的PptCOM依赖与当前用户规则不同，构建仍遵从用户入口，不在研究中擅自修改工程。

## Caveats / Not Found

- 本轮仅写本报告；未生成Abseil-only库、未提取/覆盖merge.lib、未修改产品/测试/工程配置、未编译或执行GUI、未运行任何Git命令。
- 原始仓库普通文件可读，不把当前工作树等同于历史revision `8d04529`。本轮三架构库hash一致补强了二进制来源线索，仍没有证明其Modeler内容恰由当前13个源码生产。
- 没有找到独立Abseil库与合并生产脚本；搜索边界已在正文说明，不宣称机器其他位置或私有历史中不存在。
- archive inventory证明结构性分离条件，不证明新源码的数值效果、终态质量、性能或设备兼容；这些必须随后分别验收。
- 未加载 `implement.jsonl`/`check.jsonl`。研究角色只使用本任务路径、相关Spec、产品与源码资料。
