# 白板存储独立复核

## 已修复
- DrawingController：SetWorkspace 返回桌面此前没有离开白板保存；现在在交换文档前调用同一安全点保存。
- WhiteboardStore / DrawingController：冷读回执携带请求 generation，拒绝离开再进入后的旧回执；内部 SetPage 也携带 generation，不能污染后来进入的文档。
- 冷读安装到切页命令消费之间继续持有 loading 闸门，并禁止保存完成回执卸载该目标，避免短窗口接受落笔或重新卸载加载页。
- 主题命令在冷加载期间拒绝。
- Host：白板页码、页数、主题、generation、loading、unsaved 和撤销/重做可用性在同锁快照中读取；workspace 更新与该锁同步。
- 正常退出逐页 drain 的 durable 结果经 observer 传回 Host；保存屏障明确记录 whiteboard_not_durable。保持已有 Desktop/PPT 的退出失败语义（日志并结束），未新增阻止退出协议。
- WindowControl / RealtimeStylus / PenCursor：RTS 所有实际 Down 经窄 sink 通知 Host 安装的异步 UI dismiss；WM_POINTERDOWN 同样通知，覆盖不提升鼠标消息的笔和触摸。
- 移除 Controller 对私有 RequestControlWake 的误调用，EnqueueCanvasCommand 已负责唤醒。
- 生产存储测试新增真实 current 索引提交失败/恢复与冷读 generation 原样回传。

## 验证与边界
- git diff --check 通过。
- 本代理按主会话要求不并发运行 MSBuild；完整 ARM64 solution、测试执行由主会话集中，结果以主任务日志为准。
- 100 页现有测试覆盖真实 worker/流式读写/CPU 页及 history 卸载；GPU 清理由生产切页调用核验，尚未执行无窗口 GPU 资源计数断言，不应声称 GPU 内存实测通过。
- current 索引失败后新候选 .uink 和临时 current-N.tmp 会残留；有效索引与已提交数据不受影响。持续故障可能增加磁盘占用，可在保留候选失败诊断的前提下另行决定清理策略。
- 不运行 GUI，真实 RTS/Pointer 与窗口菜单联动仍需人工验收。

## 后续补齐（源码已完成，等待统一构建运行）
- 白板完整快照补充 currentPageHasContent/contentRevision；每次发布从绘制线程实际页面更新内容状态和历史可用性后，再写 Host 同锁快照。
- 增加 InkHistoryGpuCache::Residency 只读资源计数；100 页生产测试使用 128×128 WARP composition swap chain（无 HWND，无 Present），每轮真实创建热前像与 composition node，先断言六项资源计数非零，再调用生产 Discard 入口断言全部归零。
- Undo/Composition 各限制一个 tile 的预算，避免默认大缓存造成不必要的资源开销。
- 此验证证明应用缓存持有的 COM 引用释放，不代表驱动即时返还显存；测试执行结果仍以主会话日志为准。
