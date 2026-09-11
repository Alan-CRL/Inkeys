# 主 Logo 回归验证

## 验证入口

- `InkeysHeadlessTests/bar_logo_tests.cpp`：调用生产 `BarLogoAppearance::Resolve`、`IsDrawingMode` 和 `ApplyAttributes`。覆盖 6 种真实 RGB × 5 档材质 × 5 档绘制权重、Pen/Shape 与 Selection/Eraser 门控、非绘制态记忆色独立性；直接推进生产 Value/Pct/Color 的换色、反向、禁用动画和 idle。
- `--bar-logo-profile-output <path>`：从同一生产 `ApplyAttributes` 导出每个状态实际写入 SVG 的属性三元组。脚本不复制颜色、渐变或透明度公式。
- `verify_bar_logo_svg.py`：原始几何以 `4478887c:Inkeys/src/UI/logo1.svg` 和 `4478887c:Inkeys/src/UI/Frame 94.svg` 为准。浅色轮廓对比 `0606cbe0` 的 logo2；30 个普通 SVG 对比该提交。
- `render_bar_logo_svg.cjs`：应用生产属性到当前共享 logo1；使用已安装 sharp 离屏栅格化全部 150 个状态。深色端点在80/160/256三种尺寸分别与旧原始SVG节点合成做严格字节相等检查，并与旧 logo1 + 旧 Frame 94 两张8位图的真实合成比较，浅色绘制端点与原浅色 Logo 比较，同时检查四段笔身真实 RGB、两处断口透明和屏幕颜色独立。

深色对照不会调用旧 `DisplayPenColor`，也不会使用上一任务中已改为整笔实色的 Frame 94 作为基线。透明边缘按预乘 RGBA 比较，允许单文档与旧双位图合成产生极小舍入差：同一SVG文档的旧原始节点合成必须逐像素完全一致；旧双位图合成的最大通道差不超过3/255、平均通道差低于0.10/255。通道差大于1的像素计数作为诊断输出，不以任意计数阈值误判两次8位量化的舍入差。每个样本实际误差另存 JSON，不以阈值冒充实测结果。

## 命令

完整 Solution 构建和无窗口测试由主会话执行。完成后，在仓库根运行：

```powershell
New-Item -ItemType Directory -Force Build/BarLogoValidation
./Build/LogoIndicatorFix/ARM64/Debug/InkeysHeadlessTests.exe --no-window --bar-logo-profile-output Build/BarLogoValidation/profile.json
python .trellis/tasks/archive/2026-09/09-11-bar-logo-state-transition/research/verify_bar_logo_svg.py
& 'C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe' .trellis/tasks/archive/2026-09/09-11-bar-logo-state-transition/research/render_bar_logo_svg.cjs --profile Build/BarLogoValidation/profile.json --sharp C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp --output Build/BarLogoValidation
```

## 证据边界

静态 SVG 校验、完整ARM64构建、全部无窗口测试和离屏回归均已通过。色源选择与 Geometry 接管清除非 Brush 历史由 RenderLoop 静态检查覆盖。此测试不启动 HWND，不运行产品 GUI，不声称覆盖 D2D 实际呈现、设备重建或真实点击交互；相应缓存/dirty 合同需结合产品代码审查和用户视觉验收。
## 最终离屏结果

- 150组生产属性状态全部通过；6种颜色×2种Dark端点状态×3种尺寸的36组原节点合成对照全部逐像素一致（最大差0）。
- 旧双位图合成的实测最大通道差2.239216/255，最大平均通道差0.029074/255；实际逐样本报告为Build/BarLogoValidation/dark-baseline-differences.json。
- Light整笔真实RGB、两处分节透明、screen中性、非绘制态与记忆色无关、中间权重检查通过。
- Build/BarLogoValidation/logo-state-contact-sheet.png已由主会话查看，展示两种端点和三个中间材质状态。
