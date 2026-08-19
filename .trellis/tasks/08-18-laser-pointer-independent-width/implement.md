# 实施计划

1. 在 `IdtState.h/.cpp` 增加激光独立宽度和三档预设、初始化，以及 `GetPenWidth` / `SetPenWidth` 的激光分支，确保产品状态发布该值。
2. 在 `Draw3.DrawingController.cpp` 让 Laser 从 `ProductVisualStyle::widthDip` 计算笔迹与光标直径。
3. 在 `Bar.Layout.cppm` 添加激光三档预设和普通/激光控件能力判断，并保持非激光预设 `1/3/6 DIP`。
4. 在 `Bar.Interaction.cpp` 让激光三档快捷按钮独立命中，同时禁用激光态的普通展开按钮、滑条、预览弹窗、精细轮盘和扩展箭头命中，并收起已存在的状态。
5. 在 `Bar.RenderLoop.cpp` 渲染激光纯色圆形三档按钮、同步隐藏普通控件及笔型小三角，并替换激光态预览为红色外套加白色内芯。
6. 检查改动范围、编译完整 `InkeysRepo.sln` 的 `Debug|ARM64`，并运行可用的无窗口测试；不启动窗口程序。

## Risky Files

- `Bar.Interaction.cpp`：粗细控件有滑条、触摸拖动、轮盘和多个悬停入口，门禁遗漏会留下透明可点击区域。
- `Bar.RenderLoop.cpp`：预览绘制与动画、脏区共用，必须沿用现有裁剪和 transform 复位。
- `Draw3.DrawingController.cpp`：宽度同时影响活动笔迹和 CursorAppearance，需避免两者 DPI 语义不一致。

## Validation

- `git diff --check`
- ARM64 `MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64`
- 若可用：`InkeysHeadlessTests.exe --no-window`
