# 实施计划

1. 在 `Bar.Main.cppm` 连续 Shape/Word 枚举中登记浮窗 Surface、白色墨迹圆和数值对象；在初始化路径配置 Surface 第三光与无交互内容对象。
2. 在渲染状态区加入浮窗展开、数字迁移和 Hold 控制行交换动画，绑定既有 Thumb 可见目标与统一动画时钟；浮窗 X 不新增独立动画值。
3. 从 Thumb 当前最终布局中心和笔类型控件几何计算浮窗自适应尺寸、Thumb 外缘 10 DIP 目标位置与数值内外布局；拖动、离散变化和快速重定向直接跟随 Thumb 视觉 X，并在 desired X 和最终回弹矩形两处执行真实笔类型左边界硬限制。
4. 将三个快捷按钮/小三角与 Hold 组接入单次短促透明度交换，移除中点屏障和子控件追赶动画；提示文字复用主栏内容的 EaseOutBack 缩放回弹，圆环置于文字左侧、文字右端对齐控制行。
5. 拆分 Thumb 最终绘制通道，按“普通属性内容 → 浮窗 → Thumb”渲染，补齐 PointLight 几何归一和 predicted/current 脏区。
6. 静态检查所有 Slider 清理、直接触摸 Preview、Hold-lock、颜色选择器和上下换边路径，确保新视觉没有独立残留状态；确认 Preview 相对粗细映射仅放大 direct-touch 水平行程，不影响其他 Slider 路径。
7. 在 Thumb 完全显示后的面板外侧留白派生精细命中矩形，扣除 Overflow Badge 右侧占位；将局部相对手势接入同一 Slider 捕获循环并复用 3 倍行程投影，不新增 Shape 或公共状态。
8. 运行 `git diff --check`、换行检查和 ARM64 host `MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`；记录无法自动化的视觉验收矩阵。

## 风险与回滚点

- Thumb 当前位于 Preview clip 内自绘；拆分后必须保证 clip 成对弹出并保留原视觉参数，否则优先回滚绘制顺序拆分。
- Surface 动态尺寸会影响 PointLight mask key；必须使用现有几何缩放归一，稳定状态不得持续 cache miss。
- 避让只针对笔类型控件且明确不做窗口边缘 clamp，不能复用现有 Tooltip 的 `BuildPopupLayout()` 夹取逻辑。

## 验证记录

- 2026-08-08：Preview X 改为直接读取 Thumb 当帧最终视觉中心，独立 X 动画状态已删除；safe bound 继续位于该中心之后。
- 2026-08-08：实际效果纠正完成；移除无效碰撞分支与 Hold 中点屏障，`git diff --check`、UTF-8 无 BOM / CRLF 检查通过。
- 2026-08-08：ARM64 host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 通过，0 个错误；47 个警告均为现有编译警告。
- 2026-08-08：Hold 文字与圆环改为共享组透明度和组缩放；补齐同步回弹进入、短促退出与 `SetTar` 中途反转。圆环额外透明度收窄为锁定态抑制，业务圆弧仍独立读取 `thicknessSliderHoldProgress`。
- 2026-08-08：Hold-lock 从 Thumb/浮窗可见条件中解耦；锁定仅通过既有双重更新门冻结粗细，Thumb 与浮窗保持到真实 Pointer Up 后再走原 release/cleanup。
- 2026-08-08：direct-touch Preview 相对粗细映射改用 3 倍水平行程；5 DIP 阈值、按下点零原点、取整/clamp 及鼠标/已显示 Slider 触摸路径保持不变。
- 2026-08-08：Thumb 完全显示后新增外侧留白精细区；上下方向和等距来自当前动画几何，完整轨道宽度扣除 Overflow Badge，Popup 不参与避让。鼠标、合成触摸与笔复用现有捕获循环和 3 倍相对映射，轻点不改值；兼容消息过滤仅抑制重复触摸消息，不再误丢笔输入。
- 2026-08-08：direct-touch Preview 超过 5 DIP 且有效改值后接入既有 Hold-lock；无消息帧使用最后触点推进 0.5 秒提示与后续 1.5 秒锁定，改值重置静止会话。统一候选锁门保证锁定后 MOVE/UP 均不覆盖冻结值，真实触摸抬起只提交一次并继续保持 Preview。
- 2026-08-08：上述 direct-touch Hold-lock 修改经 ARM64 host 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 通过，0 个错误；46 个警告均为现有编译警告。
- 仍需在实际 UI3 运行环境完成 DPI、上下展开、Hold 分阶段交换、触摸 Preview 和快速反转动画的视觉验收。
