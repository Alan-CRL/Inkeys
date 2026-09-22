# Technical Design

## I18n resources

- 以 `Inkeys/src/i18n/zh-CN.jsonc` 为基准，在 `UI/Bar` 下补充六个功能分组。
- 运行 `Scripts/i18n.ps1 sync` 生成快照、目标语言骨架和 `IdtI18nKeys.g.h`，再编辑 `zh-TW.jsonc` 与 `en-US.jsonc` 的新增值。
- 固定标签采用短文案；动态数值使用格式占位符并保持三语占位符一致。

## Language and typography

- 删除 `IdtMain.cpp` 的 UI3 强制简体中文赋值；I18n 初始化前将非法 `selectLanguage` 规范为 0，但不写回配置。
- 在设置左侧导航的主页与软件配置之间恢复既有语言按钮，复用 `SettingsUI.Language.N`、原语言图标和 `settingTabEnum::Language`；不复制或改写语言页逻辑。
- 主字体集合同时加载 HarmonyOS Sans SC/TC 的常规和粗体资源。
- `BarUIRendering` 保存当前字体族和 DWrite locale，并在 I18n 已加载后按语言配置：`zh-TW` 使用 TC/`zh-tw`，`zh-CN` 使用 SC/`zh-cn`，`en-US` 使用 SC/`en-us`。
- Bar 文本格式创建和测量统一读取该配置，避免构造期早于 I18n 初始化的问题。

## Rendering and layout

- 主栏网格、命中区域和按钮尺寸保持不变；测试使用 DWrite 对所有固定标签做槽宽约束。
- 笔型菜单、几何按钮和颜色页脚继续沿用既有布局，仅把测量输入替换为本地化文字，并在需要时依据测量结果调整局部间距。
- 提示浮窗的标题、正文、关闭按钮预留和脏区继续从测量结果派生。
- FPS 调试区域宽度改为本地化文本测量值与现有最小宽度的较大值，并钳制到当前显示器/窗口可用宽度。

## Semantic state

- 新增标注提示标题语义枚举；动画辅助逻辑只选择枚举，不返回任何语言字符串。
- 渲染层集中将枚举映射到对应 `I18nKey`，测试直接断言枚举。

## Verification

- i18n 同步与完整性检查。
- 静态确认语言导航入口可达既有语言页，并检查新增一行后仍位于上方导航区的固定分隔线之前。
- Bar 可见字符串静态审计。
- ARM64 solution 构建、无窗口单元测试、三语言离屏渲染测试与 PNG 视觉核对。
- 最终检查 diff、编码/换行噪声以及既有二进制修改隔离。
