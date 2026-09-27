# 验证记录（2026-09-23）

## 静态与编译

- `git diff --check`：通过。业务源码维持原 UTF-8 BOM + CRLF；测试源码维持原 UTF-8 无 BOM + CRLF。
- 当前 Visual Studio 安装中的 ARM64 原生 MSBuild，`InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1`：通过，`MSBUILD_EXIT_CODE=0`。按仓库规则在同一 PowerShell invocation 规范化 `PATH` 并设置 `MSBUILDDISABLENODEREUSE=1`。
- `InkeysHeadlessTests.exe --no-window`：通过，`HEADLESS_EXIT_CODE=0`，包括新增的 Touch 视觉归属与 Mouse 来源过滤用例。

## 隐藏集成测试与限制

- `Inkeys.exe --draw3-eraser-hidden-test` 用 `Start-Process -Wait -PassThru -WindowStyle Hidden` 运行，退出码 `1`。DComp 与 ULW 两轮各报一次 `drawpad and presentation remain Freeze siblings`、`Host stop leaves hidden Window Service HWND intact`；没有光标断言失败。失败指向窗口 Owner/HWND 生命周期，与本任务改动的光标文件不同，不能据此称隐藏集成测试通过。
- `Inkeys.exe --draw3-hidden-test` 曾输出相同 Owner/HWND 失败，以及 `drawing thread consumes pen dwell Down`、`release Down consumed` 和一次模型输入间隔错误。该次 GUI 子系统启动方式没有可靠取得进程退出码，故只记录输出，不记为通过。
- 未执行交互式 GUI 或真实触摸/笔/鼠标硬件测试。真实设备验收仍需覆盖纯触摸单指/多指擦除与抬起、触摸后真实鼠标/触控板移动、笔悬停/接触，以及 Win7 的 RTS/兼容消息路径。

## Bug 复盘

- 类别：跨层光标合同与测试覆盖缺口。Touch 接触反馈由绘制 runtime 生成，主自绘光标和系统箭头分别读取持久 Pen/Mouse owner；旧代码只清 Mouse 样本，没有让 Touch 暂时接管这两条视觉路径。
- 防回归：在输入规范写明 Touch Up 不恢复旧 Hover，新增纯逻辑断言同时检查自绘主光标、系统箭头和提升 Mouse 消息来源。隐藏 Host 的 Owner/HWND 失败另行处理，不借本任务修改无关窗口层。

## 光标诊断增量验证（2026-09-23）

- 用户复测确认触摸期间仍有额外按下圆环，最后抬起后在原光标位置出现悬停圆环。本阶段只提供诊断开关，未修改显隐与来源归属策略。
- `Scripts/i18n.ps1 sync`、`check`：三种语言均通过。
- ARM64 原生 MSBuild 在本机反复遇到 `C1041`，即使限制编译器并发、加 `/FS` 和重新生成目标 PDB 仍失败；PDB 备份已恢复。随后使用当前 Visual Studio 的 x64 MSBuild 主机构建同一 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，退出码 `0`。
- `InkeysHeadlessTests.exe --no-window`：退出码 `0`。未启动产品 GUI；控制台日志的真实输入时序仍需用户复现后验收。

复现取证：在 Debug 版的“实验选项 → 光标调试信息”开启开关并重启应用，保存从首次 `touch-begin` 到最后抬起后约两秒的 `[CURSOR_TRACE]` 行。请保留 `seq` 和可能出现的 `dropped=` 行；`mouse`、`owner`、`frame`、`visual`、`laser-tip`、`system` 与 `present` 记录可用来区分系统箭头、主光标和逐触点反馈。

## 2026-09-24：本机 ARM64 `C1041` 重新生成故障

- 用户贴出的 VS `Rebuild` 在编译标准库头单元与 `Surface.cpp` 时均报同一个 `Inkeys/Cache/ARM64/Debug/vc143.pdb` 的 `C1041`；没有光标诊断源码错误。
- 发现仍存活的 `HostArm64/arm64/mspdbsrv.exe` 进程，其原父进程已退出；Visual Studio 与它持有的 MSBuild 节点仍在运行。确认没有活动 `cl.exe` 后只终止该 PDB 服务进程，未关闭 IDE、修改全局配置或工程文件。
- 随后用当前 VS 安装中的 ARM64 原生 MSBuild 对 `InkeysRepo.sln` 执行完整 `Debug|ARM64 /t:Rebuild`，退出码 `0`；再次执行增量 Build 也为 `0`。`InkeysHeadlessTests.exe --no-window` 退出码 `0`。
- 这证明重新启动 PDB 服务后编译链恢复；服务此前为何进入异常状态尚未确认。再次出现时先核对首个错误及是否有活动编译，再按具体进程处理。

## 2026-09-24：用户光标日志复盘与原位 Move 过滤

- 三次 Touch 分别在 `seq=215/354/451` 终止；下一绘制帧的 visual 数为 0。触摸期间的可见记录均为单个 `source=touch`，没有 `primary=1` 与活动 Touch 同帧，也没有 `dropped=` 或序号缺口。
- `seq=223/361/458` 各有一条 `WM_MOUSEMOVE`：`source=0`、`promoted=0`、`extra=0`、坐标与最后 Touch 兼容消息一致；此前的 Touch 兼容消息均 `source=4` 且被拒绝。这三条 Move 各自使触摸抑制清零，随后 `seq=226/364/461` 呈现半透明 `source=primary` 圆环。
- 本轮仅增加“触摸抑制仍在、未按键的 MouseMove 来源未知、坐标等于末触点”这一过滤，以及判定边界测试；现有来源/时间屏障优先，RTS 跟踪与控制台记录同一拒绝原因。从窗口外拖入的带按键 Move、真实 Mouse/TouchPad、Pen 和触点圆环生成逻辑保持原路径。
- ARM64 原生 `InkeysRepo.sln Debug|ARM64` Build 退出码 `0`，`InkeysHeadlessTests.exe --no-window` 退出码 `0`，`git diff --check` 通过。真实纯触摸与混合输入设备需用户复测，尤其检查抬起后原位圆环是否消失和真实鼠标移动能否立即恢复。

## 来源与事件链诊断增强（schema=2）

- 用户批准本轮增加取证与消息序列验证，未授权据此宣称修复成功。保留 `!buttonDown` 例外等全部现有接管条件；新增日志只观察状态。
- 来源日志区分 api/ok/error/device/origin/sent，并关联 event；before/filter/decision/after 保留实际处理顺序。末触点记录经 Pointer 或兼容 Mouse 得到的坐标与时间，完整 Pointer ID 不可得时明确记 0。
- Raw Input 仅读取现有 Bar 接收的数据，记录实际注册/注销、失败、设备、移动和按键。已确认落笔会关闭该接收，所以缺失 raw-mouse 不能作为物理鼠标未输入的证据；本轮没有改变注册生命周期或新增接收 HWND。
- 当前 VS 的 ARM64 原生 MSBuild 构建完整 `InkeysRepo.sln Debug|ARM64 /m:1`，设置 600 秒超时，退出码 `0`；`InkeysHeadlessTests.exe --no-window` 退出码 `0`。未启动 GUI 或执行硬件测试。
- `verify_cursor_trace.py --self-test` 的五组测试通过，覆盖按下到悬停的错误链、Mouse/TouchPad/Pen 接管、多指终态、缺字段/丢失/截断和 schema=2 事件配对。
- 真实日志 bf983934... 的诊断链为 seq=123 接管 → 130 双光标呈现 → 267 抬起后按下残留 → 272 悬停残留；旧日志 cac637e8... 的三个接管起点为 seq=223/361/458。检查器均检出，单独分析这些失败日志退出码为 `1`（发现需复核链），不能误记成产品回归通过。
- 脚本退出码：0 表示完整日志中未观察到目标异常链，1 表示检出需复核链，2 表示证据不完整。`complete` 仅代表所需日志序列完整，不代表输入来源已明确或全设备行为正确。脚本不执行真实 WndProc，也不替代设备测试。
- 编码/BOM 和 CRLF 保持原格式；`git diff --check` 通过。本轮不提交，任务继续进行中。

复测方法：开启既有“实验选项 → 光标调试信息”并重启 Debug 版，确认首段出现 `enabled schema=2`。分别记录单指擦除（不碰鼠标）、多指擦除、触摸过程中移动/按下真实鼠标、触摸后笔悬停；保留从启用到最后抬起后数秒的完整输出，包括 raw-registration、dropped 和 truncated 行。

## 2026-09-25：已确认系统注入 Move 的接管修复

- 用户批准基于 schema=2 证据修复。新增生产入口 `FilterMouseCursorMessage`，统一已有来源/时间屏障、已确认系统来源、未知位置回退与按键例外；WindowControl 直接按返回结果早退，日志复用 `system-touch-move` 原因及 `systemReject=1`。
- 唯一新增行为条件为 WM_MOUSEMOVE + Touch 抑制有效 + 来源查询成功 + device=IMDT_UNAVAILABLE + origin=IMO_SYSTEM。该规则先于按键/位置回退，不使用定时器；其余来源及非 Move 消息保留既有路径。
- Headless 增加两组 C++ 测试：完整生产过滤入口的按键态/非按键态、末触点不同/不可得、明确 Mouse/TouchPad、Pen/Touch 兼容消息、查询失败/Win7 回退、应用注入和按钮消息边界；以及 event=51 系统按下态 Move → Touch Up → event=60 迟到系统 Move → 真实 Mouse 的 mailbox/visual 组合验证。
- 初次测试只有新增鼠标恢复透明度断言失败：用例外观漏设实际工具的 0.5 悬停透明度；修正测试样本后重建并重跑，未修改产品透明度逻辑。
- 当前 VS ARM64 原生 MSBuild，完整 `InkeysRepo.sln Debug|ARM64 /m:1`，600 秒超时：最终退出码 0。`InkeysHeadlessTests.exe --no-window` 最终退出码 0。`git diff --check` 及原编码/BOM/CRLF 检查通过；新增代码无相关编译警告，原有第三方转换与 ContactRecord 对齐警告保留。
- 本轮没有 GUI/真实设备测试。组合测试实际调用生产过滤入口、mailbox 和光标解析器，但不执行 WndProc/RTS 硬件分发。多指、Pen+Mouse+Touch 和 Win7 仍由设备复测验收；不将本机 Win8+ 来源判据宣称为 Win7 硬件修复。
- 复测应看到异常系统 Move 为 accepted=0 reason=system-touch-move，after 状态仍 suppressed=1、mouseValid=0；活动触点结束后 primary=0，来源明确的鼠标或新 Pen 输入可以恢复。未创建 commit，任务保持 in_progress。

## 2026-09-25：人工验收与最终日志核对

用户明确确认人工测试已修复，并授权结束任务、commit 与 push。附件 e3f4931f-44ae-43cc-8754-92360fd76573：2923 条连续记录，7 轮触摸，最多 5 个活动触点；无丢失/截断，事件配对完整，日志检查器退出码 0、findings 为空。

seq=744 的系统 Move 被 accepted=0 reason=system-touch-move 拒绝；全部 Touch End 保持 owner=Touch、suppressed=1，未发现 primary 与活动 Touch 同帧。后续接受的 54 条 Mouse 消息来源均为 IMDT_MOUSE。最终退出绘制模式时 visual 为 0。人工验收与日志一致，当前问题结案。

本份日志不含 Pen 样本或 Win7 运行证据，不把本次验收扩大成这两类平台/设备已经实测；相关逻辑边界已由先前无窗口用例覆盖。最终产品源码与上轮构建/测试通过时一致，本轮仅补验收记录并归档，无需重复编译。
