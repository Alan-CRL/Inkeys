# F-029 Parked Desktop 退出保存事务

## 证据与最小可达轨迹

`Desktop 画一笔且未 Clear → SetPresentationTarget(PPT) 或 SetWorkspace(Whiteboard) → 直接正常退出`。当前 `DrawingController::Run` 在切工作区时仅捕获 Presentation，再将 Desktop 文档放入 `desktopSlot`；`PrepareExitAutoSave` 调用的 `captureDesktopAutoSave(Exit)` 用 `activeWorkspace` 判 eligibility，非 Desktop 直接返回，随后仅遍历 Presentation slots。因此当前未 Clear 的 Desktop 区间没有 Exit UInk 请求。已有 Clear 的旧 UInk/索引可仍有效，但不包含后续新笔迹；跨进程可见恢复仍属未开放入口，不能把两者混同。

## 设计与验收

- 在绘制线程的唯一 owner 内，让现有 Desktop 快照构建接受明确的源文档、页面索引与 runtime history。调用方先选活动 Desktop 或停放 `desktopSlot`，只对该 Desktop 源执行现有 policy/schema/worker 提交；不读取当前 PPT/Whiteboard 文档，不在 PPT 进入时同步复制或落盘。
- 保持 `saveSetting.enable`、空页、历史可见项、UInk 文件身份、索引原子提交及 `Host::Stop` 最终屏障语义。提交失败必须留可追踪失败，不能伪造 prepared/持久化成功；旧已提交索引仍有效。
- 先让同一生产源选择/快照 helper 的无 HWND CLI 在 parked Desktop 场景红灯，再最小修补转绿；覆盖活动 Desktop、PPT/Whiteboard parked Desktop、禁用保存、空页、Desktop 与 PPT page GUID/笔迹隔离。现有 DesktopAutoSaveService 独立测试复验文件/index 路径；真 Host/Office/退出中活动笔触仍需 GUI 人工。
- Controller/test 实施者独占 `Draw3.DrawingController.cpp/.cppm` 与指定 Draw3 无 HWND test 文件；主 agent 独占 `IdtMain.cpp` CLI 接线、父账本、构建。独立 reviewer 看最终 diff、恢复/跨工作区链。禁止并发构建和性能采样。

本问题为 F-029，来源是当前 H0 生产调用链静态确认；实际 Desktop→PPT→退出 GUI/磁盘复现未执行。
