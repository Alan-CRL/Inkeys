# PPT UI3 修复验证报告

## 状态与边界

- 基线/分支：bugfix/pptui，94e07b2599adab9de4286aa5fb7526e2c1c681f6；开始工作区干净，远端dev只读核对相同。
- 任务已通过已批准规划的 validate/start，保持 in_progress：代码与自动验证完成，真实 Office/WPS/键盘/设备/多屏交互仍 NOT VERIFIED，因此不提前归档。
- 没有 commit、push、回退基线、清理用户改动或启动产品/Office交互窗口。hidden/offscreen进程均已正常退出。
- 原文件编码/BOM/CRLF审计通过；未修改第三方源码、SDK、编译器、ROT枚举或Application绑定逻辑。新COM接口为独立IID，旧接口顺序/schema保留，DLL/TLB已从工程生成。

## 根因与证据分级

| 问题 | 基线证据/修复 |
|---|---|
| remember off 回弹 | 基线松手仅在persist=true时调用facade，后续完整快照带回旧位置。现在成功成对窗口提交始终按版本交接运行位置，记忆只影响明确保存触发。 |
| remember on 迟到状态 | 基线完整快照捕获/发布分离，ownsLayout释放后可接受旧位置；Settings异步JSON也能迟到。确定性生产状态测试覆盖旧快照、双pair交错、旧JSON和旧完成；没有宣称真实Office复现过。 |
| 保存成功混淆 | 原dispatch返回即解除persist保护且忽略写盘结果。现在冻结请求/可恢复基线/durable saved revision分离、统一跨队列版本和原子文件替换，失败保留旧字节与精确重试。 |
| 大缩放空隙/命中 | layout用DPI×user，PageControl和Scene重新截断最终值到4，Scene还抬高<.5结果。现分离输入DPI/user与最终scale，并实际离屏绘制/像素/命中验证。 |
| 切页延迟 | native wait在ready判定后重新读取revision会漏进展；active.empty门把按住输入变成无限等待；managed快速页写入与descriptor owner末尾Sleep500分离。现使用原revision等待、页边界收尾/隔离、事件唤醒现有owner及缓存状态。 |
| 生命周期/确认 | visibility在白板下为false，不能视为结束；原binding不是每场session。现独立show session、真实失效HWND/PID退出证据、场景边沿与带会话/请求ID确认。 |
| 键盘 | 旧IdtDrawpad.cpp是None，生产Facade hook为空；没有通过删旧文件冒充生产修复。实际改焦点交接与非激活路径保护，保留非PPT快捷键。 |

## 需求逐项状态

| 原需求 | 状态 |
|---|---|
| Trellis任务/规划/研究/状态/上下文 | DONE；其他已有任务未动。 |
| 拖动、记忆开关两边沿、结束、重置、失败重试 | IMPLEMENTED + headless PASS；重启读盘和冻结基线有测试，真实GUI路径待人工。 |
| >4/<.5有效缩放、命中、阴影、白板 | IMPLEMENTED +实际Scene offscreen PASS；真实多DPI显示器未验证。 |
| 页同步、contact边界、UI提交门禁、旧结果隔离 | IMPLEMENTED + headless/真实Host hidden PASS。 |
| UInk保存/冷恢复/重排 | 真实Host启用隔离保存目录：按住切页、排空保存、Controller重启、冷恢复及重排归属PASS（两个呈现测试模式）。 |
| 主栏进入/退出/白板/互不排斥 | IMPLEMENTED +边沿/策略/既有底栏测试PASS；可见动画、任务栏变化、跨屏仍NOT VERIFIED。 |
| 退出确认 | 按用户后续决定仅主栏EndShow；取消/失败/旧请求/同HWND事件重开有自动回归；实际弹窗视觉/Office交互未启动。 |
| 键盘与焦点 | 生产编译链审查及代码保护完成；普通键/数字Enter/Esc等真实Office行为NOT VERIFIED。 |
| 延迟与耗电 | 默认关闭分段QPC日志已接入；native等待窗口合成基线/修改后已实测；真实Office全链路及静止CPU/耗电NOT VERIFIED。 |

## 执行命令与结果

在每次MSBuild的同一PowerShell调用中先执行 Remove-Item Env:PATH -ErrorAction SilentlyContinue，并设 MSBUILDDISABLENODEREUSE=1。通过vswhere定位ARM64原生MSBuild（本机安装18/Community），未硬编码安装版本。

| 命令 | 结果/证据 |
|---|---|
| python .trellis/scripts/task.py validate … / start … | exit0；planning→in_progress，上下文大文档截断警告通过各agent按相关完整章节补读处理。 |
| MSBuild InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false | **exit0**；最终14.16秒，0 errors，3条既有hashlib++ C4267 warnings。[完整日志](D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925/build-final.log) |
| Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window | **exit0**，PASS animation correctness；位置/保存/缩放/主栏/弹窗/bridge/contact/session等。[日志](D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925/headless-final.log) |
| Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test（Start-Process -WindowStyle Hidden，等待退出，INKEYS_PPT_TIMING=1） | **exit0**，all hidden integration checks；两次real Host held-contact save/drain/cold reload/SlideID reorder PASS。[stderr](D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925/draw3-hidden-final.err.log) / [stdout](D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925/draw3-hidden-final.out.log) |
| Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test（隐藏，等待退出） | **exit0**，PageControlScene failures=0，含真实Scene .25/1/5/12及白板绘制/像素/命中。[日志](D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925/offscreen-final.err.log) |
| MSBuild PptCOM.Tests/PptCOM.Tests.csproj /p:Configuration=Release /p:Platform=AnyCPU /nr:false | **exit0**，0 warnings/errors；随后Solution也重建了对应产物。[日志](D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925/managed-build-final.log) |
| PptCOM.Tests/bin/Release/PptCOM.Tests.exe | **exit0**，PASS descriptor ownership and session/owner contracts。[日志](D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925/managed-integrated.log) |
| pwsh -NoProfile -File Scripts/i18n.ps1 sync / check | sync更新既有键后补齐翻译；最终check exit0，en-US/zh-TW均330/330。 |
| git diff --check | **exit0**；原文件编码/BOM/CRLF额外脚本审计PASS。 |

修复过程中实际遇到的失败没有隐瞒：第一轮/m:2构建在metaproject返回失败但0 errors，改用/m:1诊断后找到并修复源码haptic变量声明顺序；下一轮修复Bar模块头可见性；首次headless两项失败是新增测试复用禁用fallback请求污染旧reentry用例，改独立请求；首次hidden12项失败是两条旧Freeze-siblings断言重复六次，当前生产Owner链为Drawpad→Presentation→Freeze，已据真实源码更新。最后各测试退出码均为0。

hidden日志中部分save_submit/load_submit failed属于原有空autoSaveRoot的非持久化模式（worker未启动），不作为磁盘通过证据；新增专用真实保存模式明确开启worker、检查index/UInk并冷恢复后通过。测试文件保留在Build/ARM64/Debug/Draw3HiddenPptPersistence/41032-1688130172949与41032-1688211451669，未触及用户真实保存根。

## 延迟观测

| 测试条件 | 基线 | 修改后 |
|---|---:|---:|
| Windows ARM64，生产HostRuntimeRevisionSignal，人工安排ready恰好落在判断与等待之间；8次，timeout仍50ms | 新取revision导致timeout，平均 **61.0818ms** | 保留判定前revision直接返回，平均 **0.0211875ms** |
| 真实PowerPoint/WPS事件→画布→数字→可写 | NOT VERIFIED | NOT VERIFIED |
| 静止放映CPU/唤醒/功耗 | NOT VERIFIED | NOT VERIFIED |

第一行是确定性native等待边界回归，不是整体切页提速61ms的承诺。hidden的QPC分段日志提供真实Controller文档切换/Present/手动UI回执/输入开放证据；UI回执由harness驱动，不代表真实PageControl/Office的端到端延迟。native_observed明确是native首次观察时刻，不能冒充COM事件时刻。

## 剩余风险与人工步骤

见 [manual-validation.md](manual-validation.md)。没有真实Office/WPS或可见GUI验收；纯轮询提供方在观测间隙内以完全相同HWND重开、实际系统拒绝前台切换、多屏动画、自动隐藏任务栏和实体笔触仍需设备验证。已测试事件路径中的同binding/同HWND重开拒绝旧请求；不把该结果外推所有WPS提供方。

本次不新增跨进程PPT墨迹恢复承诺；位置配置跨重启恢复与PPT UInk当前进程恢复是不同合同。新DLL/TLB与native应成套发布；旧DLL兼容页同步，但无法提供安全会话令牌时主栏确认退出失败关闭。

## 审阅与收尾

独立Trellis检查见 bar-check.md / page-check.md；位置/托管/Draw3证据见 layout.md / managed.md / draw3.md。已更新ppt-interop和native-desktop相关规范。任务维持in_progress等待人工验收，记录会话使用--no-commit，不创建提交、不推送、不伪装发布。
