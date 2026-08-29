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
4. **共享背景合同**：分页外层背景的深色填充、边框、第一/第三光源、绘制和 dirty 直接复用主栏背景实现。PageControl 只提供子控件联合外框目标，并将背景与按钮放入同一 Bar 动画批次。紧凑态和 Whiteboard 的外框圆角必须直接读取主栏 `BarMainBarCornerRadiusDip`，按钮圆角必须直接读取 `BarButtonCornerRadiusDip`（当前分别为 `8/4 DIP`），边框和内边距同样使用主栏单一来源；禁止在 PageControl 复制数值。分页外框的第三光源强度比例必须与 Main Bar 外框一致，保持背景 Shape 默认 `1.0`，不得误用按钮的 `BarButtonCursorLightIntensity`。第三光源不得由分页本地鼠标位置生成：四个 HWND 只加入主栏唯一的接收窗口/可见区域集合，并消费同一屏幕坐标、半径、强度、`Dormant/Inside/Grace` 与 5 秒期限。
5. **固定几何**：PPT 底部外框为 `165x42.5 DIP`，两侧为 `42.5x165 DIP`；PPT 拖动槽 `10 DIP`、箭头按钮 `32.5x32.5 DIP`、页码按钮横向 `70x32.5 DIP`/竖向 `32.5x70 DIP`，两侧外边距和真实按钮之间的间距为 `5 DIP`。Drag 是 divider lane，与相邻 Arrow 直接相接、不增加第三个 `5 DIP` 间距。Whiteboard 外框为 `230x80 DIP`，包含三枚 `70x70 DIP` 标准 `2x2` 按钮。
6. **页码内容**：底部页码把加粗当前页和常规字重 `/总页数` 作为整体测量并水平居中；两侧将当前页置上、`/总页数` 置下并整体垂直居中；Whiteboard 使用标准 `2x2` 上方主内容槽和下方标签槽。PPT 未知值继续显示 `-`/`/-`，底部和侧边分别保留 `9999/999` 显示上限。三种形态共用内容状态，禁止用分页专用固定偏移模拟居中。数值变化必须在收到已确认状态的同一更新中直接替换文字并重新测量，不运行 `TransitionToString`；Arrow/Add SVG 与语义标签仍使用共享内容转换。PPT 页码短按调用既有 `ViewShow` 打开 PPT 预览，Whiteboard 页码保持 no-op；两者都保留标准 Bar hover/press。
7. **SVG 与内容动画**：普通 Previous/Next 始终复用同一 `barMore` SVG 实例，通过方向变换表示语义；PPT Arrow 与 Whiteboard Arrow 之间只缩放/移动同一 SVG 并动画标签显隐。只有 Whiteboard Next 的 Arrow/Add 语义真实变化时，才以主栏内容转换在 `barMore`/`右翻页` 与 `barAdd`/`加页` 间切换；几何、SVG 和标签在同一批次并行推进，未变化语义不得重启动画。
8. **PPT 专属输入**：拖动条独立于 Bar 按钮体系，只参与命中、capture 和成对拖动，不产生按钮视觉；但 PPT 成对拖动手势可从 DragHandle、页码按钮和未被 Previous/Next 占用的真实圆角外框背景开始。Previous/Next 始终是纯按钮，按下或拖出都不得转换成拖动。Page 按下先保留标准 press，移动越过系统 drag threshold 后取消 press 并转换成拖动；阈值内且仍在 Page 上抬起时调用 `ViewShow`，不得写入位置。箭头按下立即翻一页；`EnablePageButtonLongPress=true` 时在 Pointer Down 快照系统 `SPI_GETKEYBOARDDELAY/SPI_GETKEYBOARDSPEED`，首次重复与后续间隔模拟真实键盘，COM outstanding 只合并未完成命令而不形成积压追赶。移出箭头、抬起、capture cancel 或工作区切换立即停止；配置关闭时只保留按下时的一次翻页。PPT 保留位置/缩放、滚轮、DPI 和位置持久化；删除键盘 Hook 和滚轮合成的按钮闪按反馈，但保留真实鼠标/笔/触摸产生的标准 hover/press。
9. **Whiteboard 输入隔离**：Whiteboard 只接受普通鼠标、笔和单指触摸点击并使用标准 Bar hover/press；不得继承拖动、缩放、位置记忆、长按或滚轮。翻页事务只关闭 `interactive`，未变化的 Arrow/Add/文字和启用视觉保持稳定，不得以禁用变灰模拟锁定。
10. **拖动条过渡**：进入 Whiteboard 起始即撤销 capture 并关闭拖动条命中；视觉随共享批次淡出且 `10 DIP` 槽位同步收拢。退出时槽位展开、拖动条淡入，只有紧凑布局成功呈现后恢复命中。反向切换从当前透明度和槽位几何重定向，禁止瞬间移除槽位或过渡期捕获输入。
11. **碰撞边界**：PPT 只处理底部一对、侧边一对之间的冲突和屏幕越界；主栏、主按钮和 Bar HWND 不参与，也不触发重算。手动拖动只移动命中对并停在最近可行位置；显示/DPI/缩放纠偏保留底部对、让侧边对寻找最近位置，极端不足时只降低侧边对运行时缩放。自动纠偏不得写入保存配置。
12. **工作区位置与状态机**：Whiteboard 固定在左右下角 `5 DIP`，不读取 PPT 位置/缩放且不进入 PPT 碰撞求解。可见 PPT 底栏进入 Whiteboard 时，从当前实际位置同时动画到固定位置并形变；退出返回 PPT 最新运行时位置。反向切换从当前插值值重定向并全程锁输入。PPT 底栏原本不可见时，Whiteboard 直接以最终位置/几何渐显。
13. **既有业务兼容**：保留四个共享窗口、侧栏既有侧向显隐、底栏原位渐显、EndShow A2/配置迁移、旧 JSON 兼容字段、`PptInfoStateBuffer`、COM ABI、PowerPoint/WPS 支持范围、画布换页顺序和页级墨迹存储。
14. **页状态时效**：`PptInfoStateBuffer` 仍只在 Draw3 已到达 COM 目标页后更新，UI 不得提前显示尚未完成画板切换的页码。PowerPoint/WPS 共享内存状态在不扩展 COM ABI 的前提下以不超过 `50ms` 的有界节拍检查；Draw3 完成等待必须复用 `WaitForProductRuntimeRevision` 事件唤醒，禁止继续用固定 `500ms` 睡眠串联两阶段。
15. **最近交互层级**：任一可交互 PPT PageControl 收到有效 Pointer Down 时，目标窗口必须通过 `PromotePptWindow` 移到其他 PPT 窗口之上、Bar 之下，不激活窗口、不进入 topmost band；拖动直移的 `SWP_NOZORDER` 不得替代这次交互层级维护。
16. **Draw3 绘制活动**：Draw3 必须按所有物理 contact 的聚合状态发布 `0→1` Started 与 `1→0` Ended，多个 contact 不重复通知；停止或异常退出时若仍 active 必须补 Ended。首次 Started 收起绘制属性、几何属性、更多、笔型、粗细、颜色和提示等主栏次级界面，但不改变主栏 `fold`、不隐藏 PageControl。Started 还必须无条件让第三鼠标光进入 `Dormant`；若光标仍在 Bar/PageControl 实际接收区，则阻止重新激活直到真实离开后再次自然进入。第一光源、颜色过渡和普通 UI 动画不得因绘制持续而被压制。
17. **启动输入与触摸生命周期**：首次 Hidden 配置必须提交真实透明度 `0`；Hidden→Visible 的 transition deadline 自身必须维持 PageControl 续帧和输入锁，期限后无须 Bar 或光源消息也能自行解锁。四个 PageControl HWND 继续创建触摸注册与边缘手势禁用，并在 WndProc 禁用 press-and-hold/flick 等 Tablet 手势；`WM_TOUCH` 必须支持 primary 缺失时锁定首个 DOWN、活动触点替换 cancel、最后坐标锁存和单触点 Move/Up，保证触摸长按可进入与鼠标相同的箭头重复路径。
18. **结束放映图标与结束页语义**：深色主题下 EndShow 必须使用与主栏其他图标一致的主题白色 SVG，采用统一的 `24x24` 画布、圆角端点/连接和相近线宽；Main Bar A2 与 PageControl 共用同一 `barEndShow` 资源，不再复用黑色 `ppt3` PNG。PPT 快照满足 `totalPage > 0 && currentPage < 0` 时，四个 PageControl 的 Next 稳定按钮实例把 `barMore` 动画切换为 `barEndShow` 且保持正向 `0°`；当前页恢复有效时反向切回各 surface 的箭头角度。资源替换复用 Whiteboard Arrow/Add 的共享中点内容转换，点击和长按仍走既有 NextPage 回调，不改 EndShow A2 回调或 COM 协议。

## Acceptance Criteria

- [x] Main Bar 与 PageControl 对同一按钮输入调用同一 Bar metrics、hover/press、draw、背景 draw 和圆角 hit 实现；PageControl 不再设置标准按钮 SVG/文字偏移或拥有本地 hover/pressed visual state。
- [x] 共享 draw 以显式父级 `inherit` 同步按钮、SVG 和主/次文字；PageControl 非零局部原点不再读取默认或上一帧父缓存。
- [x] PageControl 删除本地第三光源 prepare/reset；真实鼠标进入/离开四窗只通知 Main Bar，成功窗口提交发布屏幕边界，分页绘制消费 Main Bar 最终光源快照。
- [x] Headless 直接验证共享 `1x1/2x1/2x2` metrics、PPT/Whiteboard 输入策略、Arrow/Add 语义、固定位置和 PPT-only 碰撞；生产调用点静态确认两类宿主消费同一按钮运行时，最终 damage/present union 仍由各 HWND 宿主按共享视觉几何映射。
- [x] PPT→Whiteboard→PPT 全程保留相同按钮 ID/实例；普通 Arrow 不发生 SVG 资源转换，Arrow/Add 只在语义真实变化时随几何同批转换一次。
- [x] Whiteboard 固定位置且无法拖动、滚轮、长按或持久化位置；PPT 的拖动、长按、滚轮和位置/缩放配置保持。
- [x] PPT 四控件仅互相碰撞并遵守底部优先/侧边回退；移动主栏不会改变分页位置，运行时纠偏不写保存配置。
- [x] 可见 PPT 底栏从用户当前位置连续进入固定 Whiteboard 位置并反向返回；拖动条槽位、背景、按钮、SVG/文字同批动画，反向重入无跳帧且过渡输入锁定。
- [x] `PptExitShow` 旧窗口/客户端保持删除，EndShow A2、三态可见性、COM/WPS、页级墨迹和旧 JSON 兼容回归通过。
- [x] `git diff --check`、ARM64 `InkeysRepo.sln` `Debug|ARM64` 完整构建和 ARM64 `InkeysHeadlessTests.exe --no-window` 通过。
- [x] 分页外框与 Main Bar 外框对同一共享光源快照使用相同强度比例；不得把按钮强度比例写入分页背景。
- [x] PPT 的 DragHandle、Page 和非箭头背景均可拖动，Previous/Next 不可拖动；owner WndProc 在拖动中不得等待可能正同步等待该 owner 的呈现锁。
- [x] PPT 数值页码不运行内容转换，且从 COM 状态变化到 Draw3-ready 发布不再包含固定 `500ms + 200ms` 延迟。
- [x] PPT Page 阈值内短按打开预览；箭头按下立即翻页，并严格遵守配置开关和 Pointer Down 快照的系统键盘 delay/rate，移出/抬起/cancel 后不再重复且迟到节拍不追赶。
- [x] PageControl 首次显示无需 Bar 外部消息即可完成渐显并在 deadline 后自行解锁；四窗禁用 Tablet 手势，primary/fallback 单触点转译、替换 cancel 与触摸长按路径通过 headless/静态回归。
- [x] 键盘与滚轮不再合成 Arrow pressed 闪按；真实 Pointer press 视觉保持，最近交互 PPT 窗口位于其他 PPT 窗口之上且始终低于 Bar。
- [x] Draw3 物理 contact 聚合只成对发布一次 Started/Ended，停止/异常路径不泄漏；Started 收起主栏次级界面并让第三鼠标光进入带 wait-for-leave 门禁的 `Dormant`，主栏展开状态、PageControl、第一光源和普通动画不受影响。
- [x] EndShow A2 使用主题白色且符合主栏线条/圆角风格的共享 `barEndShow` SVG；PPT 结束页时底部与两侧 Next 在同一稳定按钮上动画切换为该图标，恢复有效页时动画切回箭头，点击/长按行为不变。

## Out of Scope

- 不修改 EndShow A2 排列、业务回调、PPT/WPS COM 协议、Draw3 文档/画布换页、页级墨迹或设置 schema。
- 不为 Whiteboard 新增长按、滚轮、拖动、缩放、位置配置或页管理界面。
- 真实 PowerPoint/WPS、触摸拖动、DPI、光影观感和连续工作区切换保留为后续设备手工验收；本任务只允许静态检查、完整构建和无窗口测试。
