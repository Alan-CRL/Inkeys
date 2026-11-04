# 白板桌面快捷方式与启动转发 — 实施计划

前置：本任务 prd/design 经用户审阅后才 `task.py start`；实现前先读 `.trellis/spec/native-desktop/{desktop-shortcuts,single-instance-and-startup-handoff,whiteboard,configuration-i18n-and-assets,cpp-conventions}.md` 与 11-03 归档的 `implement.md`/`research/validation.md` 以复用其验证入口。

## 当前实施状态

2026-11-04：步骤1–10已完成，完整Debug ARM64 Solution与9项相关无窗口检查通过；最终图标已获确认并接入。未进行GUI、真实跨用户/UIAccess及更新/SuperTop全链实机验收，相关边界保留。用户已确认改动有效，批准结束、归档与提交；功能提交为 `40ba6efec7767d64b2206079bc157640c062b3c9`，按Trellis流程收尾，未push。证据见 `research/validation.md`、`research/final-review.md`、`research/final-results.json`。

## 步骤

1. **配置与名称解析**：`Other.Config.cppm` 增 `Application.WhiteboardShortcut` 组及专用读写方法；`Business/DesktopShortcut.h` 增种类、白板预设映射、`NamesCollide`；单元式无窗口用例（缺省/非法/往返/冲突/语言回退）。
2. **快捷方式内容与分类保障**：`IdtPlug-in.cpp/.h` 扩展 `CreateShortcut`（参数、图标）、新增 `ClassifyShortcut`，`SetShortcut` 按种类执行，启动入口同时处理两类；`--desktop-shortcut-test` 扩展临时目录真实 ShellLink 用例（含参数/图标回读、两类互不删除、类别不符纠正、大小写同名）。
3. **设置页**：抽出类别块渲染，新增白板块与草稿/事件，移除“快捷方式保障”标题；`Setting.cpp` 事件、业务队列命令加 `kind`；同步 Design 测试镜像与页面高度；无窗口 `InkeysHeadlessTests` 覆盖布局、冲突提示、事件、两类独立开关。
4. **i18n**：新增键 → 基准 zh-CN → `Scripts/i18n.ps1 sync` → `check`；审阅各语言差异。
5. **图标**：设计 `whiteboardLnk.svg`；渲染预览 PNG 放 `research/`，先请用户确认设计，再生成 `whiteboard.ico`，接入 rc/resource.h/vcxproj；校验资源可由 `ExtractIcon`/`SetIconLocation(-ID)` 取到各尺寸。
6. **转发通道**：新增 `Launch/IdtCommandPipe.h`（或并入 `IdtSingleInstance.h` 邻近文件，遵循现有风格）：服务端线程、客户端发送、协议与安全检查、`exiting` 标志；接入 `IdtMain` 准入分支、`LaunchState::openWhiteboard`、关停屏障。
7. **进入白板与意图保留**：就绪点消费 `pendingOpenWhiteboard`；活动白板的恢复前台；`StartReplacement` `extraArguments` 白名单与三处调用；核对 SuperTop 链保留参数。
8. **专项测试**（均无 GUI，私有副本/临时目录）：
   - `Inkeys.exe --single-instance-tests` 扩展：Duplicate+参数转发 Accepted、无参数仍提示退出、允许多开时转发（多实例并存、首个监听者退出后仍可转发）、管道缺失/退出中/超时、坏包/超长/版本不符、错误安装目录客户端被拒、多请求合并、两目录互不干扰、转发路径不触碰 update.json（临时目录放置哨兵文件校验未读写）。
   - 更新相关：待更新+无实例时意图经 `StartReplacement` 保留；有实例时不碰更新文件。
   - UIAccess 跨完整性：私有副本能覆盖的覆盖，其余明确列为人工验收。
9. **质量检查与构建**：`trellis-check`；`git diff` 编码/换行/无关改动检查；完整 `InkeysRepo.sln Debug|ARM64` 构建（动态定位 ARM64 原生 MSBuild，同一 PowerShell 调用先 `Remove-Item Env:PATH`、`MSBUILDDISABLENODEREUSE=1`，超时≥5 分钟）；相关 Headless `--no-window`、`--desktop-shortcut-test`、`--single-instance-tests`、`--shutdown-supervisor-tests --instance-lease-only`、`--config-boundary-test`、i18n 检查全部执行并记录。
10. **规范更新**：更新 `desktop-shortcuts.md`（两类快捷方式、参数、分类保障）与 `single-instance-and-startup-handoff.md`（受限转发通道、与“无 IPC”旧条款的衔接）及 `configuration-i18n-and-assets.md`（新字段、第二图标资源）。

## 验证命令入口

沿用 11-03 `research/validation.md` 与 11-01 `validation.md` 中已验证的命令形态；GUI subsystem 程序必须用 `Start-Process -Wait -PassThru -WindowStyle Hidden` 取真实退出码。未能执行的项（真实桌面、SuperTop/UAC、真实更新替换、视觉）在最终记录中单列。

## 评审门与回滚点

- 门 1：规划审阅（prd/design/implement）→ `task.py start`。
- 门 2：图标预览用户确认后再接入资源。
- 门 3：步骤 6 完成后先跑单实例专项，通过再接入进入白板逻辑。
- 回滚点：步骤 2（分类保障）、步骤 6（通道）可独立回退；配置字段新增无迁移，回退即忽略。
