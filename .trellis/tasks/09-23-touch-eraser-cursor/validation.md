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
