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
