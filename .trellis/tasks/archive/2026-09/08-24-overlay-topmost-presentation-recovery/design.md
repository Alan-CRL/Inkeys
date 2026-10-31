# 技术设计：非白板覆盖层置顶与呈现失败收敛

## Architecture And Invariants

覆盖层继续遵守单根 owner 树：

```text
MagnifierHost (overlay root, 唯一 topmost 操作点)
└─ Freeze
   ├─ DrawpadPresentation
   └─ Drawpad
      ├─ PptBottomLeft
      ├─ PptBottomRight
      ├─ PptMiddleLeft
      ├─ PptMiddleRight
      └─ Bar
```

`MagnifierChild` 是 `MagnifierHost` 的 child HWND；`Setting` 是 ownerless 普通应用窗口。实现不得改变这两个边界。

核心不变量：

- 外部层级恢复只操作 `MagnifierHost`；owned popup 由 Win32 随根整体进入 topmost band。
- 树内层级只使用 `HWND_TOP`/相对 sibling 插入位置，Bar 保持在 PPT 控件上方。
- 白板模式继续通过现有 `overlayTopmost_ && !whiteboardWindowMode_` 判定切换 `HWND_NOTOPMOST`。

## State Flow

### PPT 可见性转换

1. `Inkeys::UI::Ppt::PublishPresentationVisible()` 在锁内比较旧值与新值。
2. `false -> true` 时设置 `topmostRefreshPending`，随后按原路径发布 PageControl 与 Bar 状态。
3. 发布后若 pending，调用 Window Service 的 `RequestTopmostRefresh()`。
4. 成功时清除 pending；失败时保留，下一次 500ms PPT 状态发布继续重试。
5. 收到 `visible == false` 时清除 pending，避免离开放映后继续抬升。

刷新放在 Ppt 聚合层而不是四个 PageControl surface 内，因此一次放映进入只触发一个根请求，不会让四个 sibling 各自置顶。隐藏 HWND 仍属于 owner 树，本机探针已经验证在子窗口 Show 之前刷新根同样能够整体抬升。

### PageControl 呈现与窗口提交

`RenderSurface()` 保持两个阶段：

1. 在 render transaction 中完成 Scene/ULW present，并保留现有 `Success/Retry/DeviceLost` 分类。
2. present 成功后，在 presentation mutex 下提交 Win32 状态：
   - `shouldShow == true`: `SetBounds` 成功后再 `Show`；任一步失败返回 `FrameResult::Retry`。
   - `shouldShow == false`: `Hide` 失败返回 `FrameResult::Retry`。
   - 全部成功后，按 `keepAnimating` 返回 `Continue` 或 `Idle`。

目标状态和 Scene 状态不因失败回滚。RenderPipeline 已把 `Retry` 保留到下一帧，因此下一次回调会重新提交未收敛的窗口状态。

### Draw3 surface reconciliation

将 `ReconcileDraw3Presentation()` 的内部结果改为三态：

- `Waiting`: first frame、selection auxiliary target 或 revision 尚未就绪；等待 Draw3 runtime revision。
- `Applied`: `SetDrawpadSurfaceVisibility()` 成功，允许发布 drawpad ready 事实。
- `Retry`: Window Service 操作失败，必须在没有新 runtime revision 时继续尝试。

`ProductStateThread()` 保留 `WaitForProductRuntimeRevision(..., 250)`；当 runtime 变化或本地 `reconcilePending` 为真时执行 reconcile。`Retry` 保持 pending，`Applied` 清除，`Waiting` 依赖后续 Draw3 revision 唤醒。这样不增加新线程或新定时器，并避免成功状态下每 250ms 重复提交。

## Diagnostics

诊断沿失败发生的边界记录，避免跨线程读取已经失效的 `GetLastError()`：

- Window Service owner thread 在 `SetWindowPos` 等 Win32 调用返回失败的同一位置立即保存/输出 error code、命令和 HWND 基本状态。
- `TopWindow()` 在 `RequestTopmostRefresh()` 返回 false 时写一条结构化警告，并列出 overlay 根及关键 owner 节点的 HWND、owner、visible、topmost 和 bounds。
- PageControl 在窗口提交失败时记录 role、HWND、`shouldShow`、present status 与各操作结果；成功帧不输出。
- IdtState 在 Drawpad surface 提交失败时记录选择 surface、两个 Drawpad HWND 状态以及已有 `HostRuntimeSnapshot` 的 target/revision/first-frame/clean-frame 字段。

优先复用现有 `IDTLogger`（legacy/product 层）与模块内 `OutputDebugString` 失败通道；不引入新的日志框架。格式化辅助函数保持文件内私有，除非两个以上调用点确实共享同一结构。

## Compatibility And Migration

- 不新增配置项，不迁移持久化数据。
- 不改变公共 HWND 所有权或创建顺序。
- `PublishPresentationVisible` 的 ABI/签名保持 `void noexcept`；pending 是 Ppt 模块内部状态。
- Window Service 公开的 bool 操作合同保持不变；仅让现有失败被上层消费。
- Release 与 Debug 均保留失败诊断，但正常路径无逐帧开销。

## Trade-offs

- 选择“PPT 状态边沿刷新根”而不是“显示每个控件时刷新”，减少重复命令并维护根级所有权。
- 选择“保留目标状态并 Retry”而不是回滚 UI，因为失败通常是暂态窗口提交问题，回滚会制造额外闪烁和状态竞争。
- 不为测试引入 Window Service mock 接口；优先通过真实隐藏 HWND 验证 Win32 层级，通过已有调度器测试验证 Retry 合同。

## Rollback

改动按边界可独立回滚：Ppt pending 刷新、PageControl 返回值传播、IdtState reconciliation 三态、失败诊断和窗口测试互不改变持久化状态。若即时刷新出现兼容问题，可仅移除 Ppt pending 逻辑，周期根刷新仍保留。
