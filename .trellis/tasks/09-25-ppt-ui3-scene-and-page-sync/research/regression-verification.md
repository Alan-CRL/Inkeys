# PPT UI3 两项回归：本轮验证与剩余边界

文中的 `Build/...` 日志仅保存在本机，未纳入仓库；下列退出码和摘要是当次执行记录。

## 起点和范围

- 2026-09-25 本轮开始：`bugfix/pptui`、HEAD `2aeca374ea4c863eafe851ea41b52ca21018dd14`、工作区干净、既有 Trellis 任务为 `in_progress`。此前 A/B/结束页历史测试日志不计入本轮结果。
- 仅修改 Host 内容通知、PageControl 布局/Scene/ULW/窗口收敛、必要的 Window Service PPT 结果读回与本轮测试入口。PptCOM、ROT、真实 SlideID 和 UInk 格式均未改。
- 本轮没有启动真实 Office/WPS 放映，也未向真实下层窗口投递鼠标/触摸或操控用户文稿；这些为 **NOT VERIFIED**。

## A：已确认缺陷与修复前后证据

- `DrawingController::restoreAfterDocumentSlotSwitch` 在页槽切换时强制发布新的 `contentRevision`，即使两页 `hasContent` 同为 true 或同为 false。原 `Host::ObserveCurrentPageContent` 只比较 bool，直接跳过新 revision、内容条件通知和 runtime wake；随后成功 Present 的 `presentedContentRevision` 与 Host 旧 `contentRevision` 不相等，Selection 安全等待分支一直隐藏双表面。Pen 路径可显示主表面，因此符合用户“点绘制后又可见”的观察，但未用用户现场日志证明唯一原因。
- 在修 Host 前先加真实隐藏 Host Selection 页序回归；完整 Solution 构建 **exit 0**，`--draw3-hidden-test` **exit 1（预期失败）**。true→true 示例 `page=1 has=1 content=6 presented=8`；false→false 示例 `page=2 has=0 content=14 presented=18`。修复前 stderr：`Build/ARM64/Debug/selection_content_revision_pre_fix.err.log`。
- Host 现按完整 `(hasContent, revision)` 在原 `contentMutex` 内去重，任一字段变化均存储并通知两个现有等待通道；完全相同的载荷才跳过。保留 targetReady、成功 Present、输入 admission、UI-ready 和 EndScreen 身份校验。修后同一隐藏 Host 用例 **exit 0**，两种 presenter 都通过 Selection A→B→A→E→B→Z→A、跨文稿 false→false、真实 Window Service 辅助 ULW/主窗显隐、A/B/Z 保存 UInk ink-point 指纹、Host stop/restart 和返回 Pen 后 held-contact/退出回归。最终 stderr：`Build/Validation/pptui-regression-20260925/draw3-hidden-final.stderr.log`。隐藏用例没有运行完整 `IdtState::ReconcileDraw3PresentationState` 或 GPU 像素 readback；它通过目标/输出/呈现版本和内容指纹建立身份链，实际 Office 仍待验收。

## B：已复现的工程缺口与现场未知

- 已证实同一发布代四窗原先分别用各自旧 Scene outset/scale/context bitmap limit 拟合整组布局；修前 headless 注入同一 monitor `7680×4320`、DPI `2.5`、用户倍率 `3`，左右旧预算分别 `(10 DIP,16384)` / `(35 DIP,512)`，新增同对倍率一致断言使 `InkeysHeadlessTests.exe --no-window` **exit 1（预期失败）**。这证明工程上的布局分叉，**不能**据此认定就是用户机器的左窗消失原因。
- `ConfigureSurface` 原来忽略 Scene 配置/几何失败仍推进发布代；早期 D2D/ULW 失败缺调用点错误码，旧 backing 只增不减；PPT Show/Hide/SetBounds 未读回真实结果。这些是源码直接确认的收敛缺口，不是现场已捕获的故障。
- 修后四窗共用同发布/display/device 代的保守 outset/bitmap budget，旧 backing 受新上限约束；只有完整目标和 Scene 配置成功才提交。资源失败时仅清该侧历史 backing high-water 以当前最小需求重试；ULW 当场保存 Win32 error，重复确定性失败的昂贵事务有界退避。Window Service owner thread 对 PPT Show/Hide/SetBounds 读回真实可见性/RECT；失败侧不完成 UI-required mask。
- `--page-control-hidden-test` 在屏外 `(-30000,-30000)` 运行真实 Window Service、四客户端、Scene/ULW；最终 **exit 0**、`[PageControlHidden] failures=0`。覆盖 bottom-only/side-only/both，DPI96/144/192/240，倍率大→小→大、白板覆盖返回、四 HWND bounds、主体 alpha 读回和背景命中。左底 ULW 注入 `ERROR_GEN_FAILURE=31`、资源注入 `E_OUTOFMEMORY=0x8007000E` 时均不提前 ack；释放后无需用户输入自行恢复。最终 stdout：`Build/Validation/pptui-regression-20260925/pagecontrol-hidden-final.stdout.log` / stderr：`Build/Validation/pptui-regression-20260925/pagecontrol-hidden-final.stderr.log`。**这两个错误码是故障注入，不是用户现场错误码。**
- Trellis 全范围审阅另发现拖动热路径复用整组预算时只校验 monitor/DPI，设置发布代或设备代变化后可能用旧预算求解新拖动候选。已让拖动与渲染共用当前 publication/display/device 键；纯位置直移继续缓存，只有失效时从四个 Scene 重新取预算。取消拖动时 `ApplySceneBounds` 失败已有下一帧 `RequestDragPair`，未擅自重写该回滚路径。审阅修复后的完整 Solution、四窗隐藏与 Headless 又通过；真实用户显示器上的单侧问题仍待现场日志。
- 隐藏测试前两轮的终态几何失败分别由过早检查合法 Side 入场动画、以及测试改 DPI 后遗漏生产显示变更唤醒造成；修正测试驱动后通过，不能归为用户现场根因。真实单侧消失究竟来自布局分叉、ULW/资源、Show、alpha、Z 序或其他条件，仍 **NOT VERIFIED**。

## 本轮命令与退出状态

| 命令/环境 | 结果 |
| --- | --- |
| ARM64 原生 MSBuild，经 vswhere 定位，PowerShell 同次 `Remove-Item Env:PATH`、`MSBUILDDISABLENODEREUSE=1`；`InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false` | **exit 0**；Trellis 审阅修复后的完整日志 build-reviewed-final.log：`Build/Validation/pptui-regression-20260925/build-reviewed-final.log`。有既有 hashlib++ 转换 warning，0 error。 |
| `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | **exit 0**；审阅修复后 headless-reviewed-final.log：`Build/Validation/pptui-regression-20260925/headless-reviewed-final.log`。此前一次旧测试二进制 exit1，单独 ARM64 `/m:1` 重建 HeadlessTests 后已排除。 |
| `Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test`，隐藏进程、隔离保存目录 | **exit 0**，两种 presenter；stderr：`Build/Validation/pptui-regression-20260925/draw3-hidden-final.stderr.log`。该保存/冷读用例沿用既有沙箱外权限，因为沙箱内 UInk 原子替换会返回 `ACCESS_DENIED`。 |
| `Build/ARM64/Debug/Inkeys.exe --page-control-hidden-test`，隐藏屏外 HWND，`INKEYS_PAGECONTROL_PRESENT_TRACE=1` | **exit 0**；审阅修复后四窗、Scene 像素/命中、单侧故障恢复；stderr：`Build/Validation/pptui-regression-20260925/pagecontrol-hidden-reviewed-final.stderr.log` / 阶段日志：`Build/Validation/pptui-regression-20260925/pagecontrol-hidden-reviewed-final.stdout.log`。 |
| `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test` | **exit 0**，`PageControlScene failures=0`；stderr：`Build/Validation/pptui-regression-20260925/bar-offscreen-final.stderr.log`。 |
| `Scripts/i18n.ps1 check` | **exit 0**，en-US / zh-TW 均通过；日志：`Build/Validation/pptui-regression-20260925/i18n-final.log`。 |
| `task.py validate .trellis/tasks/09-25-ppt-ui3-scene-and-page-sync`、`git diff --check` | **exit 0**。implement/check 各 17 条有效上下文；三个大型既有 spec 超过 32 KiB 注入截断阈值，实施/审阅时已按章节单独读取。原编码/CRLF 与最小变更范围已复核。 |

一次并行 `/m` Solution 尝试在 PptCOM TLB 输出后无 C++ 诊断地 exit1，并留下 50 个由同一已结束父 PID 启动的 `/nodemode:1 /nodeReuse:false` MSBuild 孤儿节点。只读核对父 PID、启动时刻和命令行后，仅停止该批节点；改回此前稳定的 `/m:1`，完整构建通过。未为环境故障改源码、SDK 或工程配置。

## 人工验收仍需进行

1. 在真实 PowerPoint 与 WPS 已写过 A/B 页的文稿中，分别保持 Selection 和 Pen 翻页，检查 A→B→A→空页→B→真正结束页→A 的当前页墨迹与工具状态。Selection 下同时验证下层窗口收到系统实际鼠标/触摸消息，不能用直接 `PostMessage` 冒充穿透。
2. 在用户出现单侧消失的显示器/DPI、设置最大倍率及记忆位置上开启 `INKEYS_PAGECONTROL_PRESENT_TRACE=1`，保存四窗同代目标/实际 RECT、visible/owner、outset/limit、ULW HRESULT/错误码、重试和恢复日志；区分左窗屏外、被遮挡、零 alpha、未 Show 与资源失败。手动检查两边最终像素和点击命中、白板返回及任务栏/显示器切换。
3. 实体笔保持按下时翻页、进入 EndScreen 和真退出；验证旧触点隔离到物理终态，结束页保存恢复及桌面选择穿透。D3D Debug Layer/真 Office/WPS/实体硬件/多屏组合均 **NOT VERIFIED**。任务保持 `in_progress`，无本轮 commit/push 授权。
