# Research: Canary / Inkeys2 来源、基准环境与 Win7 图形矩阵

- Query: 定位用户所指上一个 Canary（HC）和 Inkeys2 对照（H2）的可追溯源码/二进制；核对远端 refs、既有性能证据、当前测量环境与 Win7 SP1 + KB2670838 呈现边界。
- Scope: mixed（工作树源码/任务记录、本机只读环境、GitHub 只读 API、微软官方文档）
- Date: 2026-09-27；GitHub refs/API 观察约 09:51 UTC（17:51 +08:00）。

## 2026-09-29 候选二进制补充（覆盖下文“未下载”旧状态）

- 经 `gh release download 20260713a -R Alan-CRL/Inkeys --pattern Inkeys20260713a-arm64.zip` 只读下载到忽略的 `TestResults/release-hardening/baseline-artifacts/`，ZIP 18,757,925 B、SHA-256 `584b08c6b8af12e8a2c0966c9d39a42cc30d62ea0265629434b525c079ec426e`，与上述 Release 记录相同。校验无绝对/穿越/反斜杠/符号链接 entry 后解出的 `InkeysArm64/Inkeys.exe` 27,643,680 B、SHA-256 `2300b276aac3402e87b5c6a11ca39a81f2b06f7643f14f8ab85acd830f2635a5`；PE machine `0xAA64`、GUI subsystem 2。尚未运行该 H2 候选。
- 经 `gh run download 31487748238 -R Alan-CRL/Inkeys -n Inkeys3.0.0-20260811aArm64` 只读取得 HC 候选 artifact 并由 CLI 解包到 `baseline-artifacts/hc-31487748238-arm64/`；其中 `signedUpload/InkeysArm64/Inkeys.exe` 41,361,696 B、SHA-256 `81a3dbb26a845308ea2aafe7869844b389968e31f3798aee2e0987f947a6d07e`，PE machine `0xAA64`、GUI subsystem 2。此轮未单独取原 artifact ZIP，因此上文 archive digest 尚未本机复算；run ID/内层 EXE 散列已冻结。尚未运行该 HC 候选，亦未证明它是用户所安装 Canary。
- 初次 `gh release download` 在默认网络沙箱因代理端口拒绝退出 1，获准在仓库 TestResults 目录的只读 GitHub 访问后成功；不将这一环境错误归入产品缺陷。后续同机 GUI/性能必须保持各版本独立安装目录、相同显示/供电/效果/输入轨迹与至少三轮，历史二进制存在不等于可比基准已完成。

## Findings

### Files found

| 路径 | 用途 |
| --- | --- |
| `AGENTS.md`, `.trellis/workflow.md`, `.trellis/config.yaml` | 仓库/Trellis 规则；本研究不运行构建、GUI 或 Git 操作。 |
| `.trellis/spec/index.md`, `native-desktop/index.md`, `native-desktop/build-and-compatibility.md` | 当前主 Solution、平台证据等级和独立 demo 边界。 |
| `.trellis/spec/native-desktop/ui3-render-diagnostics.md` | UI3 生产诊断计数、采样与 headless 边界。 |
| `.trellis/spec/native-desktop/draw3-integration.md` | Draw3 独立 device/presenter/Host 约束；现仍写 DComp→DWM2→DWM→ULW。 |
| `.trellis/spec/native/platform-and-resources.md` | 原独立 Draw3 demo 的 Win7/设备合同；不能机械覆盖已集成主产品。 |
| `.trellis/tasks/archive/2026-08/08-09-ui3-animation-performance-audit/{implement.md,research/performance-runtime-baseline.md}` | UI3 旧静态热点、headless 微基准及未闭环的真实呈现验收。 |
| `.trellis/tasks/09-22-ui3-animation-stutter-investigation/{findings.md,research/validation.md,verification/first-batch-result.md}` | 最近卡顿调查、无 HWND 的 DC 探针、首批修复与诊断验收。 |
| `.trellis/tasks/archive/2026-08/08-15-migrate-draw3-into-inkeys/implement.md` | Draw3 集成时 ARM64 构建/隐藏 HWND 合成测试记录，无发布版性能基线。 |
| `.trellis/tasks/archive/2026-08/draw3-source/active/08-02-staged-code-acceptance/research/stage1-performance-hotspots.md` | 独立 demo 的性能候选，只能作追踪线索。 |
| `.github/workflows/build-windows.yml`, `Inkeys/IdtMain.cpp`, `InkeysRepo.sln` | 当前版本文字、CI 打包约定、主 Solution 目标。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp`, `Draw3.TransparentPresentation.cpp`, `Draw3.Win7Compat.cpp` | 当前生产 Draw3 的设备优先级、透明 presenter 和 Win7 API 兼容代码。 |

### HC：上一个 Canary 候选

GitHub Actions 的 `canary` 分支查询返回 29 个运行记录；截至本次 API 读取，最新的是 [Inkeys 3 Build run 31487748238](https://github.com/Alan-CRL/Inkeys/actions/runs/31487748238)，2026-08-11 11:41:30 UTC 触发，13:57:43 UTC 完成，结论 success，`head_sha=82f7b7c02080c253661514d31b55f3a827382e1d`。公开 `canary` 分支头也是该 SHA；[该提交](https://github.com/Alan-CRL/Inkeys/commit/82f7b7c02080c253661514d31b55f3a827382e1d) 合并 PR #207，提交消息为 `Version -> 20260811a`。run 的 Check、Win32/x64/ARM64 Build、Package、Upload 六个 job 均 success；这证明 CI 任务完成，**不证明用户安装的二进制一定来自此 run**。

run 的 Actions artifacts 目前未过期，API 给出的到期时间为 2026-11-09 11:41:31 UTC。`Package`（ID `9100220874`）大小 213,052,813 B，archive SHA-256 `1f0a004709005746aea03555fecdb619f8b158e7f2976a8e1a84cafd3bf2a453`；单架构 artifacts：

| 名称 / ID | archive 大小与 SHA-256 |
| --- | --- |
| `Inkeys3.0.0-20260811aArm64` / `9100206152` | 42,569,050 B；`b0f47b32886c1ad8b20b9aad8160c2fccb1408dc199afbdd6a081d831da432c2` |
| `Inkeys3.0.0-20260811a64` / `9100204432` | 44,241,608 B；`b309c54b336d036dd7264ba13b1e65296249934c7472ed937e59c87edc3919d2` |
| `Inkeys3.0.0-20260811a` / `9100202497` | 42,738,908 B；`fdfaae23319b1ad434acabcbc3886bbf033b9ed9e327a1211bb8614b32c43f25` |

上述 digest 是 GitHub **artifact archive** 的散列，不是内层 EXE 或用户安装包的散列。当前工作树 `Inkeys/IdtMain.cpp:71-72` 仍写 `editionVersion=3.0.0-dev.3981`、`editionDate=3.0.0-20260811a`，说明版本字符串没有随所有后续源码修改更新，不能据此把 H0 二进制认作 HC。公开 Release API 最新版本是 `20260713a`，没有 2026-08-11 的 Canary Release；工作流 `.github/workflows/build-windows.yml:331-340,676-688,703-708` 把 canary 分支映射 Canary 并上传 Package。Upload job 成功也不能证明外部镜像当前内容未被替换。**HC 建议固定为该 run + SHA + ARM64 artifact，并向用户安装过的 Canary EXE/包比对版本和内层文件散列；若另有私有或本地构建，则调整 HC，不能伪称唯一定位。**

### H2：Inkeys2 对照候选

[最新公开正式 Release `20260713a`](https://github.com/Alan-CRL/Inkeys/releases/tag/20260713a) 的发布说明明确写 `Inkeys20260713a(LTS) - Inkeys2`；tag 指向 `0d9751b96f063d9aa5f46f8d905a8e153e9c91dd`。关联的 [CI run 29267288526](https://github.com/Alan-CRL/Inkeys/actions/runs/29267288526) 在 2026-07-13 16:39:17 UTC 由 `main` push 触发，`head_sha` 与 tag 相同、结论 success；Release 于 2026-07-17 12:46:34 UTC 发布。应以用户实际拿到的**正式 Release 资产**作体验对照，而非默认用 CI 中间 artifact：

| Release 资产 | ZIP 大小 / GitHub SHA-256 |
| --- | --- |
| `Inkeys20260713a-arm64.zip` | 18,757,925 B；`584b08c6b8af12e8a2c0966c9d39a42cc30d62ea0265629434b525c079ec426e` |
| `Inkeys20260713a-x64.zip` | 19,362,836 B；`18c4e53e25deb77466196691ccd8eb98ce048f5c33fe3f8006ed181b404c4f66` |
| `Inkeys20260713a-x86.zip` | 18,815,729 B；`8573f07b6718948abd8c8455beada807af1b038ab3bf21301cb6ddb1ea51c90e` |

Release 说明要求普通用户选非 `UpdatePackage` 资产。若用户期望对比的是更早某个 Inkeys2 安装版，H2 需按其原包再锁定；`inkeys2-final` 当前分支头 `b166ed51fbcb1f477b00f5fbebab2cb27ccac277` 不等于最新 Inkeys2 正式 Release tag，不能由分支名称替代 H2。

### 远端 refs 与审计边界

通过 GitHub REST 的 `branches?per_page=100`、`tags?per_page=100`、`releases?per_page=100` 分页读取到 9 个分支、34 个 tags、31 个公开 releases（本次每类结果均少于一页容量）；不含不可见/已删除 refs、外部二进制和未公开构建。分支头快照：`main=0d9751b9`、`dev=8b156fca`、`canary=82f7b7c0`、`inkeys2-final=b166ed51`、`inkeys1-final=505ae82a`、`bugfix/eraser=1e48386f`、`feature/recognition=2e664a23`、`fluent=3df5647c`、`theme=85132bb1`。任务提示词给的 2026-09-27 `dev=5780f616` 是定位线索；读取时远端 `dev` 已指向 `8b156fca...`，主代理应按 **H0 本地冻结**审计终点，另记远端移动，不追逐移动 head。研究代理未运行任何 `git` 命令或修改 refs。

### 当前机器、构建入口与基准可比性

只读本机查询得到：注册表 `DisplayVersion=25H2`、`CurrentBuild=26200`、`UBR=9457`、`BuildLabEx` 含 `arm64fre`；注册表 `ProductName=Windows 10 Home` 是旧式标签，与用户提供的 Windows 11 ARM64 和 25H2/build 信息并列记录，不由该标签反推 OS。CPU 是 Snapdragon X1E80100 12-core；GPU PnP 有 Qualcomm Adreno X1-85（`oem215.inf`，驱动 `31.0.137.0`，2026-01-08）及 OrayIddDriver 虚拟显示设备（`oem230.inf`，驱动 `17.50.13.330`，2025-06-11）。`System.Windows.Forms.Screen` 当前只见一个 1440×960、32 bpp 显示；只读 GDI caps 得 96×96 DPI、60 Hz；`powercfg /getactivescheme` 为 Balanced；`GetSystemPowerStatus` 为 AC 接通、100% 电量。**当前可能走 Oray 虚拟显示会话，不能把它当作实体教室屏/GPU 输出体验。** WMI 的 OS/GPU/HotFix 查询在沙箱内 Access denied，未取得逐项补丁清单；上述信息来自注册表、PNP 和只读 Win32 API。实际 renderer/presenter、主题、效果、GPU feature level、应用进程构建均未启动测量，仍为未验证。

`InkeysRepo.sln:6-17,27-32` 包含 Inkeys、PptCOM、PptCOM.Tests、InkeysHeadlessTests，以及 Debug/Release × Win32/x64/ARM64。`InkeysRepo.sln:60-79` 将 PptCOM/PptCOM.Tests 映射 Release|Any CPU；`Inkeys/Inkeys.vcxproj:34,40,117` 使用 SDK 10.0.26100.0、v143 和 `Build/<platform>/<config>`；`PptCOM/PptCOM.csproj:11` 目标 .NET Framework 4.0。`vswhere` 只读定位本机 VS Community 安装含 ARM64 工具，实际 build 前仍须重新定位原生 ARM64 MSBuild，遵守同一 PowerShell invocation 的 PATH 去重及至少 5 分钟时限。独立 `inkStrokeModelerTest.sln` 不能替代生产 `InkeysRepo.sln`。

### 既有性能数字的可用范围

- 08-09 UI3 任务 `implement.md:341-345,394-404` 明确没有改动前可比的动画微基准。后来生产动画模块 headless 三轮给出曲线 `88.20–95.13 ns`、57 inactive 值 `5.017–5.071 µs`、57 值中一个 active `6.022–6.351 µs`，但短操作噪声高达约 101.88%，不能声称 UI3 整帧提升。该任务仍列真实 GUI、光影、ULW 和 Inkeys2 同机体验缺口，见其 `research/performance-runtime-baseline.md:75-86`。
- 09-22 卡顿任务 `findings.md:89-93` 的无 HWND WARP+D2D GetDC/ReleaseDC 对照：1600×1200 到 4800×3600、20 帧预热/80 帧采样、ABBA 顺序，合计 P50 约 `0.09–0.17 ms`，没有 ULW/真实光影，不可解释用户现场百毫秒卡顿。09-23 的 `verification/first-batch-result.md:18-27,59-61` 证明代码构建和 headless 测试成功，但原始偶发卡顿仍未现场闭环。
- 08-15 Draw3 集成 `implement.md:52-55` 记录 ARM64 Debug/Release Solution 与隐藏 HWND DComp/DWM2/DWM/ULW 测试 exit 0；不是当前 H0 的生产全链路延迟、CPU/GPU 或 Win7 实测。独立 demo 的 `stage1-performance-hotspots.md` 仅能提供复制、上传、粒子候选，不是已集成版本的成本分布。
- 当前生产 `[UI3Diag]` 依 `.trellis/spec/native-desktop/ui3-render-diagnostics.md:25-64` 区分 callback、animation advance、GetDC/ULW attempt、成功 commit、idle/Retry，健康周期输出默认关闭且异常最多每秒汇总；现有摘要不直接给三轮原始帧序列、median/P95/P99 或光学延迟。不得把回调数当有效 FPS。

### 可执行的可比基准方案（尚未执行）

1. 固定 H0/HF 工作树指纹和各自 ARM64 Release 产物；对 HC 取 run 31487748238 ARM64 artifact，对 H2 取正式 Release ARM64 ZIP，校验 archive digest、解包后 EXE/dll/tlb 版本和 SHA-256。保留来源 URL、运行配置与构建时间。H2 功能差异按相同用户意图对照，不用不同动作/效果开关混成总分。
2. 每次运行记录 OS/build、实体或虚拟显示驱动、分辨率/DPI/刷新率、AC/电源计划、GPU 驱动、实际 hardware/WARP、presenter、主题/动画/光影/SVG 开关及输入设备。Canary/H2/H0/HF 应在同一设备、显示会话、Release 架构和等价视觉配置中比较；当前 Oray 会话结果只能代表该会话。
3. 每场景固定输入轨迹和起止标记，区分冷启动、首次交互、预热稳态、长时运行；至少三轮，先 warm-up，再保留每次样本量和原始帧/成功 Present 时间戳。分别统计 UI3 Bar 展开/反向/拖动/光影/SVG 与 Draw3 落笔/续笔/抬笔/橡皮/长文档。报告 median、P95、样本足够时 P99、长帧频率、实际完成时间、CPU/线程、GPU 可得值、内存/句柄/cache；记录输入采样率、模型消费率、成功呈现率，Down→成功 Present 不冒充光学延迟。
4. 先以现有 headless 真实模块测试、`[UI3Diag]` 和离屏 D2D 探针校验计时口径；真实 ULW/设备/笔输入仍须单独 GUI/设备授权与现场记录。未取得 H2/HC 包或同机/同效果条件时，只报告 H0→HF 或模块级结果，不写“已优于上一个 Canary/Inkeys2”。

### Win7 SP1 + 仅 KB2670838：代码、合同与运行缺口

用户新增边界是 **Windows 7 SP1 + KB2670838，仅此平台更新；保留 FLIP_SEQUENTIAL；两个 Win7 DWM 透明方案禁用**。用户报告目标环境已实测 FLIP_SEQUENTIAL 可用，但本研究未收到原始设备/驱动/日志；将其记为用户提供的现场事实，不能冒充本轮 agent 实测。微软 [KB2670838 说明](https://support.microsoft.com/en-us/topic/platform-update-for-windows-7-sp1-and-windows-server-2008-r2-sp1-d97da9ca-c15c-b21f-ebb0-838f7be8d9f6) 要求 SP1；[平台更新开发文档](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7) 说明 Direct2D 1.1 完整、D3D11.1/DXGI1.2 部分、DComp 不可用；Win7 hardware feature level 不超过 11_0、该环境 WARP 为 11_0；`CreateSwapChainForComposition` 不可用，`CreateSwapChainForHwnd` 不支持 `DXGI_SCALING_NONE`。当前代码用 `DXGI_SCALING_STRETCH`，未见官方文档禁止本目标的 FLIP_SEQUENTIAL。

当前生产代码 `Draw3.GraphicsInitialization.cpp:53-75,82-99` 先以 `[11_1,11_0]` 尝试 HARDWARE，`E_INVALIDARG` 改 `[11_0]` 重试；硬件失败后同法尝试 WARP，两者失败则初始化失败。`Draw3.TransparentPresentation.cpp:89-95,777-780,856-866` 自动顺序仍是 **DComp→DwmBlurBehind2→DwmBlurBehind→ULW**；`Draw3.TransparentPresentation.cpp:113-116` 从系统目录探测 DComp，Win7 API 不可用应跳过 DComp，但两种 DWM 路径**目前没有按用户新约束禁用**。`Draw3.TransparentPresentation.cpp:544-568,620-633` 对非 DComp 以 `CreateSwapChainForHwnd`、`DXGI_SCALING_STRETCH`、`DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` 建 swap chain；ULW 在 `:273-329` 从 GPU backbuffer dirty copy 到 staging/DIB，再 `UpdateLayeredWindowIndirect`。`Draw3.Win7Compat.cpp:9-32` 动态探测新时钟 API，否则退回 Win7 时钟。旧 `.trellis/spec/native-desktop/draw3-integration.md:74,379` 明确保留 DWM2/DWM fallback，与新约束相冲突，实施者需在更新代码/测试后同步更正规范，不能静默沿用旧合同。

| Win7 目标设备 | 当前静态设备结果 | 按新约束应测 presenter | 必须实际验证 |
| --- | --- | --- | --- |
| 硬件支持 FL11_0 | HARDWARE 成功；11_1 列表若不被 runtime 接受则用 11_0 重试 | DComp 不可用；DWM2/DWM 禁用后主窗 ULW，selection 辅助仍 ULW | 实际 driver type/FL 日志、FLIP+STRETCH swap chain、透明/命中、首次成功 Present、连续墨迹、resize/device loss/退出；x86/x64 分开。 |
| 硬件不支持 FL11_0 或硬件创建失败 | HARDWARE 的 11_0 请求失败，继而尝试 WARP FL11_0 | 同上 ULW，不得因 WARP 自动降低输入/视觉要求 | 确认 WARP 真正被选、资源/延迟/CPU 与内存成本、绘制和 fallback 成功；若 WARP 失败则明确启动失败。 |

以上是基于源码和微软文档的预期测试矩阵，**不是 Win7 运行 PASS**。当前机器只有 Windows 11 ARM64；Win7 x86/x64 及硬件 FL11_0 有/无的四格、用户 FLIP 现场元组、真实 DComp/ULW 行为均未在本研究执行。UI3 另走共享 WARP+D2D/ULW，参见 `native-desktop/rendering-and-ui.md:1007,1042`，不能把 Draw3 的 HARDWARE/WARP 结果外推给 UI3。

## Caveats / Not Found

- HC 是“最新可见 GitHub canary CI 产物”的强候选；未见 Canary Release，也未读取用户安装包或外部 WebDAV/CDN 的实际二进制，因此还不能断言它就是用户口中的“上一个 Canary”。H2 最新正式 Release 的 Inkeys2 身份由 Release 文本直接支持，但用户若指定其他安装版仍需修正。
- 未下载任何二进制、未做哈希解包、未运行 GUI/构建/性能测试；旧任务成功结果不可升级为当前 H0/HF 验证。H0/最终 HF 属主代理职责，本研究不提供其工作树指纹。
- 本研究没有运行 Git CLI；GitHub API 只读 refs 是观察时点快照，不覆盖不可见/删除/本地 refs。一次后续冗余计数 API 因沙箱代理拒绝连接，早先枚举结果已完整返回；若审计需再次冻结远端，请重读 API 并记录时间。
- WMI 的 OS/HotFix/GPU 查询在沙箱被拒；注册表与 PnP 可提供 build/驱动，但没有完整 KB 清单、活跃 renderer/presenter 或实际应用配置。当前 Oray 虚拟显示需要被视为性能噪声源。
- 08-09 与 09-22 数字分别是隔离模块/无 HWND API 探针，均不能证明上一个 Canary 的 UI3 卡顿已消失；Draw3 生产输入到可见像素的可比基线尚未找到。
