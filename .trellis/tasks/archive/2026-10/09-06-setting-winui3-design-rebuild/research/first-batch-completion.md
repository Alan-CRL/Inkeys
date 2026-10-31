# 首批完成记录

2026-09-06。用户审查 v1 后明确批准导航、主页、常规页首批实现。未自动开始其余页面迁移。

## 交付

- `Setting.Design.h`：DIP 导航状态与 shell/row 几何，宽窗偏好与覆盖状态分离。
- `Setting.Typography.h`：真实 HarmonyOS 字面/基线校准；旧页字体隔离，共用现有 atlas/session。
- `Setting.Controls.h/.cpp`：明确行高、统一测量/绘制、action 实际宽度靠右与窄宽度重排；输入继续用 ImFluent。
- `Setting.Shell.cpp`：248/48 DIP 导航、稳定图标/文字轴、固定 footer、滚动列表；已与批准稿 menu/header 位置对齐。
- `Setting.Pages.h/.cpp`：新主页与常规页，返回 action/events；系统副作用继续由 `Setting.cpp` 原 FIFO 宏域接回。
- `Setting.cpp` / `Setting.Base.cppm`：接线、字体注册和切换；保留 D3D、窗口公开接口、resident/epoch 生命周期。
- ImFluent 最小图标/导航/Expander 与稳定 ID 适配，`UPSTREAM.md` 记录。
- 新文案进入简/繁/英语言源并同步生成头/快照；工程登记完整。
- `setting_design_tests.cpp` 接入现有 HeadlessTests，复用实际 CPU 字体/ImGui/ImFluent/Controls。

常规原 9 卡/10 控件的自启、快捷方式、两种倍率、边缘光影、置顶索引、右键关闭、避免全屏和安全模式行为已按 `first-batch-binding-audit.md` 逐项静态核对。主页所有原外链和真实内部入口可达，教程使用已有纹理而非新造 URL。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 完整 `InkeysRepo.sln` / Debug ARM64 / ARM64 host MSBuild | PASS；最后增量 33.27 秒，0 errors |
| `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | PASS；exit 0，8.85 秒；固定汇总输出 `PASS animation correctness` 包含新增测试 |
| 独立 ARM64 CPU probe | PASS；exit 0，3.92 秒，明细见 `headless-results.md` |
| `pwsh -NoProfile -File Scripts/i18n.ps1 check` | en-US / zh-TW 均 304/304 PASS |
| 原文件编码/换行 | 与修改前基线比较 PASS，UTF-8 BOM 属性及 CRLF 保持 |
| `git diff --check` | PASS |
| Trellis 独立全差异检查 | 无未解决代码问题 |

构建日志：`Build/setting-winui3-first-batch-final-build.log`。最后构建仍有 54 条编译警告，来源为既有 Setting 图片尺寸转换及第三方代码（hashlib++、ImFluent）；本批没有扩大为第三方告警清理。

实际回归包括四个中文字体 face、12/14/28、六种有效倍率的字形/多行文本；252/320/624 DIP 预算中的四种真实控件；导航图标轴；末行 SettingsRow 后直接 EndChild。测试推动修复字体基线、末项内容边界以及 1.875 倍率物理像素取整问题，允许原生控件有限的亚像素/像素量化误差。

## 构建环境处理

直接启动 .NET Framework MSBuild 时环境同时含 Path/PATH，导致 CL 启动出现 MSB6001，早先多节点调用甚至只报告 metaproj failed。最终仅给构建子进程传入按大小写去重的环境，并把 PATH 键改为唯一 `Path`，使用 `/m:1 /nr:false`；不修改系统/用户环境或工程配置。每次构建超时 900 秒，使用完整 Solution，未跳过 PptCOM。

## 验证边界与后续

没有启动 Inkeys、Office 或任何原生测试窗口，没有真实执行自启/快捷方式/重启/退出等系统副作用。CPU 字体/DrawData 和静态接线通过不等于已验证整页 HWND/DX11 视觉；实际显示留给用户后续审阅。

其他设置页仍采用旧内部布局；共享 D3D/CSO、DWM、窗口 chrome、深色、动画、触摸和动态脏区保持本批范围外。任务保持 in_progress 以记录后续迁移；没有 commit/push。
