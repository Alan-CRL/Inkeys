# Windows Ink 绘制延迟最小修复设计

## Root Cause And Boundary

cursor-before-Move 在 0729 已存在。区别是旧 `WaitForWake` 会让 cursor wake 提前产生下一帧，而当前严格 `WaitForFrameDeadline` 只能消费事件后继续等待。接触 cursor 每包 `SetEvent` 因而从高频帧触发器变成无效的高优先级调度干扰，并且发生在 latest Move 发布之前。

严格 120 FPS deadline 与 latest-only 均是正确约束。最小修复只把接触 cursor 的“保存坐标”和“请求一帧”拆开，并优先发布模型所需 Move。

## Target Flow

```text
RTS contact callback
    -> decode batch last packet
    -> PublishMove(latest snapshot, no wake)
    -> PublishPenCursor(mailbox update, no contact wake)
    -> diagnostics
    -> return

Drawing thread
    -> existing 120 Hz WaitForFrameDeadline
    -> read latest Move and cursor mailbox
    -> existing model / render / Present
```

RTS 与 `WM_POINTER` 继续使用同一个 `PublishPenCursorSample`，两者的坐标均保留。统一规则为：

- `inContact=false`：发布 mailbox 后保持现有 `RequestDrawingCursorRender()`，Hover 按移动刷新。
- `inContact=true`：发布 mailbox，但不请求额外绘制 wake；Down、authority/haptic 等离散事件负责进入活动态，之后由 120 Hz 活动帧自然读取。
- Clear、Up/Leave 和 `QueueSystemCursorRefresh()` 保持原行为。

## Code Changes

1. `window_control.cpp`
   - `PublishPenCursorSample` 保持 authority、previous sample、mailbox publication 和系统 cursor 状态比较。
   - 仅在 `!sample.inContact` 时调用 `RequestDrawingCursorRender()`。
   - 不修改任何 `WM_POINTER` case。

2. `realtime_stylus.cpp`
   - 在 `Packets` 中把现有 `PublishPenCursor(...)` 移至 Move publication 之后、diagnostics 之前。
   - 无论 Move publication 是否成功，仍按原语义更新 Pen cursor。
   - 不修改 Down、Up、InAir、decoder、binding、state gate 或 interruption simulation 策略。

## Compatibility And Risk

- Contact Eraser/倒转笔仍显示 cursor，因为坐标 mailbox 持续更新；只取消冗余 wake。
- Hover、Win7 RTS fallback、Win8+ Pointer/haptics、Mouse 和 Touch 不变。
- 首次接触仍由现有 Down/authority/haptic wake 激活；Clear/Up/Leave 仍能清除旧 visual。
- 不增加测试接口。通过源码顺序核对、现有自动测试、完整构建及真机 A/B 验证。
- 若仍复现，下一步使用现有 RTS trace 单独判断 `74c33fc` gate reject；不在本补丁预防性处理。

## Rejected Expansions

- 停止 `WM_POINTER` 接触坐标：会不必要地改变可见 Contact cursor 来源。
- 新增 Pointer contact 判定或来源参数：扩大消息语义和接口面。
- Pen Move ring/history：违反确认的 latest-only 设计。
- 只交换顺序：保留无效高频 wake；只取消 wake：缺少 Move 优先的可见顺序保证。
- 新增 trace/counter 或测试专用 API：超过本次最小修复范围。
