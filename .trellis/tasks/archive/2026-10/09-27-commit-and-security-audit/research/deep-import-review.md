# 2025 Draw3 源历史与 2026-08-16 产品集成深审

审查快照：H0 `8b156fca59f0337a6afc6d722941666fcf143080`；2026-09-27。范围仅为 `audit-deep-pending.tsv` 指定的 8 个 SHA。此处是静态补丁、当前调用链和二进制元数据审查；没有运行主 GUI、Win7 真机或性能采样。工作树后续修补应由 H0→HF 总审重新核对。

## 为什么旧日期提交属于本轮

`12fabc3c → 5fc6cd8c → 851e7cd8 → 901469d6 → b83d2fc4 → 3dcd14aa → 887aba07` 是旧独立 `Inkeys3-Draw3` 源仓历史，作者/提交时间均为 2025-11。`1bc302d1` 在 2026-08-15 以第二父 `8d045298` 合入该历史；`fc9a8b86` 在 2026-08-16 把它改造为 Inkeys 产品模块。因此旧 SHA 是“近期合入的历史源码”而不是 2025 年已经发布的 Inkeys 产品代码。`1bc302d1` 的两父差异另在父任务 `audit-coverage.tsv` 独立一行；本表不把 merge 证据冒充八个 SHA 的审查。

核对命令：`git show --format= <SHA> -- <changed paths>`、`git diff-tree --no-commit-id --name-status -r --root <SHA>`、`git ls-tree -r <revision> <path>`、`git rev-parse <revision>:<path>`、`git grep <H0> -- <product paths>`。大规模第三方导入按文件清单、许可证/版本/项目引用、Git blob 和二进制指令检查；没有逐行人工审阅数十万行第三方源码，不能由此声称第三方实现无缺陷。

## 八项补丁及 H0 映射

| SHA | 实际补丁重点 | H0 产品映射与静态结论 | 尚需验证 |
| --- | --- | --- | --- |
| `12fabc3c` | 根提交只新增 `.gitattributes`、GPLv3 `LICENSE`、`README.md`。 | 产品根仍有许可证与第三方 `NOTICE`；无 Draw3 执行代码。归类为历史许可证/文档导入，未确认执行期缺陷。 | 发布包是否随附所需 NOTICE/许可证由 Work 5 检查。 |
| `5fc6cd8c` | 新增 1610 文件，包括旧 demo Solution、HiEasyX、1487 个 Abseil 路径、55 个 Ink Stroke Modeler 路径和三架构库。 | H0 主 `InkeysRepo.sln` 不含独立 `inkStrokeModelerTest.sln`/旧 `main.cpp`；`Inkeys.vcxproj` 改用产品 `Draw3.*`。H0 `Inkeys/additional` 保留的 351 个 Abseil 文件及 18 个 Modeler 源文件 Git blob 与源快照 `8d045298` 相同，另有 provenance README；未编译第三方 `.cc`。旧 demo 的 HiEasyX 不等同于主产品 `Inkeys/HiEasyX`。 | 固定库的上游构建链/源提交不能仅由 blob 与头文件证明；三架构运行和分发许可证仍待验。 |
| `851e7cd8` | 旧 demo 把一个巨大的 `main.cpp` 缩为入口，新增 `main.h`、`renderer.h`、`shader.hlsl`；此阶段按磁盘 HLSL 运行时编译并曾每次绘制建 VB。 | 这些路径未被 H0 主 Solution 登记；产品使用嵌入 shader 资源、`Draw3.Renderer*` 与生成 `.cso`，不是这份旧 renderer。未将此历史性能路径误记为产品热路径。 | 产品 Draw3 的实际上传/绘制成本属于 Work 3，不从此提交外推。 |
| `901469d6` | 967 文件变动：删除 930 个旧 Abseil 与 23 个 Modeler 路径，旧 demo 增加 HLSL、CSO、RC/项目 FXC 条目，从磁盘 shader 转向资源；Debug 当时仍为 `/MTd`。 | H0 主项目只以公开/传递头文件加固定 `ink_stroke_modeler_merge.lib` 链接；`Inkeys/additional` 当前 0 个 `.cc`。产品 Debug 已设置 `/MT`、取消 `_DEBUG`、使用 Release Vcpkg ABI，不能把旧 demo 中间态当产品配置。 | 运行时嵌入资源有效性、Win32/x64/ARM64 clean Rebuild 由 Work 5；不把旧 `.cso` 存在当成功证据。 |
| `b83d2fc4` | 旧 demo 开始传变半径笔段参数/shapeType，修改顶点输入、renderer 与 HLSL；有历史 `CreateBuffer`/`Map`/HRESULT 检查薄弱分支。 | 源快照 `8d045298` 与 H0 产品 `Draw3.Renderer*`/`Assets` 后续已重写；此提交的旧 `renderer.h` 未在产品编译。未确认该薄弱分支仍存在于产品。 | 产品 HLSL/CPU 结构布局、失败处理及真实视觉由 Work 3/5。 |
| `3dcd14aa` | 旧 demo 改像素 SDF、HLSL 顶点插值与 `InkVertex`，新增调试设备 flag 和弹窗式诊断。 | H0 产品 shader 的 Git blob 与该 SHA 不同；产品 GPU 初始化没有沿用 demo `D3D11_CREATE_DEVICE_DEBUG` 默认值/弹窗。不能用该 demo 的 SDF 计算代表最终产品结果。 | 生产 shader 的真机画质、FL11.0 编译/执行仍需核验。 |
| `887aba07` | 旧 demo 再次替换不等半径 capsule SDF，移除默认 debug device flag，修改两个可见笔段。 | H0 产品 `Assets/inkPixelShader.hlsl` 与该 SHA、乃至源快照 `8d045298` 都不是同一 Git blob；H0 实现已有零长段及大圆包含分支，不能把旧 shader 除零假设直接报成现存缺陷。H0 `Assets/inkVertexShader.hlsl` 与源快照 blob 相同。 | 形状/极慢书写/压力边界实际像素与 GPU Debug Layer 仍未验证。 |
| `fc9a8b86` | 953 文件：787 新增、140 删除、26 修改；按路径为 `Inkeys/additional` 590、产品 `Inkeys/Inkeys/Drawing/Draw3` 54、其他 `Inkeys` 23、Trellis 269、旧 demo 9、其他 8。真正产品迁移涉及 `IdtMain`、`IdtDrawpadFacade`、`IdtState`/Bar、Draw3 Host/Bridge/输入/renderer/presenter、主 vcxproj/RC。该提交暂删三个固定 lib，随后 `13e81fa0` 恢复；H0 lib 与源快照 blob 一致，此中间构建断裂已修复。 | H0 正式调用链和失败路径如下。**F006 confirmed at H0**：presenter 自动/恢复候选仍含两种 DWM，违反用户规定的 DComp/ULW 门禁；本任务的并行产品修补须经独立 review、构建及 Win7 验证后才可结案。状态/笔型旁路属 F009，Work 1 静态修补已有，动态 UI/快速反向仍未验证。Save/SuperRecovery/InputTest 等明确 Unsupported/隐藏入口不算缺陷。 | 真机透明、GPU/输入手感、退出保存、兼容矩阵、性能 Work 3；本轮不能写发布通过。 |

## 产品导入的实际链路与失败边界

1. `IdtMain.cpp` 的 Window Service 创建唯一主 Drawpad 和前置 `DrawpadPresentation`；调用 `Draw3::StartProduct`，DComp 失败时在首次显示前停止 Host/窗口链、重建并禁用 DComp。`Draw3.Product.cpp` 以 call mutex 封装 Host、WndProc 和桥接状态；`IdtDrawpadFacade.cpp` 的旧入口为兼容空壳，旧 RTS/hook 不再次安装。
2. Host 绘制线程先 `InitializeGraphicsDevice`，再建立 presenter/renderer、首帧成功标志，随后启用 RTS；`IdtState::SyncDraw3State`/UI/PPT 发布快照与命令，Bridge 带工作区及文稿身份；绘制线程消费后进入 `DrawingController`、`InkPrediction`/Modeler、几何、GPU 与 Present。`Draw3.Product.cpp` 的 Stop 先封锁新调用，Host Stop 停 RTS、排最终保存命令、排空 worker、停止绘制线程，Window Service 最后销毁 HWND。这里仅确认代码顺序，未用故障注入证明崩溃恢复或可见像素成功。
3. H0 `Draw3.GraphicsInitialization.cpp:49-99` 对 HARDWARE 和 WARP 均先请求 11_1/11_0，`E_INVALIDARG` 时重试只含 11_0，硬件失败再 WARP；未证明无 FL11.0 硬件的目标设备必然成功。`Draw3.TransparentPresentation.cpp:43-58` 用绝对 System32 路径动态加载可选 `dcomp.dll`。H0 `:90-95` 的 DWM 自动候选是 F006；` :626` 保持 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`。用户已实测 Win7 SP1+仅 KB2670838 上 FLIP 可用，故该结论优先作为本任务目标环境约束；不得依据微软通用文档改为 bitblt。动态 DComp/ULW 路由修复及真实硬件 FL11.0 有/无、WARP/导入表检查由发布矩阵承接。

## 固定库、依赖与授权边界

- `inkStrokeModelerTest/lib/{lib32,lib64,libArm64}/ink_stroke_modeler_merge.lib` 在源 `8d045298` 与 H0 的三个 Git blob 分别完全相同。`dumpbin /headers` 对每个 archive 返回 0，165 个对象的 machine 各自一致：`14C x86`、`8664 x64`、`AA64 ARM64`。`dumpbin /directives` 三者都有 `/DEFAULTLIB:LIBCMT`、`RuntimeLibrary=MT_StaticRelease`、`_ITERATOR_DEBUG_LEVEL=0`、`_MSC_VER=1900`；这只验证二进制 ABI 指令与架构，不证明上游构建来源或 Win7 可运行。
- H0 头文件宏 `ABSL_LTS_RELEASE_VERSION=20250512`、`PATCH_LEVEL=0`；上游 [Abseil 发布页](https://github.com/abseil/abseil-cpp/releases/tag/20250512.2) 已列同 LTS 的 Patch 2。当前 [Abseil 官方 security advisories](https://github.com/abseil/abseil-cpp/security/advisories) 和 [Google Ink Stroke Modeler security overview](https://github.com/google/ink-stroke-modeler/security) 在审查时未列已发布 advisory。这不证明固定静态库不存在漏洞，也不能因有较新 patch 就擅自更换 ABI 固定库；若另有具体 advisory，需按受影响符号与可达输入重新核验。
- 当前产品的 Modeler 输入来自 `Draw3.DrawingController.cpp` / `Draw3.InkPrediction.cpp` 的笔触模型链；旧 demo `main.cpp` 并非可达入口。UInk 解码、文件索引及外部输入属后续产品功能，不能拿本项旧 demo 审查替代。根 `NOTICE` 列 Abseil/Modeler，`ThirdpartyLicenses/Apache License 2.0` 存在；分发包是否实际携带授权文件仍未验收。

## 状态与交接

这 8 行的**实际补丁、H0 映射和静态失败路径已审**；剩余的是 Win7/HARDWARE/WARP/FLIP 与呈现真机、三架构完整 Rebuild/导入表、固定库来源证明、发布包授权、模型输入及性能 Work 3、最终 HF diff。`fc9a8b86` 的 F006 不能因源文件已出现修补而标记“验证通过”；需要独立 reviewer 检查最终 diff 和生产测试。其他七个旧 demo SHA 不应自动算作产品缺陷，也不等于第三方无已知问题。
