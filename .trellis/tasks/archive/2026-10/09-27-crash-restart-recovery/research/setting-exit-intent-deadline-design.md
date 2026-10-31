# 设置页结束/重启意图的 15 秒保护起点

日期：2026-09-29。用户截图显示设置页退出按钮有 UI 反馈而程序未退出；截图无线程栈，不能认定本处为唯一现场根因。下述是当前代码可确定的时序缺口，关联 15 秒统一退场合同。

## 事实与行为差距

`Setting.cpp::RunSettingSession` 将 `CloseProgram`/`RestartProgram` 宏改写为 `QueueClose`/`QueueRestart`。设置页直接关闭/重启按钮先 `Setting::Hide()`，再把相应业务命令排入单个 FIFO `SettingBusinessQueue`；`Execute` 才调用真正的 `CloseProgram/RestartProgram`。同一 worker 还做配置/磁盘写、ShellExecute、提示框及 DDB 操作。若更早的 worker 任务长期不返回，按钮点击已被 UI 接受，但 `IdtMain::SetOffSignal` 未执行、`ArmShutdownSupervisor` 未启动，旧进程可以超过用户要求的 15 秒。`SettingBusinessQueue::Stop` 自身 `jthread::join` 也要由监督器保护。

## 最小合同

直接「关闭软件」「重启软件」点击属于已确认意图，应在该点击所在的设置渲染线程调用现有 `::CloseProgram()`/`::RestartProgram()`，使 `SetOffSignal(1/2)` 首次 CAS、`BeginShutdown` 与监督器在 worker 排队/I/O/join 之前发生。`SetOffSignal` 已在外部 helper 尝试前启动自身 deadline 兜底；后续业务清理仍可按原 FIFO 排空，超 15 秒按用户选择允许丢未 durable 请求。已有确认弹窗 `SettingBusinessKind::ConfirmRestart` 在用户确认前不能武断 Arm，用户取消仍无退场意图；其它设置写盘/更新动作保持原 worker 语义。

实施候选只改 `Setting.cpp` 中 `QueueClose`/`QueueRestart` 两个 wrapper，调用明确的全局 `::CloseProgram/::RestartProgram`，不改变按钮文本、布局、Update/HTTP、版本策略或关闭 prompt。需修正 `RunSettingSession` 旁旧注释，明确 I/O 仍排 worker 而确定退出意图不等。旧 `SettingBusinessKind::Close/Restart` 可保留为兼容/低风险，是否删除由最终 diff 审查决定，避免顺带清理。

## 验收与限制

- 静态追直接按钮→wrapper→`SetOffSignal`→独立监督器在 worker queue 前，确认所有其它直接退出/重启入口仍走正式函数；重复点击只由首次 CAS 决定意图，无第二 helper。
- 完整 `InkeysRepo.sln Debug|ARM64`，Headless `--no-window` 与 15 秒 suite；能安全隔离时在自建 GUI/config 根用脚本点击设置页自身按钮，精确记录旧/new PID、正式请求到强退时间及 Setting worker 被故障注入阻塞的测试边界。不能用仅 `CloseProgram` 的无 GUI suite 证明设置按钮已及时 Arm。
- GUI 输入脚本只操作本任务自建 HWND/PID；目标未命中时停止，不向其它程序发送点击。不开 computer-use。真 Win7/用户现场无 dump 仍是未验证。
