# 一秒拖动中止修正审查

日期：2026-10-17。审查角色：独立 `trellis-check`。本轮只审查最新一秒中止差距，遵循 `timeout-correction.md`，不回退此前已批准的 HiMsg 点击与可选 Raw 补充实现。

## Findings (fixed)

没有需要审查者追加修改的源码问题；五个实现/测试文件保持冻结。

已核验实现者的修正：

- `UiPointerInput.cpp:173`：`MouseGesture::AcceptRaw` 对 Cancel 先拒绝早于本次原生按下的时间和不匹配的已绑定 Raw 代次。`generation=0` 只令 `rawDisabled_` 为真，不结束业务；只有实际拖动且 Cancel 带非零 Raw 归属才接受为业务 Cancel。原生 Move 在无 Raw 归属或 Raw 禁用时仍走 `AcceptMovement`，原生 Up 保持明确终态。
- `UiPointerInput.cpp:310`：`CancelState` 明确复制通道 `state.generation`。未绑定的 pending 状态为 0；已经绑定但消费者尚未消费 Down 时仍是非零。`Deliver`/Mailbox 溢出保存产生溢出样本的 Raw generation，因此丢失排队 Down 不会被误当成未绑定。
- `UiPointerInput.cpp:326`：pending 期限仍为 `>=1000ms`，只适用于 `armed`，没有延长/删除期限。绑定时清除 `armed`，实际静止按住不受该期限影响。超时只发布 Cancel，不产生 Up。
- `Bar.Interaction.cpp:679`：私有补充 Cancel 关闭 MouseChannel 后，仅在共享 `AcceptRaw` 返回 true 时调用 `MakeBarPointerCancel`，没有 `|| Dragging()` 旁路。忽略未绑定补充 Cancel 后继续读取 HiMsg，保持活动 ordinal、业务鼠标会话和正常按住/移动。
- `PageControl.cpp:2204`：私有 Cancel 同样只根据共享裁决取消业务；仅失败的 Raw 跟踪关闭，不重置仍有效的 Scene/session/拖动状态。原生 Move/Up 仍由 MouseGesture、Session 和原有拖动业务消费。

## Findings (not fixed)

没有阻塞当前修正的未处理源码发现。

本机真实管理员前台、触摸/笔、窗口外事件交付不在本轮无 GUI 审查的证据范围内。用户已经确认修正前版本能在其 UIPI 场景点击/开始拖动；本轮不能将静态审查或无窗口测试称为管理员前台超过一秒的现场验收。

## 实际调用链与保留的取消

旧行为把“业务已经进入 Dragging”当作“Raw 已取得归属”的证据。原生拖动能在 Raw 从未绑定时开始；1000ms pending 超时随后进入两个客户端的 Dragging 旁路，导致正常原生拖动固定时长中止。当前共享裁决改用 provider 的非零 Raw 代次作为归属证据，两个客户端不再自行扩大取消含义。

- Bar 桌面失败、来源接管、模式 revision、fold、显式取消序列/当前 ordinal、销毁仍从独立路径调用业务 Cancel；不依赖可选 Raw 是否绑定。
- PPT active timer 的桌面失效、workspace/session/layout epoch/锁定/显隐校验、跨来源接管、WM_CANCELMODE、销毁仍独立取消。未修改 pair latest-wins、直移、提交与持久化。
- 未绑定补充失败不产生正常 Up、点击、翻页或 FineDial 惯性。已绑定实际拖动明确传输失败仍以 Cancel 退出，保留原有取消清理。

## 回归用例审查

- `ui_pointer_tests.cpp:439`：直接调用实际 MouseChannel、MouseGesture 和 provider 测试时钟，覆盖没有 Raw Down、收到不匹配 Down 两条路径。原生先进入实际拖动；999/1000/1500/5000ms 核验 deadline 边界、唯一补充 Cancel、后续原生移动和真实 Up 唯一结束。
- 同一用例后段检查旧未绑定 Cancel 不会结束/禁用下一次拖动，以及绑定不同 Raw generation 的 Cancel 被拒绝、同代 Cancel 接受。
- `ui_pointer_tests.cpp:328`：实际 provider 失去 pre-begin 历史的 Overflow Cancel 为 0，不能结束实际原生拖动。
- `ui_pointer_tests.cpp:676`：实际 provider 已绑定后，在消费者尚未见到 Raw Down 前产生 InputUnavailable/Overflow；Cancel 仍为非零，实际拖动明确结束为 Cancel。
- 既有绑定候选/实际拖动 Cancel 矩阵、原生/Raw Up 去重、迟到终态、来源接管用例保留。
- `page_control_tests.cpp:117`：Background、Page、DragHandle 三类入口调用实际区域策略、MouseChannel、MouseGesture、Session，在同样四个时间点核验原生拖动继续以及真实松开仅结束一次。
- 上述时间点用例在旧共享策略无条件 `dragging_` 取消下，会在 1000ms 的 `!AcceptRaw && Active` 断言失败，后续原生 Move 断言也会失败；两个客户端实际接线另由本次源码审查确认，不把纯策略用例描述为 HWND 交付测试。

## Verification

- 静态差异检查：`git diff --ignore-submodules=all --check` 退出 0。
- 格式：五个本轮源码/测试文件均严格 UTF-8、无 BOM、CRLF，裸 LF/裸 CR 均为 0；BOM/EOL 与已记录基线一致。
- Lint：静态差异检查通过；未发现该 C++ 工程独立配置的 lint/type-check 命令，没有擅自引入工具或重格式化。
- TypeCheck / 编译：按父会话要求，审查者不并行启动 solution build。代码冻结已通知父会话，由父会话执行完整 `InkeysRepo.sln Debug|ARM64` 并登记实际退出码。
- Tests：本审查完成生产调用与回归断言的静态核验；最终二进制的 `--no-window` 与两个早退生产探针由父会话构建后执行，结果以任务 `validation.md` 为准。本审查没有用旧产物冒充新代码测试通过。
- 未启动 GUI、隐藏 HWND、浏览器或主程序；未修改 hook、安装/卸载调用、HiMsg、触摸、权限/激活策略、配置/工程，未提交或推送。

主会话最终执行补充：完整 InkeysRepo.sln Debug|ARM64 exit0（0errors、3条既有hashlib++警告、无新增warning）；正常环境 InkeysHeadlessTests --no-window exit0，state-mode与pen-width生产探针均exit0。详情见 ../validation.md 的 One-second drag correction。
