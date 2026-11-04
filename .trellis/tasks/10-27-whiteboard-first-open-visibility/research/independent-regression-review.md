# 独立静态复审：白板主回归最小修复

## 范围与结论

审阅当前冻结的 `Inkeys/IdtState.cpp`、`Inkeys/Inkeys/Window/Window.cpp`、`Window.ZOrderPolicy.h` 和 `InkeysHeadlessTests/window_tests.cpp`，对照本任务最新 PRD/design/implement 及研究记录。范围限定为白板后台相对排序、纯层级恢复早分流、同 epoch 进入准备去重及其生产 helper 测试。

在该范围内，最终版本未发现需要阻断合入的 P1/P2 产品缺陷。本结论是源码、差异、生产接线和测试设计的静态复核；不是修复后 GUI、任务栏或跨 Windows 版本实测结论。

本复审者没有修改产品、测试、spec 或 shader，没有运行编译、CLI、GUI 或进程/窗口/PDB 采样。仅按主会话授权新增本报告。完整构建和两个无窗口 CLI 由主会话统一执行并另行记录结果。

## Findings (fixed)

本复审者未直接修改代码。复审期间主会话与实现者已完成以下收窄，最终源码已复核：

- `Window.cpp:1906–1909,1953–1959`：原生 band 适配器现在从固定快照 identity 推导旧 root，仅该 root 显式使用 HWND_TOPMOST/HWND_NOTOPMOST；非 root 以该固定 root 继承 band，保留 NOACTIVATE/NOOWNERZORDER，不取 Service 的新一代 root。helper 中 root 的必要第二次带 NOOWNERZORDER 校正仍保留。
- `window_tests.cpp:859–870`：删除与 Ops 无连接的 hidden/transparent 旁置布尔断言，明确生产排序 helper 和 CommandCompletion 分别测试，Service 私有恢复/Retired 分派由静态接线复核。不能再把旁置布尔未变化写成真实安全覆盖回归已经执行。
- `window_tests.cpp:882–895`：新增 Freeze、Drawpad 保持同一 HWND 但 generation 更新的 owner 子树退休用例，检查旧子树被剔除、独立旧祖先保留且没有 owner/band/排序写入。

## 已核对的生产合同

### 1. 同 epoch UI 准备与退避

- `IdtState.cpp:145–168` 的 WhiteboardEntryAttempt 是生产及新 CLI 用例共用的私有 helper。preparedEpoch 与 retryAt 由 StateMonitoring 的单一局部实例持有，没有新增跨线程共享状态。
- `:2634–2647` 的 Inactive 进入分支在发布 Entering 之前检查 entryReady；`:2791–2801` 的 Exiting 反向进入分支也在原生 Enter 前检查同一状态。两处失败均以调用返回时的 steady_clock 时间计算 250ms，提前 runtime revision 唤醒不会重复提交。
- PrepareOnce 的记录不会因同 epoch 的 Inactive/Entering/Exiting 回退清空，因此失败重试不会重复 CollapseAuxiliaryPanels/CancelPointerCapture。新 epoch 跳过旧期限并重新准备；取消分支没有被 entryReady 挡住。
- `:2723–2735` 真正 Active→Exiting 的交互清理仍保留。真实 Draw3 首帧、输出/内容版本、背景提交和窗口事务门禁没有被该 helper 改成成功。
- `:2711–2715` 既有先 Active、后单次异步激活仍在，WhiteboardActivationGuard 仍拒绝退出和旧 epoch；未扩大背景 token/API/minimized 议题。
- `:1843–1877` 新用例直接调用上述 helper，覆盖连续失败、250ms 边界、同 epoch 再准备、新 epoch 和取消。`IdtMain.cpp:1447–1454` 的现有早退入口仍调用 RunStateModeProductionTest，不需启动产品窗口。

### 2. 白板有限相对排序与身份

- `Window.cpp:1978–1984` 仅在 WhiteboardWindowMode 分支使用新的相对排序；Desktop/PPT 正常刷新仍走原完整组政策与严格 continuous 检查，没有把普通刷新统一降为宽松排序。
- `Window.ZOrderPolicy.h:104–159` 最多两轮，保留真实 owner、band、owned 高于 owner、Bar 高于所有 PPT、PPT 目标子序列及 HWND/generation 核验。它没有以忽略隐藏 PPT 或只放宽 continuous 代替修复现场内部违约。
- anchor=Bar；仅对排在 anchor 上方的 PPT 进行 PlaceAfter，逐次写后重采样。无 Bar 时由所需首个 PPT 作锚，避免把 owned PPT 放在其 owner 下方。Promote 只调整目标在 PPT 子序列中的位置。
- `Window.cpp:1917–1964` 原生适配器在每次写入前后检查当前命令、running/shutdown、固定 HWND 和 generation；Read 后再次检查相同 identity。相对写入使用 NOMOVE/NOSIZE/NOACTIVATE/NOOWNERZORDER。
- 当前健康 owner/band 不产生 band/owner 写入；正常后台相对排序不执行 Bar 的全局 HWND_TOP。实际采样、API 或读回失败返回 false，不伪造就绪。

### 3. 纯层级快照、回滚与恢复责任

- `Window.cpp:2535–2539` 仅四类映射为 Z-only：RefreshTopmost、PromotePpt、SetOverlayTopmost、SetOverlayFullscreen。
- `:2556–2561` 捕获 GroupSnapshot 后直接返回，不填公共 Native 全状态快照；`:2608` 在公共 FRAMECHANGED/Show/CanvasPolicy 路径之前显式分流到 RestoreZOrderSnapshot。不是仅设置 canvas=false。
- `:2588–2602` 的 Z-only 恢复只恢复相应命令实际拥有的 topmost/fullscreen 政策及内部层级。Refresh/Promote 不写 mode/minimized/visibleBeforeMinimize；不恢复旧 RECT、显隐、iconic、region 或非 TOPMOST exstyle。
- PPT 次序从请求前快照 ordinal 提取，不从部分失败后的当前顺序重新取目标；仍强制 Bar>PPT，不刻意恢复非法的旧 Bar/PPT 前态。
- `RetainSurvivingZOrder:80–99` 按 owner 在前的顺序排除退休成员及旧子树。`Window.cpp:1931–1943` 的 surviving 采样掩去这些角色；固定旧 identity 不会改为 Service 的新 root，也不会直接写入已替换的成员。
- 恢复传入空原命令指针，原请求过期不会永久否决必要物理恢复；running/shutdown 与当前窗口身份检查仍存在。
- `RetryCommandRecovery:2667–2680` 仍只在实际恢复成功后解除唯一责任，原请求失败以 false 或 Retired 终结，不转成 Commit(true)。失败继续持有屏障，没有超时丢弃。
- Z-only 路径没有显示/输入样式写入，不撤销后到 Hide/穿透；原有安全清理入口、完整 Canvas/模式恢复及其单调安全覆盖保持。`ApplyOverlayFullscreen` 仍按当前模式、可见性、最小化、安全隐藏及 shutdown 发布最新绝对状态。

## Findings (not fixed)

没有在本轮限定改动中确认仍需修复的 P1/P2。以下属于明确验证边界，不记作已经动态覆盖：

- 新无窗口用例实际调用生产 ReconcileRelativeOverlayZOrder、CapturePptZOrder、RetainSurvivingZOrder，证明其决策、有限重试、顺序与身份处理。FakeOps 不模拟 USER32 的真实 owner/band 传播和前台许可；这些原生参数本次为静态审查。
- CommandCompletion 的过期/Retired 测试与排序 helper 测试相邻，但不是由真实私有 RetryCommandRecovery 驱动的完整 Service 事务。四类命令映射、Restore 早分流、零 Canvas/Show/FRAMECHANGED 写集及后到安全覆盖由源码接线审查支撑；不宣称已动态遍历全部私有 dispatch。
- `animation_tests.cpp:1792` 在 --no-window 下仍调用 RunWindowCommandCompletionTests；真正 RunWindowTests 仍由 `:1794` 跳过。没有借用被跳过的 HWND fixture 作为本次执行证据。
- 旧背景 ACK/token、最小化退出与成对 Bounds 等独立问题按最新设计不在本轮扩大；本报告没有重新将它们认定为现场主回归，也没有改其代码。
- 尚无修复后 GUI 或 Win7/Win8/Win8.1/Win10/Win11 各系统实机结果。用户禁止继续 GUI/运行时采样，不能由 CLI 或源码审阅外推这些结果。

## Verification

- 静态接线与边界复核：上述四文件完成，未发现本轮阻断问题。
- git diff --check：四文件通过，退出码 0。
- 编码/换行：IdtState 保留 UTF-8 BOM，其余三文件无 BOM；分别为 2941 / 3882 / 161 / 1961 个 CRLF，四文件均无裸 LF/CR。
- Lint / TypeCheck / 编译 / Tests：本复审者未执行。最终完整 solution 和 CLI 结果由主会话统一补记，不引用冻结前编译代替最终产物。
- GUI / HWND fixture / 运行时采样：未执行。
- 未改产品、测试、spec、CSO；只新增本报告，未提交 commit/push。

## 冻结源码 SHA-256

| 文件 | SHA-256 |
| --- | --- |
| Inkeys/IdtState.cpp | B1C920BDD054E701F926C55C2B98E5950411C4FAC2802C78127EABDF2AC0A1EA |
| Inkeys/Inkeys/Window/Window.cpp | 7A441E1556F3DD95F4F7400522A2A740B30B9EDE1E08CE5889FD7D448BD3832D |
| Inkeys/Inkeys/Window/Window.ZOrderPolicy.h | BD20A7CB429628CF73A8AC53AD6E590663E5EBD8BB8278B3D16B2DC5C60A2609 |
| InkeysHeadlessTests/window_tests.cpp | 0B1B975F022B7CF6755342526839E46EF793166ED8D32B017A2AAD0547950499 |
