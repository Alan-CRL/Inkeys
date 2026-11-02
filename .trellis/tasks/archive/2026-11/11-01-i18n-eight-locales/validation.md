# 最终验证记录

日期：2026-11-02。实现与自动验证完成；保留未提交改动，未执行GUI/Computer Use、commit、push或归档。

## 交付

- 共11种语言，每种679叶子：原3种 + zh-HK、ja-JP、ko-KR、fr-FR、de-DE、es-ES、pt-BR、ru-RU。
- 8个活动设置漏接文案 + 组件37、DDB38、EDID标题1，共84个新增key；生成头及基准快照由原i18n脚本同步。
- 原报告116项最终106项至少一语修正、10项按实际行为/已批准地区化/空间例外保留。
- 原三语实际差异：[translation-changes.md](translation-changes.md)，451项：zh-CN 42修改+84新增，en-US 79修改+84新增，zh-TW 78修改+84新增，无移除。
- Noto Sans JP/KR静态TrueType Regular/Bold共四文件内嵌，OFL与来源/哈希保留；实际DWrite、ImGui及独立GDI+消费者已接入。
- 已实现母语名+语言码+Beta列表、首次系统语言匹配及旧0/1/2保存值兼容。仅zh-CN非Beta，完整生效沿用重启方式。

## 最终已通过的验证

| 验证 | 结果 | 证据 |
|---|---|---|
| Scripts/i18n.ps1 check | 十个非基准语言均679/679，PASS | 当前资源与基准逐项检查 |
| 生成头/基准快照 | 679 key；快照与zh-CN SHA-256一致 | 由sync生成，未手改生成头 |
| 完整InkeysRepo.sln Debug\|ARM64 | 退出0；最后一次增量10.15秒 | 动态定位VS的ARM64原生MSBuild；同invocation规范化Path/PATH，30分钟超时 |
| Inkeys.exe --i18n-locales-test | 退出0；0.85秒 | 真实内嵌Language/LANGID、索引、Beta、地区匹配、生产类型std::vformat及printf边界 |
| Inkeys.exe --bar-eraser-offscreen-test | 退出0；29.37秒 | EraserVisual failures=0；实际DWrite glyph、37/70DIP、组件、菜单、提示及真实转场 |
| InkeysHeadlessTests.exe --no-window | 退出0；338.72秒，stderr空 | 真实字体、混合语言列表、多倍率窄/宽设置控件、组件/DDB/赞助长文、MessageBox及既有回归 |
| 静态字体cmap | 全部语言Regular/Bold合并覆盖缺字0 | research/font-coverage.json |
| 编码/BOM/换行与diff | 通过 | 按Git检出规则比较；core.autocrlf=true，工作区CRLF保持 |

完整控制台日志位于仓库内忽略目录 `.trellis/.runtime/i18n-validation/`；离屏结果与PNG位于 `Build/eraser-b/`。完整构建仍有仓库既有类型转换/第三方警告，未为清零warning修改无关代码。

## 修复及复核依据

- 独立审查：`research/integration-review.md`；八语高风险语义审查：`research/translation-review.md`。
- 资源真实加载测试避免“请求代码已更新但RC取错语言”的假阳性；format测试直接运行真实模板，不经生产fallback隐藏异常。
- 对真实测量出的溢出精炼18条短译，并仅对主标签启用有可读性下限的局部DWrite拟合。正常标签和其他Word调用不变，同一比例作用于字面与名义fontCorrection目标。
- 赞助说明按可见glyph边界居中，按round后的ink登记完整高度；没有放宽居中/完整性断言，没有改共享主页营销测量。
- 原工作报告建议未机械套用：保留社区地区分流、已批准主页文案/署名等；英文Board按37DIP槽空间例外保留，完整窗口标题继续Whiteboard。

## 验证边界

未打开主窗口或设置窗口，未进行真实桌面视觉、触控、外部Office/WPS联动或其他系统实机验证；离屏与无窗口通过不替代这些证据。启动前故障兜底、原始技术诊断数据及已证实不可达旧UI保持既有边界；没有扩展平台或UIAccess能力/合规承诺。
