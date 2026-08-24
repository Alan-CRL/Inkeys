# PPT 翻页控件主栏化：技术设计

## 模块边界

- 新增 `Inkeys.UI.PageControl`，统一管理 `PptBottomLeft`、`PptBottomRight`、`PptMiddleLeft`、`PptMiddleRight` 四个共享窗口的 Scene、布局、输入、拖动、动画和 RenderPipeline 客户端。
- `Inkeys.UI.Ppt` 保留 COM/WPS 状态同步、页级墨迹、翻页命令、位置配置与碰撞策略，只向 PageControl 发布 PPT 页码、可见性、回调和位置。
- `Inkeys.UI.Whiteboard` 保留白板页数据和换页回调，只向 PageControl 发布白板分页状态；不再拥有独立左右 Scene/RenderPipeline 客户端。
- 保留 `PptBottom*` 窗口角色名称，删除 `PptExitShow` 角色、窗口创建和 RenderPipeline 客户端。

## Bar Scene 扩展

- `BarSurfaceScene` 增加普通 `Button` 与 `DragHandle` widget kind。
- 普通 Button 具有 Bar 的悬停、按压、内容动画；DragHandle 参与命中和捕获，但禁用悬停、按压及点击视觉。
- 新布局使用稳定 widget ID 更新目标几何；背景与 widget 的 x/y/宽高从当前呈现值过渡到新值。重新定向从当前值开始。
- 命中测试使用当前动画几何；布局过渡期间由 PageControl 门禁输入。
- 增加外部按压视觉入口，用于键盘翻页触发对应箭头按钮的短暂按压动画。

## 布局与呈现

- PPT 底部：`165 x 42.5 DIP`，依次为 `10 DIP` 拖动条、`32.5` 上一页、`70` 页码、`32.5` 下一页；外边距与间距为 `5 DIP`。
- PPT 两侧：`42.5 x 165 DIP`，相同元素按竖向排列。
- Whiteboard：`230 x 80 DIP`，保留三枚 `70 x 70 DIP` 的 `2x2` 按钮。
- 使用能容纳白板形态的稳定 backing surface；当前可见框外区域透明且命中穿透。
- PPT 底栏首次出现只做原位透明度动画；侧栏保留侧向动画。

## PPT 与白板状态机

- 稳定态为 Hidden、PptCompact、WhiteboardExpanded；`expandedLayoutTarget` 决定 PageControl 布局，Whiteboard 背景 `active` 仍只在 Draw3 workspace 首帧就绪后发布。
- PPT 底栏已可见时进入白板：先发布 expanded 目标并锁定输入，从当前 frame 过渡到 WhiteboardExpanded；背景未 active 时不得提前接收输入。退出时反向执行。
- 在过渡中反向切换时，立即切换 expanded 目标并以当前插值状态作为新起点重新定向；背景 active/readiness 不得被布局动画伪造。
- PPT 底栏原本不可见时进入白板：在最终白板位置渐显，不经过紧凑布局。
- 旧调用方 frame 在共享控制权交接后不得继续写入 Scene。

## A2 与结束放映

- 新固定 ID 为 `Inkeys.Bar.EndShow`，尺寸为 `2x2`，复用 `ppt3` 图标和“结束放映”文字。
- 点击沿用 PPT 业务队列及确认流程，并使用现有去重/门禁语义保证单次投递。
- 配置规范化在合法旧 `{Whiteboard, Freeze}` 排列后追加 EndShow；非法列表回退为固定区默认值。
- 运行时按 Desktop/PPT/Whiteboard 三态设置按钮可见性和 Whiteboard 尺寸。

## 碰撞与兼容

- 底部组和侧边组继续走最近可行位置求解，主栏当前可见矩形优先作为障碍。
- 因主栏移动产生的冲突只写运行时位置；用户拖动完成后才持久化用户位置。
- 旧 `ShowBottomMiddle`、位置、缩放 JSON 字段保持序列化兼容，但不再驱动窗口或设置 UI。
- 不改变 PPT/WPS COM 接口、`PptInfoStateBuffer`、画布换页顺序和页级墨迹存储。

## 验证

- 扩展 headless 测试覆盖 Scene widget 类型、几何动画、外部按压、紧凑布局、状态机、A2 迁移和可见性矩阵。
- 更新窗口与调度器测试，证明旧客户端/窗口删除且四窗口生命周期正确。
- 执行静态引用扫描、`git diff --check`、ARM64 完整解决方案构建和 `--no-window` 测试。
