# Capture-stage Trellis check（2026-10-21）

Active task: `.trellis/tasks/10-21-scroll-follow-mag-capture`。

代码审查、ARM64复验与x64构建尝试均已完成。ARM64完整构建/无窗口回归通过；x64主程序链接成功，但完整solution因测试PDB上限失败。真实捕获门槛未通过，本任务不得标记完成，后三阶段保持 planning。没有修改生产捕获/运行库策略，没有 commit 或 push。

## Findings (fixed)

- File: `InkeysHeadlessTests/mag_capture_tests.cpp:931`。
  - Issue: 原 PoC 的“source metadata”断言只检查递增 sequence、session 和正数尺寸/时间；没有证明固定 Display 快照、stride 或 span 合同，读 marker 也没有以前述完整验证作门禁。
  - Fix: 核对连续 sequence、Source/WorkArea、Display generation、精确尺寸/stride/span、单调 QPC 与有限非负耗时；元数据不符时不再读 marker。没有改变质量判定或放宽原失败断言。
- File: `InkeysHeadlessTests/mag_capture_tests.cpp:730`。
  - Issue: 阻塞回调用例声称验证运行库保活，但只观察了 HWND 与 resourcesReleased。
  - Fix: 同时直接断言 managed runtime lease 仍为1；保持原1秒监督、非阻塞Stop、ERROR_BUSY与真实退出后再启用例。
- Files: `InkeysHeadlessTests/mag_capture_tests.cpp:902`、`InkeysHeadlessTests/animation_tests.cpp:1649`。
  - Issue: bare-preheld-lease fixture 没有代表实际启动已创建但从未显示的首个定格控件，不能独自证明这一产品前提。
  - Fix: 保留原 `--mag-capture-poc`，新增独立 `--mag-capture-poc-freeze-control`。原 owner 在主线程 STA/Display/首次 managed lease 后创建 Host/Child，匹配 WS_CLIPCHILDREN、MS_CLIPAROUNDCURSOR、无 TRANSPARENT 的 Host 扩展样式、DefWindowProc/classStyle 与少一像素高度，设置 identity Transform/ColorEffect。创建后从未 Show，且没有先设置 Source/Filter 或采样。另一线程运行真实 MagCaptureService；Stop 后才首次显示原控件并请求新 phase90 像素，原 HWND 销毁后才释放最后 lease。没有额外 Initialize/Uninitialize，也没有迁移生产 owner。

原文件 UTF-8 BOM/CRLF、新源码 UTF-8 无BOM/CRLF均保留。审查没有修改 MagCaptureService、MagnificationRuntime、IdtMagnification 或其他生产源码。

## Findings (not fixed)

1. **共享场景真实门槛仍失败。** 新的 fresh-hidden first-owner/global-once 对照记录 main_com=0、init=1/uninit=0/leases=1。真实 service 的130个结果全部在 Filter/ERROR_NOT_SUPPORTED(50) 拒绝，0 valid；20.1457是快速失败结果/s，不是有效捕获FPS。metadata、Stop、资源与lease清理通过。Stop 后原控件的新像素读回也未通过（API到pixels成功、像素0xff000000）；缺少同样样式的独立先验正对照，不能将其解释为“捕获破坏了定格”。这是尚未解决的能力/owner设计问题，不能机械地加初始化、提前反初始化、迁移窗口或替换截图技术来修。生产策略保持批准的首次全局 Initialize/最后使用者 Uninitialize。
2. **本次 cold/生命周期像素复跑也失败。** `--mag-capture-poc-cold` 55帧全为 NearBlack/error0/mean0/variance0，0 valid，metadata通过；raw alpha0-topmost对照同样黑。生命周期的初始化失败、回调异常、1秒监督、lease保活、Stop=0ms、busy拒绝、解除阻塞后清理均通过，但末尾“重新收到真实有效像素”失败，整体exit1、init4/uninit4。此前 STA 对齐的成功日志保留为历史证据，不能覆盖本次失败。审查确认新增fixture在cold路径不调用任何额外DLL/窗口API：分支外只有空unique_ptr和nullptr/零atomic函数表；原 helper 默认样式/Show语义不变。旧x64/newARM64 Headless PE均无Magnification/MagInitialize/MagSetColorEffect硬导入，不存在由新ColorEffect测试代码导致的eager DLL导入。当前没有足够依据改生产代码或把波动归为某一SDK/硬件根因。
3. **替代 owner 实验尚非可采纳方案。** 最终外层 STA-before-Display/firstDLL 次序下重跑 native-thread：outer main_com=0、nested=1，两种策略各4轮第二owner均Filter50，source_painted=1。先前同owner双控件像素成功但private bytes增长，不能当作已证明安全或已定位泄漏；更改生产owner/生命周期属于设计决策，超出本轮检查修补范围。
4. **实际永久阻塞 PrintWindow 尚未验证。** 生命周期测试阻塞的是已进入回调，经过真实监督/保活路径；不能将它称为系统内部永久挂起的真实返回验证。真实产品Freeze端到端、Win7/8.1、浏览器/Word/PDF、笔/触控同样未由这些专用fixture证明。没有启动产品、设置或用户文档窗口。

5. **x64完整solution尚未通过。** 首次C1060 compiler heap错误在命令级CL_MPCount=1的唯一重试中未重现，产品主程序已成功链接；测试工程最终因LNK1140 PDB limit失败。这是本机构建产物/资源限制，未据此修改源码、PDB选项、项目配置或全局工具链，也不再重试。此前成功链接的x64 Headless EXE在失败后已不存在，没有版本可确认的现有产物可用于本次x64无窗口运行。

审查实际线程路径后，没有发现可确认的生产数据竞态、COM配对错误或借用缓冲提前释放。owner保留HWND/DIB/lease到返回；Stop关闭发布而不join；resourcesReleased还核对两个真实线程句柄signal，旧回调依赖须由调用者保活到该边界。此结论不代替上述失败的真实像素门槛。

## Verification

### 构建与命令

通过 vswhere 定位安装与 `MSBuild/Current/Bin/arm64/MSBuild.exe`。每个MSBuild命令均在同一PowerShell invocation保存原Path、Remove-Item Env:PATH后恢复单一Path，并设置 `MSBUILDDISABLENODEREUSE=1`；TEMP/TMP为仓库内 `Build/ScrollFollow/TestTemp`。使用完整solution，没有单独构建native vcxproj，没有平台并行。

```text
MSBuild.exe InkeysRepo.sln /nologo /m:1 /nr:false /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64
MSBuild.exe InkeysRepo.sln /nologo /m:1 /nr:false /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=x64
MSBuild.exe InkeysRepo.sln /nologo /m:1 /nr:false /p:BuildInParallel=false /p:CL_MPCount=1 /p:Configuration=Debug /p:Platform=x64
```

- ARM64最终重建 exit0，0 warning/0 error，8.68s；`Build/ScrollFollow/capture-check-build-arm64-final.log`（之前审查增量15.03s也exit0，日志另存）。
- x64首次完整构建 exit1，15:17.62；现有manifest依赖安装成功，OpenCV 4.10.0#3版本/features不变，Headless链接成功。主程序首错 `json/value.h:292 C1060: compiler is out of heap space`，另3处相同compiler heap错误；`capture-check-build-x64.log`。没有为资源错误改源码、vcxproj、依赖或全局工具链。
- x64低并发重试 exit1，11:44.66，345 warning/1 error；`capture-check-build-x64-serial.log`。只增加命令级CL_MPCount=1；主程序Inkeys.exe成功链接（51,409,408 bytes，2026-10-21 15:04:11），Headless源码编译完成，唯一首错为LINK LNK1140: limit exceeded for program database。警告主要来自随附库及现有代码，未为清零改无关源码。未修改/PDB选项，也未再次重试。失败后 `Build/x64/Debug/InkeysHeadlessTests.exe` 不存在，因此未执行x64测试。

### 测试与静态检查

所有日志位于 `Build/ScrollFollow/`，测试逐个新进程串行执行，GUI仅自动关闭专用fixture。

| 检查/命令 | 退出码 | 实际结果 / 日志 |
| --- | --- | --- |
| ARM64 `--mag-capture-only` | 0 | `capture-check-only-arm64.log` |
| ARM64 `--no-window` | 0 | `capture-check-headless-arm64.log`，PASS animation correctness |
| ARM64 `--mag-capture-poc-freeze-control` | 1 | `capture-check-freeze-control-arm64.log`，fresh-hidden/global-once第二owner Filter50；原控件像素未证明 |
| ARM64 `--mag-capture-poc-cold` | 1 | `capture-check-cold-arm64.log`，55帧/0valid，全部NearBlack |
| ARM64 `--mag-capture-lifecycle-poc` | 1 | `capture-check-lifecycle-arm64.log`，监督/Stop/保活通过，最后有效像素再启失败 |
| ARM64 `--mag-runtime-thread-poc` | 1 | `capture-check-thread-sta-arm64.log`，修正后的外层STA前提，第二owner两组各4轮Filter50 |
| x64 `--mag-capture-only` / `--no-window` | 未执行 | 最终Headless链接因PDB上限失败，之前EXE也不存在；不执行不可确认版本的产物 |
| `Scripts/i18n.ps1 check` | 0 | en-US/zh-TW各425/425，PASS；没有运行会写文件的sync |
| `git diff --check --ignore-submodules=all` | 0 | 无空白错误；未宣称检查子模块内部 |
| XML/登记/编码/EOL | 0 | 3份工程XML、capture登记无重复，原BOM与CRLF、新源码无BOM/CRLF与尾随空白检查通过 |
| ARM64/x64产品PE导入 | 0 | `capture-check-imports-arm64.log`、`capture-check-imports-x64.log`，未见IsWow64Process2/GetDpiForWindow硬导入或OpenCV/FFmpeg DLL；只作静态证据，不声称旧Windows运行通过 |
| Headless PE lazy-load检查 | 0 | `capture-check-headless-imports-ARM64.log`、`-x64.log`；最终ARM64与首次已链接x64旧fixture均无Mag硬导入，后者EXE在最终失败后已不存在 |

- Lint: PASS（diff/XML/i18n/编码检查；仓库没有独立native lint命令）。
- TypeCheck/C++编译: ARM64 PASS；x64源码编译与主程序链接PASS，但完整solution FAIL（Headless PDB limit），不能记为x64全量构建通过。
- Tests: ARM64无窗口回归PASS；真实捕获验收FAIL；x64运行未执行（无成功链接的最终Headless产物），不把本任务标记完成。

长期 `.trellis/spec/native-desktop/scroll-follow-capture.md`、父验收矩阵与研究报告由主会话同步，必须保留这次fresh-control、cold/生命周期失败及历史成功的区别。后续只应在解决Mag共享门槛后重新验收，再决定是否进入Tracker。
