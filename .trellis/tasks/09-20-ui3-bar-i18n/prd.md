# UI3 主栏 i18n 与布局适配

## Goal

将 UI3 主栏及附属面板接入简体中文、繁体中文和英语 i18n，并完成字体与布局适配验证。

## Requirements

- 将 UI3 主栏及其附属面板中的用户可见文字接入 i18n，覆盖固定/动态主栏按钮、绘制属性、几何属性、颜色选择器、提示浮窗、透明背景故障提示及 FPS 调试文字。
- 内置快捷“组件”由 `RegisterBuiltInComponents` 注册的分类名、设置名和按钮短文案不在本次范围；旧 UI2、设置窗口其他页面及 PageControl 也不在范围内。
- 提供 `zh-CN`、`zh-TW`、`en-US` 三套完整翻译；简体中文保持现有含义，繁体中文使用台湾常用表述，英语使用适合固定按钮的短文案。
- 数值型文字通过带占位符的 i18n key 生成，不在渲染代码中拼接可见中文。
- 保持主栏现有 70 DIP 按钮、2×1 文字槽和整体交互尺寸，不扩大整条主栏、不改变按钮网格或换行规则。
- 根据当前语言选择 HarmonyOS Sans SC/TC 字体族与 DWrite locale，并用真实 DWrite 测量驱动局部布局，避免裁切、重叠和越出显示器可用范围。
- 恢复现有语言设置合同 `0=en-US / 1=zh-CN / 2=zh-TW`；启动时不再强制简体中文，非法值仅在内存中回退英语。
- 语言设置仍按现有流程提示重启后完整生效，不新增主栏热切换状态机。
- 返回中文标题的动画辅助接口改为返回语义枚举，由渲染层映射到 i18n key；外部产品 API 与持久化格式不变。
- 按标准流程修改基准 JSONC、同步生成资源，再补齐翻译；不得手工编辑生成的 `I18nKey` 头文件。

## Acceptance Criteria

- [ ] `UI/Bar` 新增按 `MainButtons`、`DrawAttributes`、`GeometryAttributes`、`ColorPicker`、`Diagnostics`、`Errors` 分组的 key，并保留现有 `BottomDock`、`EraserAttributes`。
- [ ] `pwsh ./Scripts/i18n.ps1 check` 对英语与繁体中文的 key、占位符和翻译进度均报告 100%。
- [ ] Bar 生产代码静态审计后，除内置组件文案、日志和注释外不再存在未接入 i18n 的用户可见中文。
- [ ] 三种语言的固定按钮标签在现有文字槽内通过真实 DWrite 宽度检查；笔型、几何和颜色面板无裁切或列重叠。
- [ ] 标注线、粗细溢出提示根据本地化标题/正文重新计算尺寸、关闭按钮预留和脏区；FPS 区域按文本测量扩展并限制在当前显示器范围内。
- [ ] 启动语言遵循保存配置，非法值回退 `en-US`；SC/TC 字体资源与对应 locale 正确配置。
- [ ] 纯逻辑测试验证标注提示的语义分支，不依赖中文字符串。
- [ ] `--bar-eraser-offscreen-test` 覆盖三种语言、固定文字槽、提示浮窗、颜色页脚，并输出多 DPI/UI 缩放离屏 PNG 供核对。
- [ ] `InkeysRepo.sln` 的 `Debug | ARM64` 构建通过，`InkeysHeadlessTests.exe --no-window` 与主程序离屏测试通过。
- [ ] `git diff --check` 通过，既有 `Inkeys/PptCOM.dll` 修改未被覆盖或纳入本任务。

## Notes

- 用户已批准完整实施计划；本任务不自动提交或推送。
