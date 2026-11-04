# 修复显示刷新消息重入崩溃

## Goal

消除已由转储证实的 Display::Refresh 同线程消息重入加锁崩溃，保持现有显示快照、任务栏采样和订阅行为。

## Requirements

- 已授权：用户在本轮调查完成后明确要求进行修复。最小变更，不提交或推送，不进行 GUI 验证。
- 在显示模块入口处理 Shell 查询期间的重入；不只修改一个窗口消息调用方，不以递归锁或吞异常掩盖问题。
- 保留同步启动初始化、语义相同不增代次、内部锁外通知订阅者及 Shutdown 的现有合同。
- 嵌套刷新应延后/合并，不在消息重入中等待当前刷新锁；保持不同刷新原因的覆盖，避免丢失显示拓扑变化和无限重试。
- 旧版 PageControl 持锁 SetCapture 的路径在当前代码已改变，只核对现有路径，不顺带重构。
- 保留编码/换行，关键并发逻辑添加简短中文注释。

## Acceptance Criteria

- [x] 重入期间不会再次锁住同一非递归 mutex；补充能复现/验证生产路径的无 HWND 回归。
- [x] 刷新、订阅和退出清理合同不退化，回归覆盖必要边界。
- [x] InkeysRepo.sln Debug|ARM64 完整构建，InkeysHeadlessTests --no-window 验证；环境失败单独报告。
- [x] 检查最小 diff、编码和换行，无无关源码/构建配置改动，不创建 commit。

## Investigation evidence

5 份 20261021a/x64 转储：Bar 的 WM_SETTINGCHANGE -> Display::Refresh 持 refreshMutex -> SampleTaskbars/SHAppBarMessage -> 同线程 WM_SETTINGCHANGE -> Refresh 在 Display.cpp:1378 再锁并抛 std::system_error generic_category 36/EDEADLK。1 份 20261014a 同类异常源于旧 PageControl 持 renderTransactionMutex SetCapture -> WM_CAPTURECHANGED；当前 SetCapture 已在锁外。PDB GUID/Age 已逐份精确匹配，WinDbg 参数/调用栈已复核。
