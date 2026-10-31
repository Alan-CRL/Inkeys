# UI3 白板菜单统一实现（2026-10-15）

## 实际路径

- `Bar.Metrics` 增加 `StandardFourOne`：140×32.5 DIP，18 DIP 图标、12 DIP 单行文字；单行左对齐仍由共享 Scene 文字测量及标准字体完成。
- `PageControl::BuildWidgets` 只声明 More→菜单→主题的父项、最终矩形和内容，不保存任何白板弹性计时或逐项几何曲线。菜单中心与 More 中心相同，三个操作行及面板内外均采用 5 DIP；取消右箭头。
- 六色和三种样式都是 `StandardTwoTwo`、70×70 DIP，标签使用标准主文字槽（font13/y20/height25）。共享 `DrawBarButtonVisual` 负责完整 hover、pressScale、frameLight、文字和图标。选中使用现有 `colorSelect` SVG，尺寸30（画笔色板15的两倍）；`selectionVisible` 只控制 SVG，不使用外围 selected highlight。
- 背景颜色/背景样式标题为共享 Word，分割线为共享 Shape（SurfaceFrame、0.30 opacity、1 DIP 高、0.5 圆角），不另写 D2D 绘制路径。
- 通用 `BarSurfaceScene` popup 直接复用 `EraserSurfaceMotion`、`ResolveEraserSurfacePose`、`ComposeEraserPose`、`FrameAnimationClock`。每块面板只有一份 motion；所有子内容继承父姿态和 alpha；子面板从 Theme 行中心伸出。保留默认 operation duration、Back 几何、Sine alpha、全局倍率/关闭动画和父批次 join。
- 白板左下使用共享 Scene `contentEnvelope`：实际 HWND presentation 来自本帧背景和可见姿态矩形的并集+shadow；绘制、damage、presentation→logical 命中采用同一个 X/Y 原点。`Advance(frameTime)` 在读 envelope 前推进，`Render` 对相同 frameTime 不会重复推进任何动画值。
- 逻辑 backing 预算用共享 `ResolveEraserPopupEnvelope` 解析曲线极值和父子姿态，不等于实际呈现窗口；保留 PageControl 原有 high-water allocation，避免动画每帧重建 target。关闭只依赖 popup motion 活跃态，不等待 hover 的5秒淡出。
- `UpdateContent` 用于换色/页码/保存状态等内容更新，不重新布局、不重开输入锁或 popup 批次。预览绘制使用缓存 brush/rounded clip，press 通过统一矩阵缩放稳定70 DIP内容，设备重建时 reset 释放旧上下文。
- 纹理以内容中心生成对称网格，样式按钮在标准文字槽上方保留纹理预览区。

## 已有问题与修正证据

1. 原 Scene 对每个菜单行/预览分别 spring x/y/w/h 并另推进 reveal，PageControl 再用 wall-clock bounds 形变，导致多个不一致的形变来源。现只保留父 popup motion；窗口包络直接消费实际姿态。
2. 旧 ApplyTargets 在共用 interaction 写 iconOpacity1 后再对无图标/未选中项写0，以及隐藏 widget interaction 写 contentOpacity0 后再写 disabledOpacity.3；每帧目标相互覆盖，底层 SetTar 相同目标短路无法阻止这类振荡。共用 Retarget 增加默认兼容的 iconVisible 参数，并统一 hidden 内容目标，消除额外无休止动画。
3. 旧 whiteboard 任意业务发布 revision 都触发全布局与输入锁期限。现只有 mode/menu/envelope 改变走 TransitionLayout；其余走 UpdateContent。
4. PPT 初次启动时 popup ID 是普通隐藏按钮，过渡后需要显式应用 panel/divider chrome；已提取 shared chrome helper 同时供 Initialize/Transition 调用。
5. 没有发现 production Scene 同一帧重复 Advance 的证据；新 prepare/render 协议为 envelope 需求新增显式帧身份幂等门禁，不把旧卡顿误报为已证明 double-advance。

## 验证交接

- 保持原 BOM/CRLF，局部 git diff --check 无空白错误。
- 主会话统一执行 ARM64 solution / 无窗口验证，避免并行 MSBuild。
- 现有离屏像素矩阵保留固定 logical backing 便于逐像素采样，新增实际 envelope、动画速度/禁用、反向、PPT→白板 chrome、无重启动画与资源重建由 check 子代理补充。
- 未启动 GUI，未测真实设备 FPS；实际流畅度、真实 PPT/任务栏/跨屏仍需人工验证。
- 无新增资源文件、无需项目注册，无 commit/push。

## 浅色背景文字补充

样式按钮按背景亮度选择现有 Light/Dark TextPrimary，保持标准2×2文字槽及粗细；selection SVG 保留固有绿色。共享 Scene interaction 尊重显式内容色，UpdateContent 随主题更新内容色，并用既有 BarUiAdvanceAnimation 推进 name.color/icon.color1/fill，避免实时切换背景后颜色目标不生效。check代理增加实际白色背景标签像素对比度回归。

## 冷启动面板像素回归修复

离屏测试发现 PPT 隐藏普通按钮实例转为面板后填充消失：TransitionLayout 在应用 panel chrome 之后对非交互项调用 StopBarButtonHoverVisual(immediate)，将 pct 再清零。现将共享 chrome 应用移到指针清理之后，保留 panel0.95/divider0.30 真实填充；保留原像素断言继续回归，不放宽阈值。
