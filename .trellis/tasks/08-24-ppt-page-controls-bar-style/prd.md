# PPT 翻页控件主栏化

## Goal

纠正当前分页控件另建按钮行为实现的架构偏差，让 PPT 与 Whiteboard 分页按钮复用主栏 Bar 按钮唯一的计算、交互、动画、绘制和 dirty 合同，同时保留四个分页 HWND、PPT 专属拖动/页码能力以及已经完成的 EndShow 主栏迁移。

## Background

- 当前 `BarSurfaceScene` 只复用了 Bar 的底层类型、主题常量和 Shape/SVG/Word 绘制，却自行维护按钮布局、hover/press、内容位置、动画推进、命中和 damage；主栏并不消费这套按钮状态机。
- 该分叉已经造成悬停/按下动画、SVG 位置、Whiteboard 拖动和碰撞范围偏离原始需求，继续修补分页专用参数不能恢复单一行为来源。
- `PptBottomLeft`、`PptBottomRight`、`PptMiddleLeft`、`PptMiddleRight` 四个共享宿主、EndShow A2 按钮、COM/WPS 与页级墨迹业务保持现有边界；本轮只纠正分页视觉/输入架构及被错误固化的规范与测试。

## Requirements

1. **单一按钮实现**：各分页窗口可独立持有 `BarButtonClass` 实例和 D2D 资源，但按钮尺寸解析、内部布局、图标/文字定位、hover、press、内容转换、绘制、dirty 和命中必须调用主栏使用的同一实现。共享常量、底层 Shape/SVG/Word 或近似动画均不构成复用；PageControl 中禁止保留镜像状态机。
2. **所有权边界**：`Inkeys.UI.PageControl` 只拥有四个分页 HWND/RenderPipeline 客户端、每窗渲染资源、分页组拓扑与锚点、状态路由、PPT 专属拖动条/页码内容策略和业务回调。`Inkeys.UI.Ppt` 与 `Inkeys.UI.Whiteboard` 只发布业务状态及回调。
3. **稳定实例形变**：底部每个 surface 的 Previous、Page、Next 是跨 `PptCompact`/`WhiteboardExpanded` 的三枚稳定按钮实例；切换只改变同一实例的尺寸、位置和内容，不创建第二套按钮、不交换 renderer owner。PPT 宽页码形态与 Whiteboard 中间 `2x2` 页码按钮是同一实例。
4. **共享背景合同**：分页外层背景的深色填充、边框、第一/第三光源、绘制和 dirty 直接复用主栏背景实现。PageControl 只提供子控件联合外框目标，并将背景与按钮放入同一 Bar 动画批次。紧凑态和 Whiteboard 的外框圆角必须直接读取主栏 `BarMainBarCornerRadiusDip`，按钮圆角必须直接读取 `BarButtonCornerRadiusDip`（当前分别为 `8/4 DIP`），边框和内边距同样使用主栏单一来源；禁止在 PageControl 复制数值。第三光源不得由分页本地鼠标位置生成：四个 HWND 只加入主栏唯一的接收窗口/可见区域集合，并消费同一屏幕坐标、半径、强度、`Dormant/Inside/Grace` 与 5 秒期限。
5. **固定几何**：PPT 底部外框为 `165x42.5 DIP`，两侧为 `42.5x165 DIP`；PPT 拖动槽 `10 DIP`、箭头按钮 `32.5x32.5 DIP`、页码按钮横向 `70x32.5 DIP`/竖向 `32.5x70 DIP`，两侧外边距和真实按钮之间的间距为 `5 DIP`。Drag 是 divider lane，与相邻 Arrow 直接相接、不增加第三个 `5 DIP` 间距。Whiteboard 外框为 `230x80 DIP`，包含三枚 `70x70 DIP` 标准 `2x2` 按钮。
6. **页码内容**：底部页码把加粗当前页和常规字重 `/总页数` 作为整体测量并水平居中；两侧将当前页置上、`/总页数` 置下并整体垂直居中；Whiteboard 使用标准 `2x2` 上方主内容槽和下方标签槽。PPT 未知值继续显示 `-`/`/-`，底部和侧边分别保留 `9999/999` 显示上限。三种形态共用内容状态，禁止用分页专用固定偏移模拟居中。页码按钮始终 no-op，但保留标准 Bar hover/press。
7. **SVG 与内容动画**：普通 Previous/Next 始终复用同一 `barMore` SVG 实例，通过方向变换表示语义；PPT Arrow 与 Whiteboard Arrow 之间只缩放/移动同一 SVG 并动画标签显隐。只有 Whiteboard Next 的 Arrow/Add 语义真实变化时，才以主栏内容转换在 `barMore`/`右翻页` 与 `barAdd`/`加页` 间切换；几何、SVG 和标签在同一批次并行推进，未变化语义不得重启动画。
8. **PPT 专属输入**：拖动条独立于 Bar 按钮体系，是 PPT 唯一拖动入口，只参与命中、capture 和成对拖动，不产生按钮视觉。PPT 保留位置/缩放、长按连续翻页、滚轮、键盘闪按、DPI 和位置持久化语义。
9. **Whiteboard 输入隔离**：Whiteboard 只接受普通鼠标、笔和单指触摸点击并使用标准 Bar hover/press；不得继承拖动、缩放、位置记忆、长按或滚轮。翻页事务只关闭 `interactive`，未变化的 Arrow/Add/文字和启用视觉保持稳定，不得以禁用变灰模拟锁定。
10. **拖动条过渡**：进入 Whiteboard 起始即撤销 capture 并关闭拖动条命中；视觉随共享批次淡出且 `10 DIP` 槽位同步收拢。退出时槽位展开、拖动条淡入，只有紧凑布局成功呈现后恢复命中。反向切换从当前透明度和槽位几何重定向，禁止瞬间移除槽位或过渡期捕获输入。
11. **碰撞边界**：PPT 只处理底部一对、侧边一对之间的冲突和屏幕越界；主栏、主按钮和 Bar HWND 不参与，也不触发重算。手动拖动只移动命中对并停在最近可行位置；显示/DPI/缩放纠偏保留底部对、让侧边对寻找最近位置，极端不足时只降低侧边对运行时缩放。自动纠偏不得写入保存配置。
12. **工作区位置与状态机**：Whiteboard 固定在左右下角 `5 DIP`，不读取 PPT 位置/缩放且不进入 PPT 碰撞求解。可见 PPT 底栏进入 Whiteboard 时，从当前实际位置同时动画到固定位置并形变；退出返回 PPT 最新运行时位置。反向切换从当前插值值重定向并全程锁输入。PPT 底栏原本不可见时，Whiteboard 直接以最终位置/几何渐显。
13. **既有业务兼容**：保留四个共享窗口、侧栏既有侧向显隐、底栏原位渐显、EndShow A2/配置迁移、旧 JSON 兼容字段、`PptInfoStateBuffer`、COM ABI、PowerPoint/WPS 支持范围、画布换页顺序和页级墨迹存储。

## Acceptance Criteria

- [x] Main Bar 与 PageControl 对同一按钮输入调用同一 Bar metrics、hover/press、draw、背景 draw 和圆角 hit 实现；PageControl 不再设置标准按钮 SVG/文字偏移或拥有本地 hover/pressed visual state。
- [x] 共享 draw 以显式父级 `inherit` 同步按钮、SVG 和主/次文字；PageControl 非零局部原点不再读取默认或上一帧父缓存。
- [x] PageControl 删除本地第三光源 prepare/reset；真实鼠标进入/离开四窗只通知 Main Bar，成功窗口提交发布屏幕边界，分页绘制消费 Main Bar 最终光源快照。
- [x] Headless 直接验证共享 `1x1/2x1/2x2` metrics、PPT/Whiteboard 输入策略、Arrow/Add 语义、固定位置和 PPT-only 碰撞；生产调用点静态确认两类宿主消费同一按钮运行时，最终 damage/present union 仍由各 HWND 宿主按共享视觉几何映射。
- [x] PPT→Whiteboard→PPT 全程保留相同按钮 ID/实例；普通 Arrow 不发生 SVG 资源转换，Arrow/Add 只在语义真实变化时随几何同批转换一次。
- [x] Whiteboard 固定位置且无法拖动、滚轮、长按或持久化位置；PPT 的拖动、长按、滚轮、键盘闪按和位置/缩放配置保持。
- [x] PPT 四控件仅互相碰撞并遵守底部优先/侧边回退；移动主栏不会改变分页位置，运行时纠偏不写保存配置。
- [x] 可见 PPT 底栏从用户当前位置连续进入固定 Whiteboard 位置并反向返回；拖动条槽位、背景、按钮、SVG/文字同批动画，反向重入无跳帧且过渡输入锁定。
- [x] `PptExitShow` 旧窗口/客户端保持删除，EndShow A2、三态可见性、COM/WPS、页级墨迹和旧 JSON 兼容回归通过。
- [x] `git diff --check`、ARM64 `InkeysRepo.sln` `Debug|ARM64` 完整构建和 ARM64 `InkeysHeadlessTests.exe --no-window` 通过。

## Out of Scope

- 不修改 EndShow 产品设计、A2 排列、PPT/WPS COM 协议、Draw3 文档/画布换页、页级墨迹或设置 schema。
- 不为 Whiteboard 新增长按、滚轮、拖动、缩放、位置配置或页管理界面。
- 真实 PowerPoint/WPS、触摸拖动、光影、DPI 和连续工作区切换保留为后续设备手工验收；本任务只允许静态检查、完整构建和无窗口测试。
