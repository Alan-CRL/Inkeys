# 技术设计

## 数据流与根因

RTS Touch Down 或 `WM_POINTERDOWN` → `WindowController` 的触摸通知 → 主光标样本/归属 → `DrawingController` 瞬态光标；窗口线程另外在 `WM_SETCURSOR`/私有刷新中决定系统箭头。Touch contact 圆环由绘制线程按活动 runtime 单独生成。

当前通知只清旧 Mouse 样本和记录时间屏障。持久 Pen owner/样本可继续产生主光标；选中工具不是 Eraser 时系统箭头也可继续显示。Touch Up 不应据此重新恢复旧光标。Pointer 提升的 Mouse 消息若没有被兼容签名识别，仍可能重新接管。

## 修改边界

- 在 `WindowController` 增加一个原子触摸视觉接管标志：Touch Down 设置（活动 Pan 已由真实 Mouse 接管时不重新抢占）；Touch Up/Cancel 不清除；可信的真实 Mouse/TouchPad 或新 Pen 样本清除。状态变化走已有绘制唤醒和系统光标刷新，不从窗口回调触碰 D3D。
- `CursorOwner()` 返回用于当前光标解析的有效归属。活动 Touch Pan 已由真实 Mouse 接管时继续优先返回 Mouse；否则触摸接管返回 Touch，从而让主自绘光标解析器不选择旧 Pen/Mouse 样本。持久 `cursorOwner_` 仍只记录 Pen/Mouse/Unknown；Pointer 事件更新持久值时必须读取该字段，不能把暂时的 Touch 值写回。
- `ShouldHideSystemDrawingCursor` 对有效 Touch 归属隐藏系统箭头；即使当前产品工具不是 Eraser 也适用。当前已有的 Eraser/Laser 与 Touch Pan 规则保留。
- Win8+ 动态解析 `GetCurrentInputMessageSource`，在处理 `WM_MOUSE*` 时拒绝来源为 Touch/Pen 的兼容消息；可识别的 Mouse/TouchPad 正常接管。Win7 或来源不可用时沿用现有兼容签名、消息时间屏障和 Pen fallback。

## 兼容与验证

不新增全局光标状态或持久配置。多指 RTS 计数仅表示接触生命周期；接管标志在首个 Touch Down 后保持，避免最后一指抬起让旧箭头回弹。真实笔悬停/鼠标新消息不等待定时器。纯逻辑回归测试覆盖有效归属、系统光标策略、消息来源过滤；现有隐藏 Host 测试覆盖触点可视化清理。硬件上的 Windows 7 和混合设备需要用户后续验证。
