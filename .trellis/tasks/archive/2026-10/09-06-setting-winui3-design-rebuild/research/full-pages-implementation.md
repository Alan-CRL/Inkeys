# 全页迁移实现与静态复核

本记录描述阶段二代码，构建及真实窗口验收由主会话统一执行并另行记录，不能据此认定全部视觉验收已完成。

## 实现文件

- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：所有剩余实际页面采用 `Design` 公共组件；原业务仍处于 coroutine 的 FIFO 宏作用域。移除仅 Home/General 绑定新字体的分支，全部内容页使用同一套校准字体；离开内容页恢复标题栏字体。旧页的入场位移/透明度不再参与新页面绘制，没有新增动画。插件各详情用稳定子页 ID 保存独立滚动与展开状态。
- `Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp`、`.cppm`：删除旧 PageHeader/SectionHeader/BeginSettingsCard/EndSettingsCard 与 Combo 包装；固定 34% 尾部列退出生产。原标题栏、颜色、DIP 与全局样式辅助仍保留。

## 页面覆盖

| 视图 | 统一呈现 | 保留的特殊内容/交互 |
| --- | --- | --- |
| 主页、常规 | 延用阶段一 Design Pages | 已接线入口、教程、常规全部配置 |
| 语言 | Page/Section/ComboRow | 写配置→即时载入语言→新语言重启提示 |
| 配置保存 | ToggleRow/ComboRow | AutoClean双配置写入；保留时长在保存关闭时仍可用 |
| 软件版本 | Card/Text/Notice/ButtonRow/ToggleRow/ComboRow | 图片保原比例，宽卡与版本说明并排；用户ID剪贴板、修复与UpdateRestart专用FIFO分支 |
| 构建详情 | 父页导航、NavigationRow、Card/Text | 两个长URL完整换行且可点击；全部构建字段保留 |
| 绘制 | ToggleRow/ComboRow/SliderIntRow | 设备派生值即时更新；预备画布0..20，松手写入并ResetPrepareCanvas |
| 预设 | ToggleRow/SliderRow | 自动粗细隐藏两条手动项；1..30/10..100，取整且松手保存 |
| 插件总览 | 4个NavigationRow | 状态和完整功能说明，整行点击进入原4详情 |
| PPT联动 | Notice/ToggleRow/ButtonRow及专用复合SettingRow | 错误/管理员警告、0.5..3.0缩放+重置+临时同步按钮；实时Notify、全体slider松手后按Record写入；原三组同步优先级不变 |
| 超级置顶 | Notice/ToggleRow | hasSuperTop只决定说明，不新增禁用 |
| 快捷方式 | ToggleRow | 原SetShortcut即时副作用保持FIFO重定向 |
| DDB | Notice/ToggleRow/ComboRow | 总开关ConfigureDdb、管理员RestartDdb、间隔及17项控制的双写/交互通知保留；原禁用行为未扩大 |
| 组件 | Page/Section/ToggleRow/Notice | 原16项配置；变化后WriteSetting+SyncUi3BuiltInComponents |
| 快捷键 | Page/Notice | 原只读说明，无新增编辑器 |
| 实验选项 | Page/Section/ToggleRow/SliderRow | dynamic/FPS条件保留；速度0.1..5.0实时预览并松手保存；不实现新渲染功能 |
| 支持开发 | Page/Card/Text | 原赞助图片、比例和说明 |
| 软件调试 | Page/ButtonRow/Card/Text/ScrollView | 原触摸测试动作和实时状态；滚动条规则由公共容器决定 |

## 静态验证

对照 `full-baseline/Setting.cpp`（`0ffd0341`）逐页阅读并统计，详见 `full-pages-static-audit.json`：

- 配置字段引用共130处，原始与新代码的引用多重集完全相同。
- 关键业务调用共96处、FIFO命令类型6处，原始与新代码的多重集完全相同。
- 被迁移代码范围的18个switch case标签完全相同；全部实际路由保留。该统计包含插件父级/总览结构，不把它误解为新增页面数量。
- 没有删除原i18n key；仅为两处父级返回链接复用已有Version.N/PlugIn.N，无新增翻译源。
- 共32处标准静态卡、10处Notice及插件/DDB/组件动态helper已迁入公共布局；不存在旧BeginSettingsCard/EndSettingsCard/34%布局调用。
- 所有改动的3个产品文件维持UTF-8无BOM与CRLF；`git diff --check`通过。

静态计数用来发现遗漏，不能替代业务语义审阅或实际GUI验证。程序构建、headless回归、页面实际截图与后续反馈应以主会话最终验收记录为准。
