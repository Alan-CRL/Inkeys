# 启动效果最终验证

## 已完成

ReadMini 冻结效果、主题和背景/内容透明度；掠光、淡彩虹晕染与像素拼装；角落深色；内联下拉框与二十语；DPI/Zoom、进度及先隐后显交接。

## 验证结果

- ARM64 原生 MSBuild，InkeysRepo.sln，Debug|ARM64，完整 solution：`build-arm64-ready.result.json` exit0。
- `InkeysHeadlessTests.exe --no-window --startup-preview-only`：`startup-final.result.json` exit0，failures0。
- 完整 `InkeysHeadlessTests.exe --no-window`：`headless-final.result.json` exit0，PASS animation correctness。
- 国际化 sync/check 全20语通过；完整 diff --check 通过；38个原有修改文件保持UTF-8/BOM/原工作区CRLF，Git索引中的LF是正常autocrlf归一。新头文件已由实现/检查代理复核。
- Inkeys/PptCOM.dll 与 Inkeys/PptCOM.tlb SHA-256 保持任务前值。
- 完整静态审查无剩余确定问题。

## 原生图像

原始关键帧位于 `Build/StartupPreviewVisualQA/`。主会话检查了深浅彩虹和像素入场/散布/回收/归位/等待阶段；实际底板与进度层级正确，背景保持透明。`native-rainbow-review.png` 与 `native-particle-review.png` 为原生BMP在棋盘背景上的检查合成，不是GUI截图。

## 纠正与精度

- 拖尾固定30Hz，30/60/120Hz同一时刻图像一致。
- GaussianBlur SOFT源区域显式限定，8轮脉冲重心不漂移。
- history ceil尺寸按精确逆比例映射，7x5→13x9裁剪脉冲中心正确。
- 负坐标几何测试分开验证完整2W×3H与工作区裁剪。
- 底板等价两例实测只有3/1个RGB像素相差1LSB，alpha完全相同；仅RGB容差1，alpha/几何仍严格一致。

## 环境说明

沙箱内MSBuild并行节点通信停滞；改串行后出现C1902 PDB管理器错误。同一工具链在主机环境正常编译。默认输出目录出现LNK1104无法写入Inkeys.exe，因此完整solution以现有OutDir属性输出到本任务 `validation/bin/`，未改变任何工程配置/SDK/依赖/调试信息选项。Path/PATH在同次PowerShell归一并保留有效工具路径。最终构建和测试都通过，构建过程中的既有第三方warning未做无关清理。

## 边界

未启动主程序/设置窗口、未使用Computer Use或执行GUI测试；未进行真实Win7/多屏桌面验收。没有commit或push。原工作区暂存的旧eraser PNG不属于本任务，测试自动重建的相同图片会恢复为任务前缺失状态，索引保持不变。
