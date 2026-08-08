# Windows Ink 绘制延迟回归调查与修复

## Goal

在 Windows Ink Pen 成功 Present 严格不超过 120 FPS 的前提下，消除当前版本相对 7 月 29 日基线出现的明显跟手性下降，以及墨迹追上笔尖时的跳跃/抽帧感。修复必须保持 RTS 接触回调非阻塞、Move latest-only，并保留 Mouse、Touch、Hover 和所有接触光标坐标来源。

## Background And Confirmed Facts

- 用户观察到当前 Windows Ink Pen 明显不跟手，追上笔尖时伴随抽帧；Mouse 和 Touch 正常。
- `0729a` 不是可解析 Git ref；7 月 29 日最后提交 `f76a35a` 作为可检查近似基线，用户原始构建作为最终体验基线。
- Pen `Packets` 的 cursor-before-Move 顺序由 `ec0f008` 在 7 月 25 日引入，0729 已存在，因此顺序本身不是本周新增回归。
- 0729 的活动等待使用 `WaitForWake(...)`，Pen cursor `ControlWake` 会提前结束等待并让绘制循环接近报点率。`8f1ee5a` 改为严格 `WaitForFrameDeadline(...)` 后，cursor wake 不再产生有效帧，但仍会唤醒 `THREAD_PRIORITY_ABOVE_NORMAL` 的绘制线程。
- 当前 RTS `Packets` 先更新 Pen cursor 并 `SetEvent`，再覆盖 latest Move。无效高优先级唤醒可能在 Move 发布前抢占 callback；若 120 Hz 帧边界命中该窗口，最新点会额外滞留一帧并在下一帧大跨度追赶。
- RTS 与 Windows 8+ `WM_POINTER` 都会发布 Pen cursor 坐标。部分工具（至少橡皮和倒转笔）在接触时显示光标，因此两个来源均必须保留；问题是接触样本不应再独立唤醒已按 120 Hz 运行的活动绘制线程。
- Move latest-only 是确认的目标设计：RTS 高频覆盖最新点，绘制线程每帧读取当前点，中间点允许丢弃；不增加历史队列或 270 Hz 模型消费。
- Hover 保持当前 RTS InAir + Pointer Update 行为。Win7 SP1 + KB2670838 继续依靠动态 API fallback 和 RTS 提供绘制/Hover。
- `74c33fc` state gate 仅在最小修复后仍复现且现有 trace 提供证据时另行调查，本补丁不修改。

## Requirements

- R1：RTS `Packets` 成功解码后先 `PublishMove`，再更新 Pen cursor mailbox；保持 last-packet、latest-only、无等待和无分配。
- R2：所有来源的有效 `inContact=true` Pen cursor sample 继续更新 mailbox 和必要状态，但不调用 `RequestDrawingCursorRender`，不产生每包 `ControlWake`。
- R3：Hover sample、Clear、Up/Leave、系统 cursor 刷新和首次 pointer authority 变化保持原唤醒/清理行为。
- R4：不修改 `WM_POINTER` 消息分支及坐标发布、`contact_input`、`WaitForFrameDeadline`、Stroke Modeler、prediction、压力/姿态、Mouse、Touch 或 Precision Touchpad。
- R5：不新增公共 API、类型、运行时诊断或测试专用接口；关键调度边界使用简短中文注释。
- R6：保持原文件编码与换行，并使用 ARM64 工具链构建完整解决方案。

## Acceptance Criteria

- [ ] `realtime_stylus.cpp::Packets` 的可见顺序为 Move -> Pen cursor -> diagnostics，callback 不增加等待、分配或额外 wake。
- [ ] `WindowController::PublishPenCursorSample` 对所有样本继续发布 mailbox；仅非接触样本请求 cursor render。
- [ ] `WM_POINTER` handler 未改变，接触橡皮、倒转笔及其他可见 Contact cursor 仍可读取最新坐标。
- [ ] Hover、Clear、Up/Leave、触觉和 authority 行为保持不变；Mouse、Touch、Precision Touchpad 无回归。
- [ ] 成功 Present 继续受 120 FPS deadline 约束，未恢复约 270 FPS 绘制。
- [ ] ARM64 Debug/Release 完整解决方案构建与两套测试程序通过。
- [ ] 真机 A/B 中额外拖尾和追赶抽帧消失，Contact cursor 与 Hover 正常。

## Out Of Scope

- 删除或限制 `WM_POINTER`/RTS 的接触坐标发布。
- Pen Move history/ring buffer、批量模型消费或解码 batch 全部 packet。
- Hover pacing、移动阈值、cursor 外观或 mailbox 重构。
- 修改 `74c33fc`、模型、prediction、笔宽、渲染器或 Presenter。
- 新增运行时 trace/counter；复用已有诊断即可。
