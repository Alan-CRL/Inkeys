# 实施计划

> 状态：用户已批准实现并确认 D-P01；同步任务元数据后通过 `task.py start` 进入第二阶段。

## 执行原则

- 每阶段只改该合同所需文件，先补纯逻辑测试，再接 Win32/D2D。
- 不一次性重写 `IdtMain.cpp` 或 `Bar.RenderLoop.cpp`；每阶段完成后先跑目标 headless tests，再构建完整 `InkeysRepo.sln`。
- 对新增 Win8.1+ API 均动态解析；对回调、等待、线程退出和 device generation 做失败注入。
- 正式 BIN 只能在真实 Bar capture mode 和生产 parser 验证完成后加入；不得先放占位资源。

## 任务分解

### 1. Progress tracker 与纯状态机

- 新增 `Inkeys/Inkeys/Startup/Startup.Progress.cppm/.cpp`，实现 immutable plan、atomic milestone、failure freeze、snapshot 和 100% gate。
- 新增 `InkeysHeadlessTests/startup_progress_tests.cpp`，覆盖重复/乱序/并发报告、禁用 Preview 的 conditional plan、elapsed 不增长、失败冻结和 Bar commit 唯一 100%。
- 同阶段建立 Preview/cache/Bar 纯枚举与 transition reducer，不创建 HWND/D2D。

### 2. BIN/cache serializer、parser、CRC

- 新增 `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.Format.cppm/.cpp`；字段逐字节 little-endian 读写，加入本地 IEEE CRC-32 与 stable visual signature encoder。
- 新增 `InkeysHeadlessTests/startup_preview_format_tests.cpp`；逐字段截断、checked overflow、64 MiB 上限、非法矩形、错误 CRC、unsupported version、四分类和标准 CRC 向量。
- 只实现普通内存/stream 合同，不接资源、窗口或正式缓存目录。

### 3. StartupPreview 独立窗口

- 新增 `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cppm/.cpp` 的 owner thread、命令 queue、无激活窗口类、点击吞掉、show/position/hide/destroy 和 bounded stop。
- 为可测纯命令 reducer 增加 `InkeysHeadlessTests/startup_preview_state_tests.cpp`；HWND 行为放到可窗口测试/手工矩阵，受限环境仍用 `--no-window`。

### 4. D2D target、blur、shimmer、progress rendering

- 在 StartupPreview 实现 per-generation target/resource、embedded upload、cubic、GaussianBlur bounds、`FillOpacityMask` shimmer 和确定进度条。
- 所有创建/复制/effect 都只在 RenderPipeline callback；分别注入 bitmap/blur/shimmer/ULW 失败验证降级。

### 5. RenderPipeline client

- 修改 `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm/.cpp`：新增 `Client::StartupPreview`，dispatch order 放在 Bar 后，保持 unregister drain 和初始化幂等。
- 扩展 `InkeysHeadlessTests/render_scheduler_tests.cpp`，验证仅 Preview 注册、Bar-before-Preview、unregister drain、epoch recovery。

### 6. Bar committed-frame bridge

- 修改 `Inkeys/Inkeys/UI/Bar/Bar.cppm`、`Bar.Initialization.cpp`、`Bar.Rendering.cppm/.cpp`、`Bar.RenderLoop.cpp`：显式启动状态、成功帧 metadata、非 target proxy 和 stable cache candidate。
- 修复 WindowMissing/client registration/signal stop 的 silent return；失败帧不发布、不缓存、不报 100%。

### 7. Presentation alpha transaction

- 新增纯逻辑 `Inkeys/Inkeys/UI/Bar/Bar.PresentationAlpha.h`，在 RenderLoop 接入 requested/attempted/committed、full-window demand 和 retry。
- 新增 `InkeysHeadlessTests/bar_presentation_alpha_tests.cpp` 并扩展 `present_decision_tests.cpp`；覆盖四步提交失败排列、业务 dirty 不误推进和无 Preview=255。

### 8. IdtMain 启动阶段接入

- 小步修改 `Inkeys/IdtMain.cpp`：合法 T0、mini read、DPI 前移、条件化 early RenderPipeline、stage report、fatal helper、shutdown 顺序。
- 修改 `Inkeys/Inkeys/Window/Window.cppm/.cpp` 增加可注销 topmost observer；修改 `Window.Legacy.cpp` 让 TopWindow 消费明确 ready/failure。
- 修改 `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h/.cpp` 与 `Draw3.Product.h/.cpp`，添加 noexcept 非阻塞 milestone callback。
- 修改 `Inkeys/IdtFreezeFrame.cpp` 和 `Inkeys/IdtPlug-in.cpp`，只在实际 Freeze ULW/PPT UI client 成功点报告。

### 9. Setting/config

- 修改 `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`：默认 true、mini paths 和 full persistence。
- 修改 `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h`、`Setting.cpp`：UI3 实验卡片“启动时快速显示主栏”，说明“下次启动时显示主栏预览；更改将在下次启动生效。”切换只排队写配置，不影响本次 runtime。
- 扩展 `InkeysHeadlessTests/setting_session_state_tests.cpp`。

### 10. BIN 生成与 RC/project 接入

- 实现 debug/developer capture mode，必须由真实 normal Bar scene 的稳定 committed frame 导出，并验证 96 DPI、中文、深色、默认按钮、signature/epoch/CRC。
- 生成唯一正式 `Inkeys/Inkeys/UI/StartupPreview/StartupBarPreview-v1.bin`，并记录可复现命令/expected signature。
- 修改 `Inkeys/resource.h`（计划 ID 306）、`Inkeys/Inkeys.rc`、`Inkeys/Inkeys.vcxproj`、`Inkeys/Inkeys.vcxproj.filters`；资源类型专用，不触碰 PptCOM 221。

### 11. 错误状态与清理

- 统一 T0 后 fatal helper：Tracker freeze -> red Preview committed/350ms -> 现有 MessageBox -> shutdown。
- 完成 topmost observer unregister、render callback drain、owner destroy、writer stop/join 和 device-loss recovery；所有等待加上限。
- 验证 Preview/cache/effect 失败均不阻止主程序，RenderPipeline 和正式启动阶段失败仍是致命。

### 12. 全量构建与 Win7 验证

- 构建完整 `InkeysRepo.sln` 的 Debug/Release Win32、x64、ARM64（以本机 toolchain 实际可构建为准），运行 `InkeysHeadlessTests.exe --no-window`。
- 对 ARM64EC 做现有 `_M_ARM64EC` 源码/静态 API 审计；仓库无该工程配置，不擅自新增。
- 在 Win7 SP1 + KB2670838 VM 用 WARP/FL11.0 执行 `test-matrix.md` 的手工路径；记录无法自动化的窗口、DPI、SuperTop 和 device-loss 证据。
- 检查 `git diff --check`、编码/CRLF、无无关格式化、资源 reproducibility、Trellis acceptance 全部映射。

## 逐文件变更计划

| 文件 | 第二阶段拟修改原因 |
| --- | --- |
| `Inkeys/IdtMain.cpp` | T0、DPI 顺序、mini config、条件化 early pipeline、progress/failure/shutdown orchestration |
| `Inkeys/Inkeys/Startup/Startup.Progress.cppm/.cpp`（新） | 纯 tracker、plan、milestone、failure snapshot |
| `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cppm/.cpp`（新） | owner HWND、render client、动画、handoff、cache worker |
| `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.Format.cppm/.cpp`（新） | BIN/cache parser、serializer、CRC、signature |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm/.cpp` | 新 client、dispatch order、early idempotence/epoch contract |
| `Inkeys/Inkeys/UI/Bar/Bar.cppm` | 启动状态和 committed frame 公共合同 |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | 消除 silent return、报告真实子阶段/client 注册 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm/.cpp` | 安全 proxy/staging copy 与 stable frame metadata |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | committed 发布、alpha transaction、full ULW retry |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationAlpha.h`（新） | 可独立测试的 requested/attempted/committed reducer |
| `Inkeys/Inkeys/Window/Window.cppm/.cpp`、`Window.Legacy.cpp` | topmost observer 与显式 startup states |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h/.cpp`、`Draw3.Product.h/.cpp` | 真实 Draw3 子 milestone callback |
| `Inkeys/IdtFreezeFrame.cpp`、`Inkeys/IdtPlug-in.cpp` | Freeze committed 与 PPT UI 注册真实报告点 |
| `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp` | 新开关默认值、mini read、持久化 |
| `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h`、`Setting.cpp` | UI3 实验开关和下次启动生效文案 |
| `Inkeys/resource.h`、`Inkeys/Inkeys.rc` | embedded BIN 资源 ID/type；保持 221 |
| `Inkeys/Inkeys.vcxproj`、`Inkeys/Inkeys.vcxproj.filters` | 登记新 module/source/asset |
| `Inkeys/Inkeys/UI/StartupPreview/StartupBarPreview-v1.bin`（新） | 真实默认 Bar capture 产生的唯一 canonical asset |
| `InkeysHeadlessTests/*.cpp` 与 `InkeysHeadlessTests.vcxproj` | tracker、format、state、alpha、pipeline/config 测试 |
| `.trellis/spec/native-desktop/startup-preview.md` 与本 task 文档 | 随实际实现同步合同、决策和验证结果 |

文件列表是调查后的最小预期集合；第二阶段若某项能在既有文件内完成则不额外拆文件，若无需触碰的文件不会为了列表而修改。
