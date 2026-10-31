# 定格 Magnification 请求收敛与失败处理设计

## Verified Current Chain

- `Freeze::Toggle()` / 工作区状态更新用户期望状态。
- `FreezeFrameWindow()` 当前会直接 Show Host/Child、调用 `UpdateMagWindow()`、重绘、设置 alpha=255，并随后写 `RequestUpdateMagWindow=1`。
- `MagnifierThread()` 又会在 `RequestUpdateMagWindow` 或 `MagTransparency` 条件下重复完成同一组动作。
- `UpdateMagWindow()` 返回 `void`；过滤失败只能让该函数提前返回，调用者仍继续重绘、揭开窗口并更新本地显示状态；`MagSetWindowSource` 返回值未检查。
- 初始化过滤失败仍退出等待，而正常请求受 `magnificationReady` 限制，导致后续缺少可靠重试入口。
- `RequestUpdateMagWindow` 与 `magnificationReady` 是跨线程普通变量；Freeze 模块自身的 `freezeState` 已是 atomic，本轮只补充请求版本和通知，不重写无关状态逻辑。
- `IdtHistoricalDrawpad.cpp` 中旧请求代码位于注释块，不是活动调用路径。

## State and Notification

- Freeze 状态变更带单调递增版本；状态、工作区标志与版本必须作为同一次原子转换发布，使并发回调可以按版本丢弃迟到通知。
- Freeze 模块提供最小状态观察入口。观察回调只发布 `{revision, desiredActive}` 并唤醒协调线程，不执行 Win32 操作、不持有窗口或业务锁。
- Magnification 协调器维护最新期望请求、已处理版本和已应用版本。相同 active 值的新版本仍是新请求，避免开启→关闭→开启复用旧结果。
- 工作区切换会清除 Freeze active 并发布新版本；停止路径显式使全部请求失效并唤醒线程。
- 对当前请求失败时使用“仅当版本仍匹配才清除 active”的条件接口，禁止无条件 Toggle。

## Unique Execution Path

`MagnifierThread()`（或等价的唯一协调线程）是唯一可以完成 Magnifier 呈现事务的入口：

1. 取得该请求时刻 Window Service 中当前 Host/Child，并校验窗口、进程、父子关系和当前角色句柄。
2. 保持 Host 透明，按既有窗口线程命令显示必要 Host/Child，避免旧帧提前显露。
3. 从 Window Service 重建当前排除集合并提交 `MagSetWindowFilterList`。
4. 仅在过滤成功后提交一次 `MagSetWindowSource`。
5. 执行必要 invalidate/redraw；每个必要步骤均检查返回值。
6. 在最终揭开前和揭开后再次确认请求版本、active 状态、停止状态及 Host/Child 当前身份；过期则立即撤销本次呈现且不提交 applied 状态。
7. 仅当全部步骤成功时设置不透明并提交本版本已应用。

关闭、取消或失败清理只将 Magnifier Host 恢复透明并隐藏 Host/Child；不调用 `HideAllUserWindows`，不触碰 Bar/Drawpad、owner 链、topmost、焦点或任务栏规则。

`FreezeFrameWindow()` 保留 Freeze/PPT/Recall 的现有绘制职责，但移除所有 Magnifier source、显隐、alpha 和请求变量操作。

## Result and Failure Model

- 生产更新函数返回 `[[nodiscard]]` 的小型结果，至少区分：过期/停止、目标句柄、显示准备、过滤、source、invalidate/redraw、alpha/最终显示。
- 每一步失败立即停止成功链；过滤失败时 source 调用计数必须为零，source 失败时 alpha=255 调用计数必须为零。
- 失败清理本身也检查返回值；内部 visible/applied 状态只反映成功完成的窗口操作。
- 资源可用性与单次事务结果分离。初始化不再以一次过滤失败永久关闭后续请求；每个新有效开启版本均可重试一次。
- 记录 `{stage, requestRevision, host, child}`。Magnification API 的 `GetLastError` 若无明确文档保证，仅标记为即时辅助值；同一持续失败阶段只记录一次，后续恢复记录一次。

## Filter Set

固定角色顺序保持：

1. `MagnifierHost`
2. `Freeze`
3. `DrawpadPresentation`
4. `Drawpad`
5. `PptBottomLeft`
6. `PptBottomRight`
7. `PptMiddleLeft`
8. `PptMiddleRight`
9. `Bar`
10. `Setting`

候选仅在非空、仍为窗口、属于当前进程、不是 `WS_CHILD` 且未重复时加入。Magnifier Child 和 message-only DisplayObserver 不加入。每次 source 前重新构建，不缓存 HWND。

## Test Boundary

- 将请求选择、阶段执行、过期检查和结果提交提取为生产实际调用的小型内部边界；通过操作函数表/窄适配层注入 Win32/Magnification 步骤，测试使用同一算法。
- 将排除候选过滤保留为生产使用的可测试内部函数，通过窄 Win32 查询适配验证无效/跨进程/child/重复/Setting 缺失和句柄更新。
- 测试覆盖单次 source、过滤/source/显示失败、失败后恢复、开启→关闭→开启、执行中取消、迟到结果、工作区/停止失效以及内部状态不虚假收敛。
- 自动测试只能证明调度和 API 调用顺序；实际捕获像素、驱动行为与首帧正确性仍需人工视觉验收。

## Unchanged Contracts

- 不调用桌面 `SetOverlayFullscreen`，白板原有调用保持。
- 不改变 `RequestTopmostRefresh`、owner 链、激活样式、任务栏样式、捕获矩形和一像素策略。
- 不引入 D3D9/WDDM 推断、固定等待或第二捕获后端。
