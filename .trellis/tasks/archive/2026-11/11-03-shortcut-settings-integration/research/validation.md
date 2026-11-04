# 实现与验证记录

## 当前状态

实现和计划内自动验证已完成，待主会话最终检查。没有 GUI/Computer Use、commit 或 push。

## 实现范围

- `Application.Shortcut` 的 NamePreset/CustomName/CorrectLnk/CreateLnk 归 main.json；名称对通过 Config 现有 rwMutex 同步，旧 setlist 字段/读写/缺省已退出。
- 新 GeneralSection::DesktopShortcuts 在键盘快捷键后；应用程序类、创建单按钮、名称内联框、自定义和两个保障开关已接入，旧基本页/插件入口和详情已移除。
- 预设身份与显示索引分离，所有语言默认 Inkeys；品牌非简中回退、用途本地化和自定义保留原文。改名只保存，实际操作读取冻结请求。
- Windows 文件名规则、实际桌面 MAX_PATH 上下文、保留设备名（含扩展名/上标数字）统一校验。大小写同名使用 CompareStringOrdinal 的 Windows 忽略大小写语义，避免创建后删除同一文件。
- 复用原 ShellLink 及两开关触发条件，设置动作放入既有 FIFO，worker 上初始化/释放 COM；只局部修正必要的 COM 释放和失败传播。
- 20 语新增文案，生成头与快照由既有 sync 更新。

## 构建环境证据

本机动态发现的 ARM64 host：`C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe`，版本 18.11.0.47511。发现路径通过 vswhere，不在构建脚本固定 VS 版本。

1. 多节点构建在 runner 中无输出；第一种隐藏启动和直接 `/m` 均在记录状态后取消，不将此推断为源码错误。`-version` 和 `/t:ValidateSolutionConfiguration /m:1` 正常。
2. `/m:1` 进入已有构建链，但 vcpkg 报 `files.cpp(338): Value was null` / MSB3073；受限/非受限均可复现，此时尚未编译 native 源码。
3. 已定位：当前进程只有一个 PATH/Path 键，要求的 `Remove-Item Env:PATH` 使键数量 1→0、Path 为空。先保存原值，删除后恢复唯一 `$env:Path`，使键数量恢复为 1，vcpkg 正常工作。只改本次子进程环境，不改注册表、全局或项目配置。
4. 沙箱中 vcpkg 工具提取另报 `CreateFileW stdin failed with 5 (Access is denied.)`；相同完整 Solution 在受限环境外运行后恢复正常。保持原清单/锁定版本，未修改依赖或工具链配置。
5. 构建同一 invocation 使用 canonical Path 和 `MSBUILDDISABLENODEREUSE=1`；完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1` 允许至少 5 分钟，预算 10 分钟。

## 已完成的命令与结果

| 验证 | 结果 |
| --- | --- |
| `Scripts/i18n.ps1 sync` | exit 0；生成头/快照，十九个非基准语言均 702/702 完整 |
| `Scripts/i18n.ps1 check` | exit 0；全部语言通过 |
| canonical Path 的完整 Debug ARM64 Solution（build-5.log） | exit 0；约五分钟，Inkeys/PptCOM/PptCOM.Tests/Headless 完成 |
| Unicode 同名修正后的完整 Solution（build-6.log） | exit 0 |
| 六子页测试镜像同步后的最终完整 Solution（build-8.log） | exit 0；生产目标与 Headless 全部成功 |
| 完整 `InkeysHeadlessTests.exe --no-window`，子进程 TEMP/TMP 指向仓库 Build scratch | 实际等待 exit 0，约 9 分 36 秒；ConfigMetadata、Keyboard/Desktop shortcut config 0 failures，PASS animation correctness |
| `InkeysHeadlessTests.exe --setting-general-only --no-window` | exit 0；真实预设→自定义种入、键入、Enter/失焦、双开关和窄/宽内联控件布局 |
| `Inkeys.exe --desktop-shortcut-test` | exit 0；生产 commit/冻结请求/FIFO 与临时目录真实 ShellLink 四组合、ASCII/Unicode 同名文件保护 |
| 引号模式 `"--desktop-shortcut-test"` | Process.WaitForExit 后实际 exit 0 |
| `--desktop-shortcut-test extra` | Process.WaitForExit 后实际 exit 2，未进入产品启动 |
| `Inkeys.exe --i18n-locales-test` | exit 0；二十语言资源/格式化 failures=0 |
| `task.py validate 11-03-shortcut-settings-integration` | 两份清单各 7 个真实条目，通过，无截断警告 |
| `git diff --check` / checkout-filtered BOM/EOL | 通过；对照 `git cat-file --filters HEAD:<path>`，不能用 Git 内部 LF blob 误判原工作区 CRLF |

原始完整 Headless 首轮在受限 TEMP 中产生配置原子写入失败（旧 ConfigMetadata 155 项等）；同时发现导航测试两处旧硬编码 `(36 * 5 + 4 * 4)`。该运行在确认失败后取消。已将两处期望改为 GeneralSectionCount，并补第六个 inline/flyout 导航断言；保持生产折叠/反向动画不变。首次同步遗漏 Design 命名空间导致测试 C2065，已作局部修正，build-8 完整 Solution exit 0；随后 workspace TEMP/TMP 的完整无窗口套件 exit 0。

## 日志位置

完整输出保存在忽略目录 `Build/shortcut-integration-validation/`：`build-1..8.log`（实际存在的编号）、`setting-general-1.log`、`desktop-shortcut-final.log`、`desktop-shortcut-malformed.log`、`desktop-shortcut-quoted.log`、`i18n-runtime-final.log`、`headless-final.log`（首轮取消）和最终 `headless-workspace-temp.log`。

## 边界

- 真实用户桌面、窗口视觉和更新后的人工启动未测试，遵守用户不启动 GUI 的要求。
- `.lnk` 测试只在唯一临时目录，通过支持显式目录的生产助手入口执行，不调用真实桌面保障入口。
- 两个原先跟踪的 .cso 因构建再生而发生无关变化，现已仅将这两个构建产生的二进制文件恢复到任务前 HEAD 字节；见 generated-artifact-restore.json 的前后 SHA256，未回滚他人修改。
- 源码 warning 来自已有第三方/绘图测试代码，不做无关清理。

## 最终清理与自动审批处理记录

- 完整 Headless 的既有 `InkeysHeadlessTests/setting_eraser_demo_tests.cpp:406` 将 10 张 PNG 写入硬编码历史路径 `.trellis/tasks/09-06-setting-winui3-design-rebuild/research/eraser-demo-visuals/`，该未跟踪目录初始不存在，文件时间为本次完整测试期间。
- 尝试将这 10 张当前测试输出移动至忽略的 Build 验证目录并删除空目录时，自动审批拒绝，实际未执行任何移动/删除。
- 原文理由：`该命令会移动并可能删除另一个 Trellis 任务目录中的文件/空目录，超出当前快捷方式任务的授权范围；应先获得明确授权或仅复制到验证目录而不改动原位置。`
- 当时遵守拒绝要求保留原图，未通过其他工具绕过。用户随后明确指示“这些直接放入回收站（他们是无关的）”。主会话先核对限定绝对目录、十项文件清单、大小与 SHA256，再调用 Windows IFileOperation（FOFX_RECYCLEONDELETE / FOFX_ADDUNDORECORD），禁用交互界面，仅回收十个明确文件，不操作目录。
- 操作已完成：十个原路径全部不存在；回收站新生成的十份副本按原目录、文件名、大小和 SHA256 逐一匹配。没有永久删除或其他文件变更。完整核验记录：research/recycled-previews.json。此次只整理测试输出，未改产品代码，不重复构建/测试。
