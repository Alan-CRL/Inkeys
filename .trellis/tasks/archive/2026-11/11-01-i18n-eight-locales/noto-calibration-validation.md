# Noto设置字体校准：试用与验证

2026-11-02，基线提交e5f0574e。用户选择日/韩继续使用Noto，Harmony日文候选暂不进行；任务保留进行中，本阶段未提交或归档。

## 已实现

- 产品修改只在 `Setting.Typography.h` 与 `Setting.cpp`，未改主栏/DWrite、MessageBox、字体资产、翻译、图标、语义字号、行框或全局DPI几何。
- 保持Harmony Design既有配置、Legacy Harmony原倍率1。Noto Design按自身hhea使用约1.40456，Legacy约1.193735，仍为真实400/700字重。
- 所有文字源按自身字族校准尺度，合并源基线按primary字族与Legacy/Design角色对齐，避免Noto与Harmony补缺字形在一行上下错位。
- 回归覆盖共同汉字/H/数字的实际ImGui字面上下界、descender完整性、旧Noto跨字号负对照、主/回退的真实缺字基线，以及原十一语布局。

## 试用与对照

重新启动本次 `Build/ARM64/Debug/Inkeys.exe` 构建后，选择日文或韩文；语言变化仍按既有流程重启完整生效。

真实ImGui DX11/WARP离屏图（无HWND），五列依次为中文基准、日文校准前/后、韩文校准前/后：

- [100%](../../../../../Build/setting-fonts/noto-calibration-100.png)
- [150%](../../../../../Build/setting-fonts/noto-calibration-150.png)
- [200%](../../../../../Build/setting-fonts/noto-calibration-200.png)

图中包含共同字样、混合回退、实际PageHeader/SectionHeader/ToggleRow和Legacy caption字样。所有对照均保留真实400/700字重，未试Harmony日文。

## 最终结果

| 验证 | 结果 |
|---|---|
| 完整InkeysRepo.sln Debug\|ARM64，ARM64原生MSBuild | 退出0，最后一次12.88秒 |
| --no-window --setting-general-only | 退出0，10.96秒；三个DPI的校准/混排检查均0失败 |
| 完整InkeysHeadlessTests --no-window | 退出0，358.22秒；stderr空，PASS animation correctness |
| 旧Noto负对照 | 高DPI聚合Design约0.715–0.719、Legacy约0.843–0.846，能抓原始缩小回归 |
| 真实字号/基线 | 校准后通过相对上下界、基线、fallback与descender断言 |
| 源码范围和格式 | 产品/测试5个文件保留UTF-8、原BOM及CRLF；diff检查通过 |

原有类型转换和第三方警告未作无关清理。日志保留于 `.trellis/.runtime/i18n-validation/noto-build-3-*.log`、`settings-*.log`、`headless-*.log`；独立审查见 `research/noto-calibration-review.md`。

## 验证解释

小字号位图quad包含栅格边缘，13px粗体H的9px/10px包络差仅1设备像素。尺寸正向回归保留相对±10%，同时允许绝对≤1物理像素（不随DPI放大）；原始约71%/84%的缩小仍由大字号和跨6字号负对照检测，未通过缩小产品字体或任意提高比例阈值来消除失败。

没有启动交互GUI、操作桌面或做人工实际窗口验收；本记录证明真实ImGui字面/离屏及无窗口回归通过，不代表所有字体字形与Harmony完全相同。
