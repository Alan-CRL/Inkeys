# 快捷方式整合实施计划

## Entry Gate and Ownership

用户已于 2026-11-03 明确批准最新完整规划并要求开始实现。主会话启动任务并按 Trellis 工作流派发实现/检查子代理；派发首行必须为 `Active task: .trellis/tasks/11-03-shortcut-settings-integration`，先加载清单，再读 PRD/design/implement。

实现代理负责本任务列出的设置、配置、助手接线与必要测试/i18n；检查代理负责完整任务范围的合同、差异及必要自修复。工作区可能还有其他会话/代理改动，不得覆盖或回滚他人修改；主会话协调产品问题及最终验收。保持单个任务，UI/配置/运行期需共同验收，不拆独立可交付子任务。

## Ordered Checklist

1. [x] 审批后重新读取活动任务、git 状态及当前源码；记录已有改动，检查源码编码/BOM/EOL。加载 implement.jsonl；发现产品范围变化先回规划。
2. [x] 在 Other.Config schema 加入 Application.Shortcut 四字段，保持 ID 与显示索引分离；补名称读取/更新的最小同步方法、缺省和非法值归一；复用现有字符串 codec。
3. [x] 增加共享的名称解析/校验纯逻辑，覆盖全部默认 Inkeys、品牌语言回退、用途 i18n 和自定义输入；不创建新配置系统或无关通用抽象。
4. [x] 修改助手从新配置/冻结请求取名称和两个独立开关，保持原启动位置与保障语义；改名不得触发文件动作。设置即时动作通过既有 FIFO，worker 不重读活动草稿/语言。
5. [x] 增加 GeneralSection::DesktopShortcuts，接好 Shell/转场/滚动；实现应用程序类、创建单按钮、左右名称内联区和两个保障开关。有效自定义按提交时机写盘，非法值提示且创建不可用，离页/关闭不丢已完成有效编辑。
6. [x] 移除旧基本页创建/更多与插件助手入口/详情；停止旧 setlist 读写和缺省，CaptureSettingJson 清理退休键，更新活动测试初始化；不处理无关历史源码。
7. [x] 补各语言的新页/预设/错误文案，运行既有 i18n sync/check 并审查生成头、快照与语言 diff；不把全局品牌文案改动混入本任务。
8. [x] 补必要行为测试并执行下述验证；只检验产品合同和实际调用链，不写镜像实现的测试。
9. [x] Trellis check 复核所有 AC；主会话记录验证结果和适用 spec 更新。本任务没有 commit/push 或 GUI 授权，不自动提交、归档或启动产品窗口。

## Affected Files and Rollback Points

- 设置：`Inkeys/Inkeys/UI/Setting/Setting.Design.h`、`Setting.Shell.cpp`、`Setting.Pages.h/.cpp`、`Setting.cpp`；只在需要跨帧草稿时局部扩展现有 session 状态。
- 配置：`Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`、`Inkeys/IdtConfiguration.h/.cpp`、`Inkeys/IdtMain.cpp`。
- 助手：`Inkeys/IdtPlug-in.h/.cpp`；必要时在 Business 下新增小型名称解析纯头，源文件工程登记仅在确有必要时修改。
- 活动镜像：`Inkeys/Inkeys/UI/Bar/Bar.Presentation.Test.cpp` 的旧助手缺省初始化；设置 Headless、必要配置测试及工程登记。
- 文案：`Inkeys/src/i18n/*.jsonc`、`Scripts/i18n.zh-CN.snapshot.jsonc`、生成的 `Inkeys/IdtI18nKeys.g.h`。
- 核对 diff 按三组回滚边界：配置/业务、页面/路由、i18n/测试。遇到失败定位第一个有意义错误，不重置整个工作区或改 SDK/依赖。

## Behavioral Verification Matrix

| 范围 | 必须验证的结果 |
| --- | --- |
| 配置 | 缺省、合法值、类型错误/未知 ID、非法自定义回退；ReadAll/ReadMini/写回与 Config 快照复制；旧字段不迁移/不双写 |
| 命名 | zh-CN 五项/其他语言四项，ID/索引不串位；半角括号、默认 Inkeys、用途随语言、自定义原文、品牌非简中回退 |
| 编辑 | 预设→自定义种入、重新打开恢复；有效 Enter/失焦/离页保存；非法草稿不写盘且不可创建；任何名称编辑不产生文件动作 |
| 创建/保障 | 实际生产 helper 在临时目录 .lnk 验证目标/名称、两个开关四组合；同名有效幂等、错误目标与缺失、其他名按旧修正规则；不得触碰真实桌面 |
| FIFO/线程 | 入队后修改名称不改变旧请求；配置字符串与全快照复制无并发未保护读取；共享渲染线程不执行 shell/link/桌面扫描 |
| UI/路由 | 新页位置、旧入口消失、窄窗/DPI/长翻译左右不覆盖、自定义显隐、离页/返回、动画关闭及原快捷键页独立正常 |

## Validation Commands

项目现有构建系统是 Visual Studio/MSBuild，复用 InkeysRepo.sln：

1. 迭代前确认现有测试入口。设置局部可用 `Build/ARM64/Debug/InkeysHeadlessTests.exe --setting-home-only --no-window`；配置/命名/文件动作新增覆盖接入现有测试可执行程序或现有早返回调试入口，不另建构建系统。
2. 文案更新后运行 `pwsh -File Scripts/i18n.ps1 sync`、`pwsh -File Scripts/i18n.ps1 check`。规划期不运行会改产品文件的 sync。
3. 动态发现 Visual Studio 安装（vswhere 或当前安装信息），优先 ARM64 原生 MSBuild.exe，不硬编码 VS 版本。完整构建使用 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m`，同一 PowerShell invocation 先执行 `Remove-Item Env:PATH -ErrorAction SilentlyContinue` 及 `$env:MSBUILDDISABLENODEREUSE='1'`，为单次 solution 构建至少允许 5 分钟，建议 10 分钟；等待中可输出进度，不因短时无输出来重启构建。
4. 最终运行 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，有需要再运行现有 `Build/ARM64/Debug/Inkeys.exe --i18n-locales-test`，确认其在产品 UI/配置入口前返回。不得启动默认 Inkeys.exe、设置窗口或浏览器。
5. 检查退出码与首个有意义错误；源码、链接、依赖/环境错误分开报告。环境失败不通过升级工具或改项目配置绕过。
6. `git -c diff.ignoreSubmodules=all diff --check`，检查逐文件 BOM/EOL、新文件尾空格与 diff 范围。当前 HiMsg 子模块元数据读取受限时使用上述局部忽略，不改用户 Git 配置。
7. `python .trellis/scripts/task.py validate 11-03-shortcut-settings-integration` 校验上下文清单。此命令不代表产品构建/运行测试通过。

## Planning Review Checklist

- [x] R1–R8 有用户请求/会话选择依据，R9 依据文件名与有效配置边界补充。
- [x] 已研究导航、旧双开关、启动顺序、配置快照、TextBox 和 i18n 生成链。
- [x] PRD 已按最终结构收敛，没有遗留已回答问题或平行重复要求。
- [x] design.md、implement.md 和 implement/check 上下文已准备。
- [x] 清单验证、文档引用和格式检查通过。
- [x] 向用户展示最新最终规划，并通过会话提问控件获得后续明确批准。
- [x] 完整规划批准已收到，可执行 task.py start。

## Completion Evidence

最终 build-8、完整 workspace TEMP/TMP 的 --no-window、快捷方式/设置/i18n 专项与独立全范围检查均通过；验证与审查记录位于 research/validation.md、research/check.md。主会话已更新 code spec 并以 --no-commit 记录 session 162。历史测试目录的十张生成 PNG 已获用户明确回收授权，并通过原生回收及副本哈希校验；没有剩余实现、验证或清理工作。

## 当前界面修正轮

- [ ] 按 research/ui-layout-followup.md 合并同一卡片、修正下方弹层及双向动效。
- [ ] 更新两处说明（上方去旧入口指引、下方补充启动时机），同步各语言。
- [ ] 实际控件/布局/动画定向测试、完整 ARM64 Solution、i18n 和差异检查。
- [ ] 独立 Trellis check 本轮差异、规范同步及会话记录；不 GUI/commit/push。
