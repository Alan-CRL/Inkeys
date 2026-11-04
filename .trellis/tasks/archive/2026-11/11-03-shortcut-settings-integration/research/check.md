# Trellis 完整范围检查

## 结论与范围

2026-11-03，按已批准 PRD R1–R9、design.md、implement.md 和 check.jsonl 的七份上下文检查最终工作区。当前没有未修复的任务内缺陷；实现、验证和范围检查可以交付主会话复核。

本检查覆盖设置导航/页面/草稿与提交接线、名称共享逻辑、main.json 与旧配置退出、启动/队列助手、二十语言资源及生成物、全部相关测试和代码规范。没有自行启动 GUI、重复构建或执行已通过的测试。仅新增本报告；产品修复由实现代理完成，规范/任务元数据由主会话维护。

## Findings（已修复）

1. **P2：测试模式可能落入普通产品启动。** 原新 CLI 对完整原始命令行做相等比较，引号模式和额外参数可绕过早返回，且测试体未受 Release Harness 开关保护。现于既有 argv 分支识别模式、严格验证参数数量，并用 INKEYS_ENABLE_UI3_PRESENTATION_HARNESS 编译/执行门控。精确与引号形式实际 exit 0，额外参数实际 exit 2。
2. **P2：恢复后的自定义名称可能超出实际桌面路径预算。** 原配置归一仅检查文件名本身，换到更长桌面路径后会在保障入口直接失败。现启动与设置初始化向 NormalizePreset 传入真实目录长度；不适用的选择回退并保存 Inkeys，保留未采用的自定义原文，未增加桌面文件动作。新增 generic-valid/context-invalid 回归。
3. **P2：Unicode 大小写同名可能删除刚保存的同一快捷方式。** ASCII 忽略大小写修正起初使用依赖 CRT locale 的 _wcsicmp，不能覆盖默认 C locale 下的 ä/Ä。现使用 CompareStringOrdinal(..., TRUE)，真实 ShellLink 用例覆盖 ASCII 和 Unicode 大小写同名。参考：[Microsoft CRT 文档](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/stricmp-wcsicmp-mbsicmp-stricmp-l-wcsicmp-l-mbsicmp-l)。
4. **测试镜像：五个子页的硬编码高度过期。** 两处折叠/反向测试现使用 NavigationRowHeight、RowGap 和 GeneralSectionCount；inline 与 compact flyout 均实际点击第六项并验证 DesktopShortcuts，包含折叠再展开保持所选页。

预设到自定义、键入、回车、失焦和队列冻结原先覆盖不足，现已补足实际控件交互、共享生产提交方法和真实 SettingBusinessQueue 的有界屏障探针。离页/隐藏事件接线通过生产调用点与共用提交方法组合核对，没有声称运行完整可见设置窗口。

## Findings（未修复）

无任务范围内剩余缺陷或待决定产品问题。

完整 Headless 在 `.trellis/tasks/09-06-setting-winui3-design-rebuild/research/eraser-demo-visuals/` 生成的十张橡皮预览 PNG 属于测试输出，不是源码改动；生产来源为 InkeysHeadlessTests/setting_eraser_demo_tests.cpp:406。自动审批最初因该历史目录跨任务范围而拒绝移动/删除，未执行变更。用户随后明确要求直接放入回收站，主会话仅回收清单中十张图片，并核对原路径全部不存在、回收站副本大小/SHA256 全部一致（research/recycled-previews.json）。清理已完成，未改动其他会话内容或永久删除文件。

## 验收对应

| 条目 | 检查结果 |
| --- | --- |
| AC1 / R1 | 新 DesktopShortcuts 位于键盘 Shortcuts 后；旧基本页创建/更多及插件卡片/详情删除，其他插件编号保留；两种导航形态显式覆盖第六项。 |
| AC2 / R2–R3 | 应用程序类为创建单按钮及名称内联卡；自定义输入左、下拉右，真实窄/宽与 DPI 控件测试无重叠；20 语预设符合半角括号/无附加空格。 |
| AC3 / R4 | 实际 popup 选择自定义会填入之前显示的名称；真实键入仅改变草稿，Enter/失焦返回保存事件；名称提交方法仅改配置，不提交 Create/Ensure；离页/Hide 接入同一方法。 |
| AC4 / R5 | 两个开关独立，缺省 true/false；保持原开关触发条件，启动与显式动作共用当前名称；设置文件/COM 操作在 FIFO worker。 |
| AC5 / R6 | Application.Shortcut 四字段归 main.json，字符串由 Config rwMutex 保护；旧字段停止读写并退休键清理；不迁移旧值。 |
| AC6 / R7 | 最终完整 Debug ARM64 Solution 和 Headless 通过；差异限于本任务所需接线、文案、规范与测试，编码换行及 whitespace 检查通过。 |
| AC7 / R8 | ID 与显示索引分离，所有语言默认 Inkeys，品牌只简中；语言变化归一配置但不触发桌面操作，自定义原文不翻译。 |
| AC8 / R9 | Windows 非法名、保留设备名、组件/实际路径长度统一校验；无效草稿不覆盖有效配置，创建不可用；持久化无效选择按上下文回退。 |

## Verification

以下构建/测试由实现代理执行，已核对 research/validation.md 和相关日志；主会话提供实际等待后的退出码，未用日志末行代替退出状态：

| 检查 | 结果 |
| --- | --- |
| 完整 InkeysRepo.sln，Debug / ARM64，原生 ARM64 MSBuild，最终 build-8 | exit 0 |
| 完整 InkeysHeadlessTests.exe --no-window，子进程 TEMP/TMP 为仓库 scratch | 实际 exit 0，约 9 分 36 秒；日志末尾 Desktop shortcut config 0 failures / PASS animation correctness |
| --setting-general-only --no-window | exit 0 |
| Inkeys.exe --desktop-shortcut-test | exit 0；生产提交/冻结 FIFO、真实隔离 ShellLink 四组合及大小写保护 |
| 引号模式与额外参数 | 引号形式 exit 0；额外参数 exit 2，早于产品初始化 |
| i18n sync / check | exit 0，二十语言一致（十九个非基准语言各 702/702） |
| --i18n-locales-test | exit 0，二十语言资源/格式化 failures=0 |
| Trellis 上下文 validate | 两份清单各七条有效引用，无注入截断 |

本检查另行执行的静态校验：

- git -c diff.ignoreSubmodules=all diff --check：通过。
- 40 个已跟踪文本文件对照 git cat-file --filters HEAD:<path>：UTF-8、原 BOM 与原换行类型保持；新业务头/规范为 UTF-8 + CRLF。
- 二十语言的新文案字段均非空；两个用途预设统一 Inkeys(...)，没有全角括号或附加空格。
- check.jsonl 七个文件引用均存在且小于 32768 字节。

## 环境与未执行边界

初始构建/测试问题涉及 runner 多节点启动、PATH 被清空、沙箱工具提取与 TEMP 配置原子写权限；处理仅限构建/测试子进程环境。已和之后的测试镜像命名空间编译错误区分，未修改项目配置、升级依赖或绕过签名。

未进行真实用户桌面文件操作、可见 GUI/Computer Use、实际自动更新后的启动验收，也未额外构建 Release、Win32/x64 或测试旧 Windows。Release 测试入口门控已作静态检查，不外推为这些平台完成运行验证。没有 commit、push 或 archive。
