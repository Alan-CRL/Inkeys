# 快捷方式整合：现状与规划证据

## 范围与状态

- 此次现状调查发生在规划阶段；用户随后于 2026-11-03 明确批准完整规划并要求开始实现。
- 规划调查时仅修改任务文档；获批后任务已转为 `in_progress`，当前行为以 PRD/design/实现记录为准。
- 初始工作区主仓库无修改（`git status --short --ignore-submodules`）；普通 `git status` 因 HiMsg 子模块元数据访问限制失败，不需要改变 Git 配置。
- `python` 命令命中不可访问的 WindowsApps 占位符；使用现有 `C:/Program Files/KiCad/10.0/bin/python.exe` 成功执行 Trellis 脚本，不修改环境或项目配置。

## 设置导航与旧入口

- `Inkeys/Inkeys/UI/Setting/Setting.Design.h:74`：`GeneralSection` 当前为 Basic、Appearance、Interaction、Storage、Shortcuts，其中 Shortcuts 指键盘快捷键；新增文件快捷方式必须使用不同枚举标识，不混用。
- `Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp:165`：子页文案/图标数组受 `GeneralSectionCount` 静态断言约束；新增页应同步导航数组及高度、滚动目标计算。
- `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:1034`：基本页创建快捷方式目前为“创建”和“更多选项”两个按钮。新页只保留用户要求的创建按钮，旧助手入口随整合退出。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:3500`：手动创建取得桌面路径，使用 `Widget.LnkName` 冻结完整 .lnk 路径及当前 exe，投递 `SettingBusinessKind::CreateShortcut`。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:1122`：FIFO worker 负责检查/调用 shell-link helper；新页继续通过该业务队列执行实际文件操作，不能移到共享渲染线程。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:3988`：插件总览含 shortcut-helper 导航卡；`Setting.cpp:4024` 为详情页，两者均属合并范围。

## 现有保障语义

- `Inkeys/IdtConfiguration.h:140`：旧 `setlist.shortcutAssistant` 包含 `correctLnk` 和 `createLnk`。
- `Inkeys/IdtMain.cpp:2994`：旧缺省 `correctLnk=true`、`createLnk=false`。
- `Inkeys/IdtConfiguration.cpp:392`：从 `opt/deploy.json` 的 `PlugIn.ShortcutAssistant` 读取；`:625` 写回。新需求明确不读取这些值进行迁移。
- `Inkeys/IdtMain.cpp:3051`：`config.ReadAll()` 早于启动助手调用 `:3245`；可从 main.json 读取新配置后保障。
- `Inkeys/IdtPlug-in.cpp:1896`：correctLnk 开启时，修正当前标准名称的错误目标，并扫描桌面，将指向当前 exe 的其他名称 .lnk 删除，再创建标准名称。
- `Inkeys/IdtPlug-in.cpp:1927`：createLnk 开启时，当前标准名称缺失或目标不符则创建。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:4037`：原修正开关开启后立即执行保障；`:4048` 原补建开关发生变化且 correctLnk=true 时也立即执行。用户已决定保留两个开关；已有开关即时副作用可沿用；用户已确认名称变化仅保存，创建或下次启动保障时再使用。
- 当前助手始终使用 `Widget.LnkName`，必须改为与手动创建共用用户当前名称；当前 zh-CN 为长名称 `智绘教Inkeys（屏幕批注工具）`（`Inkeys/src/i18n/zh-CN.jsonc:7`），不能直接复用作新短预设。

## 内联布局与配置/i18n

- `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:679`：手掌预览 `BeginCard` 提供内联区域的视觉参考；`:702` 直接放置 ComboBox。
- `Inkeys/Inkeys/Other/Other.Config.cppm:370`：`INKEYS_CONFIG_SCHEMA` 是 main.json 字段定义入口。具体字段和预设持久化身份待规划定案；双开关沿用原默认值。
- 预设应持久化稳定身份，并在当前语言下生成名称；自定义必须持久化用户文本。用户已确认所有语言缺省 Inkeys，非简中隐藏品牌专用选项；已选品牌项切换到其他语言时回退 Inkeys；用途预设随语言翻译，自定义文本不变。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:303`：zh-CN 为基准，`Scripts/i18n.ps1 sync/check` 同步各语言、快照及生成头；规划阶段不运行 sync。
- 已使用 trellis-session-insight 做本项目只读搜索；历史对话包含“简中用智绘教Inkeys，其余用Inkeys”的品牌决定，本次用户已再次明确品牌预设只在简中出现，因此无需再问品牌范围。
- 相关历史任务 `.trellis/tasks/archive/2026-10/09-06-setting-winui3-design-rebuild/research/full-page-binding-audit.md:152` 记录了快捷方式原双开关与即时副作用；其设计要求副作用继续使用 FIFO。

## 最终规划审阅项

名称生效时机、Windows 文件名校验、非法输入就地提示/禁创建及有效提交时机均已随完整规划获得用户批准。

## 本轮已确认

- 保留原来的修正/补建两个独立开关，不合并。
- 保持旧缺省修正开启、补建关闭；此处不再存在合并后默认值的问题。
- 后续产品澄清通过会话提问控件询问用户。
- 所有语言默认 Inkeys；用途预设随语言翻译，自定义原文不变；已选简中品牌项切至其他语言时回退 Inkeys。

## 后续验证边界

- 用真实名称解析、配置编解码和临时目录 .lnk 文件验证必要行为，不触碰真实用户桌面。
- 设置布局和路由复用现有 `InkeysHeadlessTests`，通过 `--no-window` 保持无 GUI。
- 产品编译遵守 `InkeysRepo.sln`、`Debug|ARM64`、动态发现 ARM64 原生 MSBuild、同一 PowerShell 调用处理 PATH、至少 5 分钟超时。
- 规划阶段不编译；获批实现后按上述入口进行构建/无窗口验证，仍不启动产品窗口。

## 名称输入与动作边界补充

- `Inkeys/Inkeys/UI/Setting/Setting.Controls.h:103` 已有 std::string 版本 TextBox，可复用现有输入控件，无需引入新控件库。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:1122` 手动创建仅检查指定名称的目标，没有清理其他名称快捷方式；用户已确认只保存名称，旧名称由后续启用的修正保障处理。

## 配置字符串同步证据

- `Inkeys/IdtAtomic.h:11` 限制为平凡且保证无锁的类型，不能承载 std::wstring。
- `Inkeys/Inkeys/Other/Other.Config.cpp:240` 提供 std::wstring JSON codec；`:278` 为 IdtAtomic codec。
- `Inkeys/Inkeys/Other/Other.Config.cppm:549` 的私有 rwMutex 由 Config 复制使用（`:586`）。新增可变 CustomName 应通过受此锁保护的专用方法读写，复用快照锁而非新增字符串原子包装。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:1283` 在生产者复制完整 Config；`:1303` 的 QueueConfigWrite 仅保证快照入队顺序，不能代替字符串本身的同步。
