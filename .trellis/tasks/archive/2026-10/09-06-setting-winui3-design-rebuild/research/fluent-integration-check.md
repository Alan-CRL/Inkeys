# 原 fluent 工作区交付复核（2026-10-08）

本轮仅复核已批准交付从隔离工作区转入原目录；没有修改源码、索引或分支，没有另起构建、运行应用、提交或推送。父任务保持 `in_progress`，两个实施子任务为 `completed`；已读取父任务检查上下文与两个子任务验证记录。

## Findings (fixed)

无。本轮未发现需要修复的转入问题，仅新增本复核记录。

## 核对结果

- 原目录为 `D:/Project/Inkeys/Repo/Inkeys`，当前分支 `fluent`；HEAD 为 `834307a4b21cc4b58d6f34a6572af6f401096218`，MERGE_HEAD 为 `e2c212d4d9cd68367c08363c450d392e9d63bcef`。仍为待审查的未提交合并状态。
- 独立运行 `git diff --cached --ignore-submodules=all --name-status 5dfa2fe547a3225d74f7f57c3d33498da9047c32`，退出 0、无差异；原索引内容与隔离验证 tree 一致。`git ls-files -u` 退出 0、无未解决路径。设置、ImFluent、测试、配置、i18n、PptCOM 与工程文件的冲突标记搜索无命中（rg 退出 1，表示无匹配）。
- 原目录 16 个 Setting 文件，以及 ImFluent 实现和补丁记录、两份设置测试、测试与主工程、solution、两份配置文件及三种语言源，共 28 项实际文件逐字节与已验证隔离版本一致。ImFluent 与两份测试的 SHA256 也与子任务 `hover-final-validation.json` 的最终来源记录一致。
- 新侧栏仍使用固定 Home/Language、独立滚动的 8 个中段条目及固定 Community/Sponsor/Restart/Exit/Debug；中段顺序与批准方案一致，赞助调用强调按钮，退出调用语义红色，调试仅展开时显示无图标 Caption 按钮。分割线、动态固定区域预算、overlay 与稳定页面 ID 沿用已复核版本。页面与导航的退休 Preset/Performance 路由搜索无命中（rg 退出 1）。
- `LerpColorU32` 保持透明度加权 RGB、插值 alpha、普通 RGBA 输出及透明端点处理；`UPSTREAM.md` 本地补丁与隔离验证版本一致。此次没有改回旧悬停算法。
- `Inkeys/IdtMain.h` 当前 3679 字节，SHA256 为 `47868f5a1bad038e8f8f847ab1ce9f01239fd4a7b78a9c124d12b2b8d9ac1c34`，与转入前保护清单及备份完全一致。
- `Inkeys/Cache/fluent-integration-20261008/original-files/` 内 15 个保护文件全部存在，SHA256 均与第一子任务 `research/original-workspace-preservation.json` 一致。包括原 `PptCOM.dll` 及 13 个 `PptCOM.Tests` Release 产物；复核时这 13 个当前测试产物也都与备份逐字节一致。
- 生产 `Inkeys/PptCOM.dll` 已采用整合后的依赖，原版完整保存在上述备份中。完整构建可能重编生产依赖或测试输出；其正常产物变化不属于转入遗漏，原测试输出在构建结束后的最终恢复和复核由主会话负责。

## Findings (not fixed)

- 常规 `git status` 读取 `.git/modules/HiMsg` 时遇到本机子模块目录权限限制。用 `GIT_OPTIONAL_LOCKS=0` 和 `--ignore-submodules=all` 的主仓库只读检查退出 0；本轮不修改子模块权限、索引或 Git 配置，未验证子模块内部状态。
- 原目录完整 ARM64 Solution 构建在本轮复核时仍由主会话运行，其最终退出码、原目录产物的 `--no-window` 及 i18n 结论由父任务后续 `research/fluent-integration.md` 记录。本轮不重复构建，也不将隔离工作区通过的结果当作原目录重编已通过。
- 真实鼠标悬停、截图、DWM/DX11 呈现、实际窗口缩放与最大化后恢复等 GUI 验收仍待授权执行。此前已登记的极小工作区可达性、原有同步 LnkHelper IO 等父任务验收债项未因转入获得额外验证。

## Verification

- Lint / 静态完整性：通过。主仓库索引与验证 tree 对照、未解决路径及活动设置冲突标记/退休路由检查通过；本记录另做 UTF-8、CRLF 与尾随空白检查。不清理发布基线原有格式。
- TypeCheck / 编译：本轮不新增编译；隔离版本完整 `Debug | ARM64` Solution 已通过，原目录完整构建的最终证据由主会话收口。
- Tests：本轮不重复运行。转入前最终 `--no-window` 退出 0、i18n 366/366 已有子任务记录；实际来源逐字节一致。原目录重新运行的最终结果由主会话记录。

本记录是静态转入核对，不表示 GUI 已验收，也没有自动创建 commit 或 push。
