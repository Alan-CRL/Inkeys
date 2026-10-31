# RTS 前窗口层级准备设计

## Boundary and cause

历史经验合同为 owner 链创建 → 根置顶 → RTS 注册 → 显示。当前为创建（exStyle 无 TOPMOST）→ StartProduct → stylus.Initialize/put_HWND/Enabled → 首帧/显隐 → 根刷新。遗漏的是 Host 启动前提，不是单纯周期刷新过慢。

产品协调层 IdtMain.cpp 为目标位置；Draw3 不承担全局窗口树置顶。预计产品修改以 IdtMain.cpp 为主；只有现有诊断复用不足才添加该文件内私有 helper。保持 Window Service 和 Host 公共接口。

## Proposed order

每一代窗口链：
1. StartWindowService 成功并刷新当前 HWND。
2. 私有 PrepareDraw3WindowTopmost helper 读取当前 root/Freeze/DrawpadPresentation/Drawpad，确认策略为桌面置顶且非 Whiteboard。
3. 调用同步 RequestTopmostRefresh（唯一根 HWND_TOPMOST 操作，SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE）。
4. 读取实际句柄、GW_OWNER 和 WS_EX_TOPMOST，检查 root 回退与现有 owner 链；检查刷新前后句柄仍同一代。API false、无效 HWND、链错误或缺少 topmost 均准备失败。
5. 准备成功才允许 StartProduct，随后 graphics、RTS、首帧照既有顺序推进。

root 使用 OverlayRoot，Mag 缺失时按照服务已有 Freeze 根回退；预期 owner 关系取当前生产合同（Freeze → DrawpadPresentation → Drawpad），不得复制旧版的兄弟画布关系。读取 HWND 属性用于验证，不在协调层直接执行 SetWindowPos 或独立更改节点样式。调用期间无窗口重建并发；失败诊断必须保留实际 root 和各 HWND 身份。

## Initial and fallback failure boundaries

在 StartDraw3ProductWithFallback 内两处 StartProduct 前都准备。首次准备失败在创建 firstHostCleanup/调用 Host 之前直接返回失败，走外层已有启动失败监督；禁止合并成 prepare && StartProduct 后进入 DComp fallback。

若 Host 真正初始化失败，现有 StopProduct/StopAndJoin/CompleteOrFatal 保持不变。成功重建 ULW 链后，对新 HWND 再准备，成功后才创建新 Host cleanup scope 和调用第二次 StartProduct。准备失败直接结束启动；不复用旧 HWND，也不继承上一次 deadline。

前置日志注明 initial/rebuilt-ulw、root、关键 HWND/owner/topmost 与失败类别。成功路径最多按启动代记录一次；不新增逐帧或周期日志。Win32 错误在对应调用线程捕获，不从主线程误读旧 GetLastError。

## Existing post-frame responsibilities

保留 IdtMain.cpp:3142-3163 observer、RequestTopmostRefresh、InitialTopmostRefresh 和失败清理。它们仍服务 Preview/首帧后层级维护；不能再被认为满足 RTS 前提。不移动或重命名原 milestone，不更改进度模板。

白板运行时仍由 !WhiteboardWindowMode 控制 NOTOPMOST；前置准备只约束当前正常产品启动，不在白板转换时强制回到 TOPMOST。不调用 Show，不重引入上一任务删除的分页窗口强制显示。

## API result vs observable state

SetWindowPos 非零只代表该次操作成功。非 topmost owner 可以拥有 topmost popup，因此 Bar 正常不能证明画布正常。检查属性/owner 的结果仍不等于视觉证明；真实 GUI 验证应记录 RTS 前后及故障现场的可见性、样式和 Z 序。微软未公开要求 RTS HWND 必须 topmost，本设计恢复项目既有经验合同。

## Compatibility and rollback

无配置、COM ABI、Host/Window Service 公共接口和依赖改变。API 都沿已有 Windows 基线。回滚只撤销 IdtMain 的两处前置准备及私有 helper；原首帧后/周期恢复保留。具体 RTS 系统原因及必要性用故障机器对照验证，未验证前不扩大系统结论。
