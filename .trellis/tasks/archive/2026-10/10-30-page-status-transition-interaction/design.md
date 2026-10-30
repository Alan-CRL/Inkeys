# 技术设计

## 状态与内容边界

- 在 `BarPageSurfaceContent` 增加显式的页码内容类型/状态标记，由 `BuildPageContent` 设置，不通过本地化字符串猜测。
- PPT `ppt.recovery.kind != None` 为状态内容，沿用现有 recovery 文案和重试门禁。
- Whiteboard `switching=true` 为状态内容，主槽显示新 `Loading`，次槽显示现有 `Wait`；加载结束或失败离开 switching 后回到真实稳定页码。
- 普通数字内容保持现有 `PageNumber`、总页数及 enabled/interactive 语义。

## 动画路径

- `BarPageSurface::UpdateContent` 按旧、新内容类型选择更新方式：旧新均为数字时调用 `SetStringImmediate`；任一则为状态时调用 `TransitionToString`。
- 主文字与 `pageTotal` 分别维护现有内容时间线，使用默认主栏时长、中点和缩放；不新增第二套 easing、时钟或绘制。
- `AdvanceBarButtonVisual(..., &pageTotal)` 和 `DrawBarButtonVisual(..., secondary)` 继续负责推进与绘制，保证 dirty、字体和光影走现有事务。
- 初次 Configure/布局重建仍按现有 direct 规则初始化，不人为播放一次状态动画。

## 交互路径

- 在共享按钮指针层实现与主栏一致的 Up 后 hover 抑制，可接收屏幕坐标；Up 记录抑制锚点并清除 pressed/hover，同坐标 Move 保持无 hover，真实坐标变化后解除。
- PageControl 在生产 Down/Move/Up 路径传入屏幕坐标，Leave/Cancel/新手势按共享规则复位。
- 保留 PageControl 的 PPT 长按、拖动、场次/页面身份门禁和业务回调；共享层只拥有通用按钮反馈，不接管业务。
- pressed 的释放继续由 `UpdateFeedback` 和 `RetargetBarButtonInteractionVisual` 驱动 `pressScale`，不额外新增视觉状态。

## i18n

- 在基准 `zh-CN.jsonc` 增加白板 Loading 文案，补齐 `zh-TW` 与 `en-US` 的短文案，复用 `CanvasRecovery.Wait`。
- 运行 `Scripts/i18n.ps1 sync` 生成快照和 key header，再运行 `check`；生成文件不手工编辑。

## 白板翻页立即收尾（追加）

- 翻页请求仍在当前工作区协调器串行发布，一次只保留一个页面事务；本迭代不扩展为多个冷页加载的 latest-wins 重定向。
- Host 接受 Whiteboard Next/Previous 后，立即以 `commandScenePending` 关闭新输入准入，并为页面命令标记 `sceneClosedAdmission`。这复用 PPT 的 `sealPresentationContacts`：已被绘制线程接受的当前笔按最后位置完成，旧物理 Move/Up 隔离到终态，不能写入新页。
- 暖页切换和冷页加载完成后的真实 Present 继续通过 `stageWorkspaceReady` 清除 pending 并重开输入；不得只改页码或提前开放。
- 第一页 Previous 等 no-op 不得遗留关闭的输入门；失败或无效命令必须有明确终态，不能永久等待。
- 保留冷页加载期间的完整性门禁、失败暖页和保存规则。立即的是当前墨迹收尾与首个页面事务启动，不是跳过目标页加载或允许新墨迹提前落入未就绪页面。

## 验证边界

- 状态构建：PPT recovery、白板 switching、普通数字的类型和文案。
- 真实 Scene：数字即时更新；进入/退出状态的主次文字中点替换；中点前后打断；同目标幂等；最终数字正确；动画禁用。
- 指针：Down/Up、同坐标 Move、真实移动、Leave/Cancel，以及 pressScale 释放；PPT 长按/拖动既有测试继续通过。
- 三语言真实 DWrite 槽位与共享按钮像素；完整 ARM64 构建、目标离屏测试和完整 no-window。
- 真实 GUI、PPT/Office、CPU 满载与触摸视觉不由 headless 代替，未运行时明确报告。
