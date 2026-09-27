# 补充修复验证记录（2026-09-25，本次执行）

文中的 `Build/...` 日志仅保存在本机，未纳入仓库；下列退出码和摘要是当次执行记录。

## 基线与边界

- 开始时为 `bugfix/pptui`、HEAD `9d432cd6c68bc7e1c04933702e3dc85ef4e6e669`、工作区干净；Trellis 当前任务 `09-25-ppt-ui3-scene-and-page-sync` 为 `in_progress`。没有 reset、清理或改动其他任务。
- 本次未在 PowerPoint/WPS 可见放映、实体笔/触摸、真实下层窗口系统命中上执行验收。过去 `research/verification.md` 的日志是前一轮历史，不计入下表。
- A 的**源码已证实**状态空隙：可信 EndSession 原来没有统一 Selection 收尾；`ReconcileDraw3PresentationState` 对输出未 ready 的 Waiting 保持旧主 Drawpad 可见并清 retry，Host admission 只管 contact。具体用户机器上的拦截 HWND 和时序仍 **NOT VERIFIED**。本次增加的 `INKEYS_PPT_EXIT_TRACE=1` 默认关闭、等待期约每 2 秒采一次，输出生命周期/bridge/runtime/输出/捕获及 Window Service 各角色 HWND 属性，可供现场确认。
- 审阅中另确认退出检测→业务收尾之间可能被后来的用户工具操作穿插。修复把模式比较/赋值/版本增加串行化，但在同步 Window Service 事务前释放模式锁；窗口 owner thread 在释放 Drawpad capture 或显示表面前检查最新 bridge revision。隐藏测试直接覆盖主 Drawpad capture 释放和双表面提交；真实用户点击与新笔竞争的系统级时序仍属人工验收。
- B 的**源码已证实**失败链：State 5 descriptor 提前返回无拓扑；native 不发布目标；稳定 UInk 的编码、解码和严格应用导入均拒绝无 SlideID 的普通画布。修复后结束页用显式 kind/marker，不改变正常 SlideID 校验。真实 Office 在 State 5 枚举 Slides 的成功率仍 **NOT VERIFIED**。
- 已知兼容边界：同一路径稳定文稿的增删/重排只在保存的 SlideID 集与当前集合仍有至少一个交集时自动恢复。若所有真实 SlideID 都换新，`CompatibleSlideIdSet` 返回冲突，结束页也暂不冷恢复。反过来，不同文稿若占用同一路径且至少复用一个 SlideID，当前交集检查仍可能误判；现有 `bindingToken` 含放映 HWND 和 binding revision，不能直接作为跨放映的持久文稿身份。此风险需独立的稳定文稿身份方案。
- 既有会话边界风险：`PptInfo::EndSession` 在 binding-only 重绑也会调用 `Ppt::PublishSession(old,false)`；记忆开启时该 facade 可能按旧场次结束保存一次位置。此行为在本补丁前已存在，本次 A/B 未重定义 PptCOM 的 binding/session 语义，需真实 Office 重绑条件再判定是否属于产品缺陷。

## 已执行命令和结果

| 命令 / 条件 | 本次结果 |
| --- | --- |
| `python .trellis/scripts/task.py current --source`、`validate .trellis/tasks/09-25-ppt-ui3-scene-and-page-sync` | 当前任务正确；加入本记录后 implement/check 各 14 项，均通过。大 spec 超过注入 32 KiB 的已有截断警告，实施时单独阅读相关章节。 |
| ARM64 原生 VS MSBuild `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false` | **exit 0**。同一 PowerShell 中先 `Remove-Item Env:PATH` 并设 `MSBUILDDISABLENODEREUSE=1`；由 vswhere 找 ARM64 MSBuild。首次整合、审阅修改及持笔退出测试后的最终全 Solution 均通过；既有 hashlib++/配置转换 warnings，0 error。最终完整日志：`Build/Validation/ppt-ui3-supplement-20260925/build-held-exit.log`。 |
| ARM64 原生 VS MSBuild `inkStrokeModelerTest.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1` | **exit 0**（UInk 实施代理执行）；两个目标与 VS/PS/UpdateCS/EmitCS 四个 Shader 均编译。最初并行 `/m` 遇到 CL.exe exit 4、无源码诊断，串行完整重跑通过。 |
| `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | **exit 0**；含 descriptor/end target、PageControl -1/N 提交、既有 UI3/缩放/Bar/Draw3 回归。最终日志：`Build/Validation/ppt-ui3-supplement-20260925/headless-final.log`。 |
| `PptCOM.Tests/bin/Release/PptCOM.Tests.exe` | **exit 0**；State 5 冷启动拓扑、失败/忙碌、temporary 释放。日志：`Build/Validation/ppt-ui3-supplement-20260925/managed.log`。 |
| `ARM64/Debug/inkStrokeModelerTestTests.exe --uink-presentation-only` | **exit 0**；实际 Encode→Decode→严格导入、旧文件、重复/坏 marker、增删重排、不同 pageGuid/内容。日志：`Build/Validation/ppt-ui3-supplement-20260925/uink-focused.log`。 |
| `ARM64/Debug/inkStrokeModelerTestTests.exe`，隐藏进程，沙箱外 | **exit 0**；完整 UInk/Desktop/Presentation/ThinGPU 套件通过，测试中的预期注入失败仍会写 stderr。stdout：`Build/Validation/ppt-ui3-supplement-20260925/uink-full-escalated.out.log` / stderr：`Build/Validation/ppt-ui3-supplement-20260925/uink-full-escalated.err.log`。 |
| `Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test`，`Start-Process -WindowStyle Hidden`，沙箱外 | **exit 0**；两种 presenter 的真实 Host + Window Service 表面交接/主 Drawpad capture 释放断言，以及末页 A→结束页 B→返回、结束页 Clear/Undo/Redo、排空保存→UInk 内容/pageGuid→重启直接进入结束页冷读→SlideID 重排均通过。最后一轮还覆盖结束页持笔→Desktop Selection→旧物理 Move/Up 不污染桌面。最终 stderr：`Build/Validation/ppt-ui3-supplement-20260925/hidden-held-exit.err.log`。 |
| `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test`，隐藏窗口 | **exit 0**；`PageControlScene failures=0`。最终 stderr：`Build/Validation/ppt-ui3-supplement-20260925/offscreen-final.err.log`。 |
| `Scripts/i18n.ps1 check` | **exit 0**；en-US/zh-TW 均 330/330。日志：`Build/Validation/ppt-ui3-supplement-20260925/i18n.log`。 |
| `git diff --check` | **exit 0**。原文件 BOM/CRLF 保留；未触碰旧 Draw2 与 PptCOM ROT/Application 绑定或 typed HWND 修复。 |

## 沙箱文件提交诊断

在普通沙箱内，隔离测试目录中首次 UInk 文件创建成功，但后续 `SaveUInkFile` 的 `ReplaceFileW` 固定返回 `UInkSaveStatus::IoError`、Win32 `ERROR_ACCESS_DENIED (5)`；隐藏 Host 因而不能证明磁盘冷恢复，完整 UInk 测试也在 index commit I/O 失败后停滞。使用同一编译产物、同一隐藏入口在获准的沙箱外重跑，两项均 exit 0。该失败属于此执行环境的文件替换权限边界，不通过修改 UInk 保存算法或禁用校验绕过。测试目录位于 `Build/ARM64/Debug/Draw3HiddenPptPersistence/`，没有使用用户真实保存根。

## 尚需人工验收

1. 在 PowerPoint 和 WPS 分别展示正常页、有墨迹页、真实 State 5 结束黑页；按 B/白屏/暂时 busy 时不应创建结束页。可从结束页先启动 Inkeys，验证独立空页、书写、撤销/重做/清屏、退出重入恢复。
2. 在桌面下层打开可记录鼠标 Down/Up/Wheel 和触摸的测试窗口；分别用 Esc、主栏确认、PageControl 结束动作退出 PPT，验证系统实际将事件投递给下层窗口，而非只看 `IsWindowVisible` 或手工 `PostMessage`。同时观察 `INKEYS_PPT_EXIT_TRACE=1` 的各 HWND/capture/ready 状态；空桌面及有墨迹桌面均测，取消确认不触发退出。
3. 实体笔在切页/结束页/退出时保持按下，物理抬起后再切工具，确认旧 contact 不跨页、不粘笔；快速退出重开、显示器/DPI/任务栏与白板覆盖后重试。D3D Debug Layer 的实际输出及人工基础绘制/prediction/resize 未采集。该矩阵在无可见 GUI/Office 控制权限的自动测试中为 **NOT VERIFIED**。

任务保持 `in_progress`，没有归档或声称 Office 端到端通过。当前对话未授权本次补充提交 commit，也没有 push。
