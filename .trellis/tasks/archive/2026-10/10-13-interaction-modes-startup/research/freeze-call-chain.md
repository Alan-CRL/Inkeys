# 设置切换卡死：生产等待链
## 直接证据
- Setting.cpp::RenderSettingFrame 在 settingImguiMutex 锁内 Resume 会话；ImGuiWndProc 也用此锁更新 IO。
- 会话的模式选择和历史画布保存开关直接调用 SyncDraw3State。
- IdtState.cpp::SyncDraw3State 无条件发布 Setting owner 期望版本并调用 ApplySettingOwnerDesiredState，后者在 settingOwnerApplyMutex 内调用 Window Service::SetSettingOwnedByDrawpad。
- Window.cpp::Submit(Command) 给 Setting HWND 线程的命令设置 promise 并在发起线程 result.get() 等待完成；Setting HWND 线程如果正在 ImGuiWndProc 等待渲染线程的锁，就无法 drain 该命令。
- 反向边：RenderPipeline Settings渲染/ImGui锁 -> 等待Setting HWND线程 -> ImGuiWndProc等待ImGui锁。这不是Combo或RadioButton特有的问题；同步工作未完成导致整个共享UI渲染线程停住。
## 上轮测试遗漏
- 原无窗口回放把 SyncDraw3State/WriteSetting 替换为计数器，不执行Window Service等待，遗漏生产线程边界。不能以控件切换通过宣称卡死已解决。
- 更换单选控件没有消除同步等待；用户现场反馈证实问题持续。
## 修复边界
- 阻塞同步不能在设置渲染/ImGui临界区内执行。优先复用现有FIFO业务worker，保持配置/状态/保存顺序，并检查停止排空和所有Setting直接Sync调用。
- 恢复原下拉控件，仅上方说明改为现有PPT信息卡片风格。共享输入处理、owner线程所有权、自动启动判定保持。
## 回归要求
- 无窗口真实双线程/队列调度，渲染持锁时提交任务必须能返回并释放锁，worker允许等待但不能反向拖住render；超时不能卡测试进程。
- 对生产调度边界建立有效断言，避免测试中重写一份与生产无关的业务逻辑；应验证Sync/配置写盘FIFO顺序和停机排空。
