# 启动预览外观链路研究

## 当前链路

1. IdtMain.cpp:2626 创建局部 startupMiniConfig 并 ReadMini；2691 附近组装 StartOptions，随后 StartupPreview::Start 复制 options 到 Runtime。
2. StartupPreview.cpp:398 的 RenderResources 持有 target/mask/shape/brush；EnsureResources 按 device generation 与尺寸重建。
3. StartupPreview.cpp:579 用 base fill + frame 的最终 alpha 创建 A8 mask。Drawing::CreateShimmer 当前为七段白色线性渐变。DrawShimmer 的 contentOpacity 参数目前恒传 1，命名不能作为该层属于内容的依据。
4. StartupPreview.cpp:790 附近依次 DrawShape、DrawShimmer、DrawProgress；整窗 alpha 仅在 ULW SourceConstantAlpha 应用。
5. DrawProgress 的 visual.opacity 来自既有状态机；两种形状实际调用 Drawing::DrawProgress，失败 0% 染红轨道而不虚构完成量。

## 复用与约束

- Other.Config.cppm 提供 ThemeModeUsesDarkStyle、NormalizeThemeMode、NormalizeResidentBackgroundOpacity、NormalizeResidentContentOpacity；ReadMini 使用同一字段 codec。
- Bar.ThemeMaterial.h 提供 LightColor、ResidentFillOpacityScale、DefaultResidentFillOpacity。注释中有遗留 1.10/0.88 文案，但当前常量 LightResidentSurfaceOpacityScale=1.15，展开浅色实际默认 0.92；本任务不要顺手修无关文案。
- Bar.RenderLoop.cpp:698 直接确认浅色折叠主按钮 dark 材质；2647 附近更新同一目标。用户对本次角落预览的期望待确认。
- 透明度合同：BackgroundOpacity=0 主题默认，其余归一 30~100；主栏是绝对 fill，角落按钮按 0.6/0.8 同比例。ContentOpacity 默认 100，归一 30~100。
- 当前背景各元素 source-over 会叠加 alpha；实现时需核实分组或等价合成，确保反光属于背景而不会在低透明度下变成独立不透明亮斑。保留 premultiplied BGRA 和最终 ULW fade 单次应用。
- 不导入完整 Bar renderer，不另建渲染 device/thread，不引入 Win10-only effect；浅色晕染应在现有 D2D 渐变/遮罩能力内完成。
- 不修改真实进度 reducer 和 handoff 时序；设备恢复必须重建与冻结外观一致的资源。

## 验证入口

- InkeysHeadlessTests/startup_preview_state_tests.cpp 已有真实 Config 临时目录 fixture 和无窗口 WARP/D2D OffscreenPreview，可扩展实际生产绘制，不能只复制一套新算法测试。
- 像素测试已有 SavePreviewBitmap 输出 Build/StartupPreviewVisualQA；可按深浅/两种外形/透明度组合导出并静态看图，不创建窗口。
- 原生 ARM64 MSBuild 先通过当前 VS 安装定位，构建 InkeysRepo.sln Debug|ARM64；在同一次 PowerShell 中执行 Remove-Item Env:PATH -ErrorAction SilentlyContinue 并设置 MSBUILDDISABLENODEREUSE=1。
- Headless 命令必须带 --no-window；GUI smoke/manual-delay 不在授权范围内。
- 默认 python 命中不可运行的 WindowsApps 别名；已使用现有 C:/Program Files/KiCad/10.0/bin/python.exe 正常执行 Trellis，不修改全局环境。

## 工作区基线

主分支 bugfix/theme，最新 fa5f8711。原始 git status 显示十个历史 eraser-demo PNG 的 AD 状态；Trellis get_context 将其过滤为 0，不能据此覆盖或清理那些暂存项。本任务创建了自身目录，尚未修改产品代码。

## 下一步

确认 Q1 后完成 design.md / implement.md、实施/检查 JSONL 清单及 PRD 收敛；最终方案评审通过后再 task.py start，按 auto 模式分派 Trellis implement/check 子代理。无 commit/push 授权。
