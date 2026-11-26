# 设置窗口触摸与笔经 Pointer/HiMsg 路由导入 ImGui

## Goal

设置窗口的触摸与笔接触按主栏的输入框架处理：窗口过程消费接触阶段的 Pointer 消息并解码为值型样本，经设置自己的接触上下文裁决后，以带设备类型的事件导入 ImGui；HiMsg 通道过滤系统派生的兼容鼠标副本。由此支持触摸长按、笔操控，并让 ImGui 与后续滚动惯性能可靠区分设备。父任务：`11-26-setting-pointer-input`。

## Requirements

### R1 接触来源

- R1.1 Win8+（`GetPointerInfo` 可用，即主栏同一 `PointerMode` 判定）：设置客户区内的触摸/笔接触只由 `WM_POINTERDOWN/UPDATE/UP` 进入；该接触的 `WM_TOUCH`、带 Pointer 签名的兼容鼠标都是派生副本，必须丢弃，不能重复输入。
- R1.2 接触消息被消费后窗口过程直接返回 0，系统不再为该接触派生兼容鼠标与长按右键。
- R1.3 悬停（未接触）的 Pointer、`PT_MOUSE`、查询失败保持默认处理，与主栏相同。
- R1.4 Win7（无 Pointer API）：触摸由 `WM_TOUCH` 进入（设置窗口在该模式下 `RegisterTouchWindow`），带触摸签名的兼容鼠标丢弃；笔保持系统兼容鼠标路径（Win7 没有笔的 Pointer 消息），以 Pen 来源进入 ImGui。
- R1.5 非客户区（标题栏拖动区、标题栏按钮、缩放边框）不受影响：这些区域收到的是 `WM_NCPOINTER*` 或非客户区鼠标消息，保持现有系统行为。

### R2 接触裁决（单接触）

- R2.1 设置是单指针 UI：同一时刻只接受一个主接触；该接触未结束前，其他手指与笔的按下被吞掉、不进入 ImGui。
- R2.2 第二键、笔桶键（及非主按钮）按下吞掉，不产生右键语义（与主栏相同）。
- R2.3 取消：系统 `POINTER_FLAG_CANCELED`、`WM_POINTERCAPTURECHANGED`、窗口隐藏/销毁、`WM_CANCELMODE`、DPI 变化、设置会话失效时，结束当前接触且不产生点击（ImGui 侧先把指针移出窗口再松开按钮）。
- R2.4 丢失终态保护：同一 pointerId 再次 Down、同设备出现新的主接触时撤销旧接触（与主栏规则一致），不留下永久按下。

### R3 导入 ImGui 与设备区分

- R3.1 每个接触事件在设置窗口线程、持有现有 `settingImguiMutex` 时写入 ImGui：先 `AddMouseSourceEvent(TouchScreen | Pen)`，再 `AddMousePosEvent`（客户区物理像素）与 `AddMouseButtonEvent(Left)`。
- R3.2 触摸抬起或取消后 `AddMousePosEvent(-FLT_MAX, -FLT_MAX)`，不留下 hover；笔抬起后保持在原位，后续悬停由兼容鼠标继续更新（用户决定：保留笔悬停 hover）。
- R3.3 鼠标路径完全不变，`io.MouseSource` 仍由 backend 按消息标为 Mouse。
- R3.4 现有按设备分支（组件起拖阈值鼠标 4 / 触摸笔 10 DIP、触摸/笔 0.45 s 长按抓起、只对鼠标的 hover 动效、`ComponentEditor` 来源变化取消）依据新的 `io.MouseSource` 正确工作。

### R4 长按与笔操控

- R4.1 触摸/笔按住不再被系统长按打断或转右键；ImGui 侧按 `MouseDownDuration` 的长按逻辑（组件卡片抓起）正常工作。
- R4.2 笔尖接触等同主按钮，笔可点击、拖动控件与滑块、长按抓起卡片。

### R5 HiMsg

- R5.1 设置窗口启用 HiMsg 绑定（与主栏一样经 Window Service 的 `messageCallback`），回调负责过滤派生兼容鼠标：PointerMode 下带 Pointer 签名的按钮消息与接触期移动消息返回 Handled（不进入 ImGui）；笔悬停的移动消息放行（R3.2）；Win7 下只过滤触摸签名。
- R5.2 设置没有 HiMsg 队列消费者，绑定不得在通道里积压消息（入队过滤为空）。

### R6 样本轨迹（供 scroll-inertia）

- R6.1 对外提供当前/最近接触的有界样本轨迹：来源、Down/Move/Up/Cancel、客户区坐标、输入时间戳（Pointer `PerformanceCount` 优先，回退 `dwTime`/消息时间）、接触代次。
- R6.2 渲染线程可无阻塞地读取自上次读取后的新样本；溢出时丢最旧并标记，不阻塞窗口线程。

## 约束

- 设置窗口在 setting owner 线程，五窗触摸路由（`Input/UiTouchRouting.h` / `UiPointerInput.cpp` 的登记表）属于 overlay 线程且无锁：不得把设置登记进五窗表，也不得跨线程访问它。
- 复用主栏的 Pointer 解码与规则，主栏/分页行为、日志、`ui_pointer_tests` 不回归；不修改 HiMsg 库。
- 兼容 Win7 SP1 + KB2670838；`.cpp/.cppm/.h` UTF-8 BOM + CRLF；最小修改。

## Acceptance Criteria

- [ ] 本机触屏：任意设置页点击、拖动滑块、切换开关正常；长按无右键；组件卡片长按抓起、拖到预览放下正常。
- [ ] 本机笔：点击、拖动、长按抓起正常；笔桶键不产生右键；悬停有 hover 高亮，移开后 hover 消失。
- [ ] 触摸抬起后无残留 hover；鼠标行为与改动前一致。
- [ ] 两指同时操作只有第一指生效，不卡住按下状态；按住时隐藏/切走设置窗口不残留按下。
- [ ] 标题栏触摸拖动、标题栏按钮、缩放边框可用。
- [ ] `InkeysHeadlessTests` 中 `ui_pointer_tests` 与新增设置接触裁决测试通过；`InkeysRepo.sln` Debug|ARM64 构建通过。
- [ ] Win7 回退路径登记到父任务实机验证项。
