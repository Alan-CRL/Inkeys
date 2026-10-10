# F-038 活动 PPT 加载安装遗漏 retained 页

`DrawingController::materializePresentationSlot` 将 UInk `retainedCanvases` 放入 `DrawingDocumentSlot::retainedSlides`。当该 load completion 属于当前活动文稿且没有新 mutation，`Run` 把 `loaded` 的 document、pageRuntimeStates、fileGuid、revision 等安装到活动槽，却没有移动 `loaded->retainedSlides` 到 `activeRetainedSlides`；后续 `capturePresentationAutoSave` 和换槽都从空/旧 active map 读取，可能漏掉已恢复 retained 页。parked completion 直接移动整个 slot，可避免这一条特定遗漏。见独立 `fatal-and-ppt-review.md`；真实 Office/磁盘复现未执行。

最小修补为活动安装时把同一 `loaded->retainedSlides` 移入 `activeRetainedSlides`，不改 target/页身份、revision 或加载拒绝门。先抽出被生产 active completion 实际调用的窄安装 helper，显式无 HWND CLI 从含不同 Active/Retained GUID 和笔迹的生产 materialize 结果安装，再调用同一生产 PPT save builder 验证 retained 原样保留；旧路径红灯，新路径绿灯。必须覆盖 active load 已被新 mutation 拒绝安装时不覆盖用户新 retained 状态，以及 parked 原路径不受影响。Controller 实施者独占 `.cpp/.cppm`；主 agent 独占 CLI/构建，独立 reviewer 查实际调用链。真 Office 多文稿/结束页、旧污染文件不自动修复，仍需人工。
