# R8 设置注意事项验证

日期：2026-11-01。最新需求为PRD R8；不覆盖前轮验证的历史事实。

## 完成内容

- 激光多指“修改后对新开始的笔画生效”和粒子“当前版本暂不可用”及三语言对应句直接删除，没有转成提示。BETA、无粒子toggle、运行时强制关闭保持。
- SettingRow与七类行封装支持显式可选note，另有InfoText用于分组注意；解释下方蓝E946信息图标、与解释一致的Caption字号、TextPrimary文字。
- 提示参与原响应式宽度/退让动画测高；无note返回旧几何；包含长文、无说明、stacked、分数和高DPI等布局。
- 按settings-attention-inventory.md逐项处理活动设置的重启/启动、清理配置风险、架构/通道风险、PPT权限、SuperTop限制、DDB管理员、组件协议/课表、快捷键例外、触摸面积辅助等。正常说明、模式名称、原教程动作、独立确认与运行状态条件保留。
- 所有已国际化内容同步三语言/Keys/中文快照；原硬编码组件前提只按原语义拆分，不恢复旧WPP等退役条目。

## 最终结果

| 检查 | 结果 | 记录 |
| --- | --- | --- |
| 完整InkeysRepo.sln Debug / ARM64 | 退出码0，0错误，既有85条警告保留 | validation/attention/build-arm64.* |
| --setting-attention-only --no-window | 退出码0；七种DPI、SC/TC/EN、明暗主题、宽窄/stacked、空说明/空note、实际glyph颜色/字号、动作一次及动画避让 | validation/attention/attention.* |
| --setting-copy-only --no-window | 退出码0，文案/卡片专项 | validation/attention/copy.* |
| i18n check | en-US、zh-TW均572/572，通过 | 本轮工具输出 |
| 只读质量复核与编码/diff检查 | 通过，无源码阻塞项 | research/attention-review.md及工具输出 |

两个专项均使用已有Headless程序并带--no-window，注意事项专项新增短路分支，不调用HomeWARP基准。实际ImGui/font/DrawData断言验证提示几何、字号、信息图标和字符完整性，不是仅对常量做镜像断言。

构建复用动态发现的ARM64原生MSBuild、同invocation PATH规范化及已确认的生成Inkeys.ilk清理方式，在正常Windows环境执行；未改工程、依赖、工具链或全局配置。pwsh探测失败按项目既有逻辑回退系统Windows PowerShell完成applocal。

## 验证边界

- 未启动应用/设置GUI、Computer Use或hidden HWND fixture，未做真实桌面观感验收。
- 前轮完整设置专项的HomeWARP等待失败/超时见validation-refinement.md。本轮未修改该渲染/基准代码，也未重复运行该无关耗时套件；本轮通过的是注意事项与文案专项，不能称历史问题已解决。
- 未创建commit、push或自动归档，所有修改保留工作树。
