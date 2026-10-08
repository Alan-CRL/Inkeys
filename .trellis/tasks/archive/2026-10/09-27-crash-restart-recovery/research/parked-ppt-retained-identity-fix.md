# F-031：停放 PPT 文稿 retained 页来源修复

日期：2026-09-28。当前单元只改 `Draw3.DrawingController.cpp/.cppm`；主 agent 独占 `IdtMain.cpp` 的显式无窗口 CLI 接线。未改 UInk schema、Presentation worker、索引、PPT 入口、设备或窗口模式。

## 修改和合同

- 将原 `DrawingController::Run::buildPresentationSaveRequest` 的构造主体原样抽为同翻译单元 `BuildPresentationSaveRequest(document,runtimes,page,target,fileGuid,mutation,dpiScale,retainedSlides,clearPageGuid)`。活动画布、历史可见项、EndScreen、workspace/file/host 身份和 Clear ordinal 的构造顺序不变；retained map 现在是显式值源，不再隐式读取 `activeRetainedSlides`。
- `submitPresentationSlot` 同样显式接收 retained map。活动文稿保存和 Clear 传 `activeRetainedSlides`，稳定 SlideID 拓扑重绑的直接 builder 调用也传活动 map；正常/致命 Exit 的每个停放槽、迟到 `PreviousInterval` completion、旧 page-index→Stable binding migration completion 均通过 `RetainedSlidesForSave(active, parkedSlot)` 传对应 `slot.retainedSlides`。该 helper 在 parked 非空时只返回该槽自己的 map，避免不同文稿仅因 SlideID 数值相同而混用页 GUID/笔迹。
- 请求仍为绘制线程构造的纯 CPU 值快照，保留原 `ShouldQueuePresentationSave` revision 门槛和 Host worker 的持久化/索引事务；不在切文稿时新增请求、不读取真实配置、不做同步 I/O/GPU 调用。两种 DWM 透明模式仍禁用，`FLIP_SEQUENTIAL` 保持。

## 红绿证据

- 同一显式 `RunParkedPresentationRetainedSaveTest() noexcept` 无 HWND 入口调用**生产** `BuildPresentationSaveRequest` 与 `RetainedSlidesForSave`。A/B 具有不同 workspace GUID、active/EndScreen page GUID 与笔迹；两文稿 retained 页都使用 StableSlideId=102，但 page GUID/笔迹互异。Stage 1 的 selector 维持旧错误返回活动 A map：完整 `InkeysRepo.sln Debug|ARM64` Build 退出 0，CLI 退出 1，两个断言失败：B 请求混入 A retained；活动 retained 为空时 B retained 消失。原始日志位于忽略目录 `TestResults/release-hardening/draw3-ppt-retained-red-build-debug-arm64.log`、`draw3-ppt-retained-red-debug-arm64.stderr.log`。
- Stage 2 只把 selector 改为 `parked ? parked->retainedSlides : active`，测试不变。完整 Debug|ARM64 Solution Build 退出 0，同一 CLI 退出 0，stderr 为 `PASS: production A/B retained source identity`；日志 `draw3-ppt-retained-green-build-debug-arm64.log`、`draw3-ppt-retained-green-debug-arm64.stderr.log`。父任务随后运行 `--uink-presentation-only` 退出 0，并在沙箱外隔离数据运行完整 `inkStrokeModelerTestTests.exe` 退出 0；这些旧用例覆盖实际 PPT service/严格读取模块，却未把**本次 Controller 请求**直接送入 worker。
- `git diff --check` 通过；两个 Controller 文件仍为原 UTF-8 BOM/CRLF。独立 reviewer 尚需核全部 builder/submit 调用点与后续更改的交互；实施者自查不作独立复审。

## 剩余风险和未验证

- 无 HWND CLI 验证生产快照中的同槽身份，未验证真实 Office 多文稿快速切换、迟到 completion、最后页/EndScreen、磁盘失败以及 Win7 SP1+仅 KB2670838 的完整运行链。没有 GUI 授权时保持人工门禁，不能把本机 Debug 构建或旧 worker 用例写成端到端通过。
- `PresentationAutoSaveService::ValidatePresentationSaveRequest` 校验 retained marker/SlideID/extra，但没有独立来源证明；本修复在 Controller 侧消除已确认的错误值源。若较早版本已经写下错属页，当前修补不自动更改、删除或猜测修复该用户文件；严格导入可能拒绝未知 SlideID，数值与目标历史 ID 碰巧相同的旧文件仍需隔离样本诊断。
- F-026 只尝试为已完成文档排队保存，活动 contact 和 durable worker 结果另有发布验收；F-031 的正确快照不构成崩溃重启或跨进程可见恢复证明。
