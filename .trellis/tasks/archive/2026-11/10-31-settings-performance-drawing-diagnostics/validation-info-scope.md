# R9 填充图标与信息作用范围

日期：2026-11-01。用户连续澄清的最终范围以PRD R9为准；R8记录是前轮实现历史。

## 已完成

- 条目附注使用填充InfoSolid U+F167，不是E946描边；本地imfluent_icons.h有InfoSolid定义，Segoe Fluent Icons.ttf的cmap确认实际glyph存在。字号、蓝色、占位不变，侧栏和旧Notice图标不全局替换。
- SettingRow若只有note而无普通description，将其归一为普通说明，TextSecondary、无i；只有普通解释外的额外注意才使用i。
- 快捷键原整块Notice和三语言原完整HotKey.E恢复；状态、各子开关和Freeze额外限制保持。
- ClassIsland类级、SuperTop风险、实际架构不匹配、更新通道等原块恢复；目标架构恢复普通说明；通道原完整三语言ChannelTip和无额外说明的下拉框恢复。
- IslandCaller、SecRandom、NamePicker等七个单段前提恢复普通description；DDB单段说明同样处理。具体选项内的重启/风险附注保留。
- PPT子页顶部重复支持说明移除，插件概览支持说明保留；Setup/Admin卡片恢复普通正文、原解决方案按钮/URL及条件，无额外蓝i。
- 顶部更新状态框使用独立Version.StatusTitle（版本状态/版本狀態/Version status），实际“更新偏好”栏目键和标题不改。

## 验证

| 检查 | 结果 |
| --- | --- |
| 完整InkeysRepo.sln Debug / ARM64 | 退出码0，0错误；既有84条警告保留 |
| --setting-attention-only --no-window | 退出码0；真实ImGui/字体/DrawData验证填充glyph、null/空description无i、完整文字、普通说明几何等价和原附注避让 |
| --setting-copy-only --no-window | 退出码0 |
| i18n check | en-US、zh-TW均571/571，通过 |
| 只读复核、BOM/CRLF、git diff --check | 通过，见research/info-scope-review.md |

日志：validation/info-scope/build-arm64.*、attention.*、copy.*。沿用既有ARM64原生MSBuild与PATH规范化，清理已知会出问题的生成Inkeys.ilk缓存；未改工程/依赖/全局配置。

本轮没有启动应用/设置GUI、Computer Use或hidden HWND fixture，也没有创建commit/push/自动归档。没有修改引擎、配置值、权限或业务流程；保留所有前轮未提交功能。未重复运行已知无关HomeWARP基准，不据此宣称其历史问题已解决。
