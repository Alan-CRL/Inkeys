# Research: 配置、设置和验证入口

- Query: 新缩放键、默认重置、设置页及启动消费者的最小改动范围。
- Scope: internal / external
- Date: 2026-10-19

## 代码证据
- Inkeys/Inkeys/Other/Other.Config.cppm:325：INKEYS_CONFIG_SCHEMA 的 UI.Bar.Zoom 为 IdtAtomic<double>，默认 1.0、NoUpload。同组新增 bool 开关、将 Zoom 改名 Zoom2 可复用 schema 生成的默认/复制/遍历。
- Other.Config.cpp:535 起：ReadDocumentHandler 仅加载 schema 叶子；bool codec 严格要求 isBool。ReadAll/ReadMini 必须共同覆盖新路径。
- Other.Config.cpp:742–805：Write 在 AutoClean=false 时保留未知键再 OverlayDocument。旧 Zoom 可保留为惰性未知数据，但不能再读取或更新；无需为重置用户倍率删除其他数据、创建迁移器或清理未知字段。
- Inkeys/Inkeys/UI/Setting/Setting.cpp:1272、3500–3578：BarZoom 会话变量、滑块 0.50..2.00、SetConfigZoom 实时生效，交互结束 QueueConfigWrite。新增开关复用相邻卡片和配置写入队列。
- Setting.cpp:697、3579 起：settingGlobalScale 来自传统 setlist，设置界面缩放独立。保持其字段与使用链。
- Inkeys/IdtMain.cpp:2267–2271、2327：启动 ReadMini 与 previewOptions.barZoom 均直接读取旧 Zoom，必须换新键并读取新开关；缓存预览宽度仍为逻辑 DIP。
- Inkeys/Inkeys/UI/Bar/Bar.Zoom.cppm:157–204：Initialize、FitInitialAfterMainBarLayout 均读写 Zoom，后者仅在默认倍率且启动空间不足时下调倍率。保留该既有空间保护；100% 等高以未发生 fit 的标准布局为验收前提。
- InkeysHeadlessTests/config_metadata_tests.cpp、startup_preview_state_tests.cpp：已有真实配置读写、Mini、Reset、复制等覆盖，旧字段引用随新键同步；增加明确旧键不迁移案例。
- Inkeys/IdtMain.cpp:1424：--config-boundary-test 位于启动 GUI 之前，可复用；InkeysHeadlessTests 的 --no-window 是默认验证入口。
- Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc：BarZoom 与 SettingUIScale 位于 SettingsUI.Regular.Appearance；Scripts/i18n.ps1 sync/check 维护 IdtI18nKeys.g.h 与基准 snapshot。

## 外部接口依据与限制
- Microsoft ABM_GETTASKBARPOS：返回 Windows 任务栏矩形，不能把工作区差值当成仅任务栏厚度，其他 appbar 也会改变工作区。https://learn.microsoft.com/en-us/windows/win32/shell/abm-gettaskbarpos
- Microsoft SHAppBarMessage / ABM_GETAUTOHIDEBAREX：后者支持按 monitor 查询自动隐藏 appbar，不能保证所有第三方任务栏实现。https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shappbarmessage
- Explorer 的 Shell_TrayWnd / Shell_SecondaryTrayWnd 枚举只能作为兼容采样策略，不能称为微软公开保证的逐屏任务栏合同；需要严格矩形/monitor/厚度校验并支持失败回退。

## 相关规范
native-desktop/configuration-i18n-and-assets.md、build-and-compatibility.md、rendering-and-ui.md；ppt-interop/native-session-ui3.md。

## 环境及未验证项
- 默认 python.exe 为不可运行的 WindowsApps alias；已使用本地 bundled Python 执行 Trellis 脚本。
- 项目 .codex/skills 为空，未找到 trellis-start / trellis-brainstorm；以现存 .trellis/workflow.md 的规划流程为依据，不安装或改造 Trellis。
- git status 默认递归子模块在 HiMsg 遇访问拒绝；--ignore-submodules=all 可检查本任务主仓库变更。没有改动子模块。
- 当前未实际执行 Shell 查询/GUI，自动隐藏及第三方 Shell 的矩形表现需要后续真实机器证据，算法测试不替代这些证据。
