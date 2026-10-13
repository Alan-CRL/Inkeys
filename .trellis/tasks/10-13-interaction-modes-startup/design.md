# 本轮技术设计
- 目标是消除设置渲染时执行 SyncDraw3State 导致的跨线程等待循环，恢复下拉选择和独立信息说明卡片；全部自动启动判定及三语文案保留。
- 已确认链：RenderSettingFrame 持 settingImguiMutex 调用会话，选项调用 SyncDraw3State -> ApplySettingOwnerDesiredState -> Window::SetSettingOwnedByDrawpad -> Submit 的 future.get；设置 HWND WndProc 同时需要 settingImguiMutex，存在反向等待循环。
- 复用已有 Setting FIFO 业务调度，把需要同步等待 HWND 的绘图状态应用从 ImGui 临界区移出；实施前确认所有相关等待及状态/保存时序，不扩展到不相关架构。
- 下拉框复用 Widgets::combo，实时取 PaintDevice；说明复用现有 PPT 提示卡片的图标、边框、内边距，自适应三语文字高度。
- 回归验证必须覆盖跨线程调度边界；之前仅替换 SyncDraw3State 为计数器的回放不足。禁止 GUI/Computer Use，不能声称已做现场GUI复现。

## 最终实现
- 全局 SyncDraw3State 入口增加 Setting::DeferDraw3SyncIfRendering guard；设置会话的两个 Resume 位置使用可嵌套/异常恢复的 thread_local RAII标记。guard将Sync投递到既有业务FIFO，业务worker执行原Sync。非render调用维持同步。
- 队列停止拒绝新任务时也不得锁内回退；接受的任务按既有Stop排空。配置写盘仍由生产者冻结快照，FIFO顺序保持。
- 审查发现延后worker与设置写入并发访问模式字段，最终使用既有可复制IdtAtomic<int>，Sync单次load，JSON显式load；不改配置键/数值/缺省。
