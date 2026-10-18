# F-031 Parked PPT retained slide 保存身份

## 已确认的当前调用链

`DrawingController::Run::buildPresentationSaveRequest(document,runtimes,target,...)` 对 active canvases 使用参数文档/页运行态，对 StableSlideId retained canvases 却读取闭包捕获的 `activeRetainedSlides`。`DrawingDocumentSlot` 自带 `retainedSlides`，`swapActiveDocument` 会随文档一起交换；`submitPresentationSlot` 保存 parked 文稿时传其 document/runtimes/target，但没有传其 retained map。当前调用包括正常/致命 Exit 的 parked loop，以及延迟的 PreviousInterval、binding migration completion。若 B 的 completion 在用户切到 A 后到达，提交 B 时可漏掉 B 的 retained 页，或将 A 的 retained 页按 B 的 workspace/file identity 写入请求。保存请求校验只检查 retained flag/slideId/extra，不校验这份 map 原本属于哪个文稿；UInk 导出会把请求 workspaceGuid 赋给所有 canvases。严格加载通常可能拒绝不属于 B 的 SlideID（导致恢复失败），数值重合时还有串页风险；实际文件/导入复现待红测。

## 修补与测试合同

- 让生产 `buildPresentationSaveRequest` 和 `submitPresentationSlot` 显式接收与 document/runtimes 同源的 `retainedSlides`。active 调用传 `activeRetainedSlides`，每个 parked 调用传对应 `slot.retainedSlides`；Rebind/Clear 仍传 active。不得从当前 target key、全局 active map 或最新 UI 状态推断来源。
- 值源必须同一 `DrawingDocumentSlot`：workspace GUID、active pages、retained page GUID/SlideID/笔迹及 mutation/queued revision 成组一致。Clear 与旧 UInk schema、worker 排队/索引原子替换不变；不得打开未发布的 PPT 入口。
- 显式无 HWND 生产 builder 回归先红再绿：文稿 A/B 不同 workspace/page/retained 点与交叉的 StableSlideId，active A、parked B 各建请求，断言 B 只含 B retained。再用隔离 PPT service/UInk 导出与严格加载路径验证 B 文件和身份，必要时覆盖 delayed completion。测试须调用同一生产 builder，不复制算法。
- Controller 实施者独占该文件；主 agent 唯一写 CLI/父账本与串行 MSBuild。独立 reviewer 查所有 builder/submit 调用点，不能只改 Exit 一处。真实 Office 多文稿/最后页/结束页仍需 GUI 人工。

状态：源码链 confirmed，磁盘串页未实测；F-026 当前最小修补不包含 F-031，修复前保持发布阻塞。
