# 设计研究与代码证据

研究日期：2026-09-06。代码基线：`fluent` / `b67c4f9c`。本轮没有启动 Inkeys 原生窗口；代码中已确认的问题与用户报告的视觉问题分别记录。预览截图来自独立审查稿，不是产品已修复截图。

## 参考材料

1. 用户图 1/2：Windows 设置的浅色层级、标题/分组/设置行、文字左对齐与控件右对齐。原截图 2880×1776，不能把截图像素直接当作 DIP。
2. 用户图 3：桌面应用收起导航的稳定图标轴、内容面板与清晰层级。只参考结构；本轮不实现它的深色配色。
3. [Windows typography](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/typography)：Body 14/20、Caption 12/16、Subtitle 20/28、Title 28/36；单位是 effective pixels。官方偏好 Regular/Semibold，本仓库中文仅 Regular/Bold，不把 Bold 冒称 Semibold。
4. [Content layout and spacing](https://learn.microsoft.com/en-us/windows/apps/design/basics/content-basics)：按钮间距 8、表面文本 inset 16、控件右对齐、控件与展开按钮间距 16。页面较早，但这些布局规则仍是当前官方页面的内容。
5. [NavigationView](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/navigationview)：区分展开、紧凑、覆盖和最小模式；LeftCompact 打开时覆盖内容。通过用户指定 Browser 实际加载并读取该页，其他规范用官方网页查询补充核对。
6. [WinUI Gallery](https://github.com/microsoft/WinUI-Gallery)：官方 NavigationView 文档给出的图库参考入口。本轮没有运行 Gallery，也不声称已逐项复刻 Gallery 源码。

以上是设计依据；下述 248 DIP 侧栏、900 DIP 阈值、72 DIP 卡片最小高度、内容最大宽度 1040 等是本项目的提案，不是微软强制尺寸。

## 已确认的现状

| 证据 | 发现 | 后果 / 推断 |
| --- | --- | --- |
| `Setting.Widgets.cpp:87–118` | `trailingWidth=clamp(availableWidth*0.34,120,240)`，随后 cursor 设置到 trailing 列左端 | 短按钮、开关不会自动靠右；必须按实际 action 宽度分配/对齐 |
| `Setting.Widgets.cpp:115–154` | 文本先绘制，`textBottom` 来自 ImGui cursor；用统一 `ControlHeight` 估计控件高 | 图标、文字和不同实际高度控件缺乏共同测量合同，容易出现不一致的纵向中心 |
| `Setting.Widgets.cpp:65–163` | 480 宽阈值决定 stacked，Begin/End API 不知道 action 的实际尺寸和复合布局 | 窄窗与长译文只能靠固定列比例猜布局；后续应由内容预算触发重排 |
| `imfluent.cpp:203–238` | 默认导航宽 320/48，padding 4，NavItemHeight 36 | 960 DIP 窗口中 320 导航过度占用内容；不能直接把 demo 默认值当设置页规范 |
| `imfluent.cpp:3651–3706` | 导航使用 AnimateFloat 改变宽度，按钮目标在 Open/Compact 间切换，汉堡中心使用 SpacingXXLarge | 现有库的 mode 与项目的窗口断点状态耦合；本轮只设计最终位置和行为，动画不纳入验收 |
| `Setting.cpp:2052–2066` | 自动 layout 改变时重置 desktopNavigationMode | 用户手动收起与自动断点未独立保存；跨尺寸行为需要明确状态机 |
| `Setting.cpp:2186–2211` | 自建导航 child、底部分隔与三项操作占位 | 主菜单与底栏应共享 inset，并通过 flex/剩余高度合同保持各自滚动/固定关系 |
| `Setting.cpp:2240–2260` | NavigationView content 内又开 AutoResizeY child，叠加页宽和过渡偏移 | 多层 padding/坐标责任需要收口；不能仅修改一处间距来宣称布局已统一 |
| `Setting.cpp:2266–2324` | 主页为快速链接按钮、作者设置卡、教程图片 | 已替换旧星空，但信息架构仍像设置列表；可用产品引导区、功能入口与轻量作者区重组 |
| `Setting.cpp:1346–1415` | 内嵌文字以 30 为参考大小载入，绑定 12/14/20/28 等层级；图标以 36/32 合并，offset 10/4 | 字体层级已经存在；问题不能简单归因为“未使用字体层级”。字面比例、行高、基线、图标度量需要分别处理 |

## 字体量化

运行 `python .trellis/tasks/09-06-setting-winui3-design-rebuild/research/font-metrics.py`，读取 TTF head/hhea 表，SC/TC Regular/Bold 四种字体均为：

- unitsPerEm = 1000
- ascent = 928；descent = -285；lineGap = 0
- `(ascent - descent) / unitsPerEm = 1.213`

`imgui_draw.cpp:4667` 默认 STB loader 通过 `stbtt_ScaleForPixelHeight` 换算；`:4670` 乘 `ExtraSizeScale`；`:4703–4707` 计算布局 ascent/descent 时又除掉 ExtraSizeScale；`:4778–4779` 独立处理 glyph offset。`imconfig.h` 中 FreeType 开关为注释，产品工程未查到启用宏。

**由度量与默认 loader 推导**：未额外校准的 14 像素字体高度，对应字库 em 约 `14/1.213 = 11.54` 像素。这能解释偏小，但不是像素截图测量，更不能证明所有控件统一偏移多少。1.213 是换算起点，不是已验收的全局“放大 21.3%”修复值。

后续应使用真实 ImGui/STB 生成离屏字体样张，分别比较 12/14/20/28 与 96/120/144/192 DPI，选择 `字号/实际字面/行高/基线` 的组合。禁止通过每页 SetCursorPosY 随意补偿；也不简单裁掉 descender 空间，必须保留 `g p q y`、繁体、括号和标点。

本稿浏览器按 em 使用仓库字体，展示目标字面；浏览器排版与 ImGui 输出仍需原生验证，不能据此声称原生字体已修复。

## 迁移内容边界清单

| 现有路由 / 起点（Setting.cpp） | 迁移策略 / 必保留行为 |
| --- | --- |
| `tab1` / 2266 | 新主页；保留官网、GitHub、社区、Bilibili、反馈、联系作者与教程入口；不增加全局更新底栏 |
| `Language` / 2328 | 界面语言选择、加载、保存、重启提示；保留简繁英 |
| `tabConfiguration` / 2359 | 清理、画布历史、保存时长；保留两套配置的既有写入关系 |
| `tab6` / 2421 | 软件版本、用户 ID、修复、架构、更新开关/渠道/操作与状态 |
| `tabCICD` / 2670 | 版本页进入的构建详情；不是新增侧栏主入口 |
| `tab2` / 2714 | 自启、快捷方式、缩放、光影、置顶、关闭、兼容性与崩溃策略 |
| `tab3` / 2903 | 设备、绘图优化、智能绘图与原有绘制行为 |
| `tabPreset` / 3054 | 现有预设、颜色、画笔和橡皮相关控件及保存语义 |
| `tab4` / 3147 | 插件总览；状态来自原服务；不虚构新插件系统 |
| `tabPlug2` / 3218 | PPT 联动全部设置；原生/托管写入和运行时同步不得改变 |
| `tabSuperTop` / 3492 | 超级置顶的既有启停和配置 |
| `tabPlug3` / 3523 | 快捷方式管理及异步命令 |
| `tabPlugDDB` / 3553 | DesktopDrawpadBlocker 页的进程/配置/功能关系 |
| `tabComponent` / 3710 | 组件与联动选项，保留条件显示和依赖禁用 |
| `tab5` / 3805 | 当前为快捷键说明 InfoBar；不得趁迁移扩展为完整快捷键编辑器 |
| `tabExperimental` / 3816 | 保留现有实验配置入口；不等于本轮实现设置窗口动画/触摸/脏区 |
| `tab8` / 3923 | 支持开发/赞助内容及现有图片和链接 |
| `tab9` / 3947 | 调试页已有功能、危险操作提示与服务关系 |

顶层有 14 个实际 case，另有 4 个插件详情页面；`tabPerformance` 仅见 enum，不作为“丢失页面”或本轮新功能。实施前将上述清单细化为每个 settingId → 字段 → action → 保存 → 即时同步 → 条件/提示 的覆盖表。

## 提交历史与任务关系

- `791058df`：首次 ImFluent 迁移和源码引入。
- `82928330`：完成设置页视觉迁移，修改 Widgets/Setting/Base 等；继续使用原 switch 和业务状态。
- `5a237ddf`：窗口框架与固定浅色，属于历史基础。
- 原 Trellis 任务规定 ImFluent 补丁仅字体接口/重置、页面优先委托原生控件。本轮提案将“兼容旧卡片签名”改为新的设置行布局 API；确有必要的库侧字体/导航适配应小范围记录 UPSTREAM，不能偷偷建立另一套控件库。
