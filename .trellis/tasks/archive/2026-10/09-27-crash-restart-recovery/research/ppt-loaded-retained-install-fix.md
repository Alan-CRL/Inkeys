# F-038：活动 PPT 加载安装 retained map

日期：2026-09-28。当前单元只改 `Draw3.DrawingController.cpp/.cppm`；主 agent 独占显式无窗口 CLI 接线和串行构建。没有改 parked completion、PPT UInk schema/worker、Clear/Rebind、窗口或设备路径。

## 修补与行为

- 将生产 `materializePresentationSlot` 的页/笔迹/EndScreen/retained 构造主体原样提为同翻译单元 `MaterializePresentationSlot(snapshot,target,revision,allocateToken)`；原绘制线程 lambda 调用该函数，沿用现有 raster token 分配器。它把 UInk active canvases 建成 `InkCanvasCollection + CanvasRuntimeHistory`，把 `retainedCanvases` 放入返回的 `DrawingDocumentSlot::retainedSlides`。
- 活动 load completion 的旧安装语句提为共用 `InstallLoadedActivePresentationSlot(loaded,latestTarget,activeRefs)`。仅当 loaded 存在且当前 `activePresentationMutationRevision==0` 时，按原顺序安装 document、runtime、current page、target、file/revisions；现在同时把 `loaded->retainedSlides` 移入活动 retained map。迟到加载若已发生新 mutation 则整个安装仍被拒绝，既有文档和 retained map 不被覆写。parked completion 仍整体移动 `DrawingDocumentSlot`，不经过此 helper。
- 后续活动保存沿 F-031 的生产 `BuildPresentationSaveRequest` 接收 `activeRetainedSlides`。本修补让该 map 与刚安装的文档/历史/页身份同源，避免再保存或停放时漏掉已恢复的 retained 页。

## 红绿证据

- 新 `RunPresentationLoadedRetainedInstallTest() noexcept` 不创建 HWND、不读配置、不写文件。它用生产 PPT 快照构造与 `MaterializePresentationSlot` 建一页 active 笔迹、独立 EndScreen 与 SlideID=102 的 retained 页，走**同一生产活动安装 helper**，再调用生产 `BuildPresentationSaveRequest` 检查 workspace/page GUID 与两组笔迹；另以 mutation=9 验证迟到加载不会覆盖当前 retained 状态。Stage 1 保留旧遗漏：完整 `InkeysRepo.sln Debug|ARM64` Build 退出 0，同一 CLI 退出 1，唯一红断言是 `post-load production save keeps retained page GUID and ink`；原始日志在忽略目录 `TestResults/release-hardening/draw3-loaded-retained-red-build-debug-arm64.log` 与 `draw3-loaded-retained-red-debug-arm64.stderr.log`。
- Stage 2 只在安装 helper 的通过分支增加一处 retained map 移动；测试不改。主 agent 的完整 Debug|ARM64 Solution Build 退出 0、同 CLI 退出 0/PASS；受影响的 F-031、F-029、Laser、SuperTop 五个无窗口入口均退出 0。绿构建日志为 `draw3-loaded-retained-green-build-debug-arm64.log`；具体进程输出由主 agent 的验证记录归档。
- `git diff --check` 通过；Controller 两文件继续保持原 UTF-8 BOM/CRLF。独立 reviewer 正检查 materialize→install→save 调用链，实施者自检不能替代独立结果。

## 未验证与保留边界

- CLI 以隔离的合法形状快照进入生产 materialize，但没有经实际 Office、`PresentationAutoSaveService::SubmitLoad`/磁盘索引/严格导入到 completion 的整条链。真实多文稿切换、加载迟到、SlideID 重合、结束页、文件故障和 Win7 SP1+仅 KB2670838 需各自证据；本机 ARM64 Debug 构建不能外推这些组合。
- 旧版本若已经漏存或错存 retained 页，本次不会凭 SlideID 猜测补回或修改既有用户 UInk。F-026 致命图形恢复仅对已完成文档尝试入队，活动 contact 和 worker durable 终态依旧另列风险；本修补不改变 FLIP 或 DComp→ULW、双 DWM 禁用合同。
