# Native / UI implementation record

- 日期：2026-10-18
- 角色：dispatched trellis-implement，native/UI 独占范围；managed、规范、构建和产物由其他实施者/主会话负责。
- 实施已获用户批准；未启动 GUI/Office/Computer Use，未 commit/push，未改 COM ABI、两个 wire schema、依赖或工程配置。

## 已实现

1. 新会话的基本准入由 owner 的 Active + binding/show session revision 决定。HWND0 表示没有窗口证据，不再因 `IsWindow(nullptr)` 触发真实退出。
2. `ResolvePptControlPage` 同时核对本次 descriptor/current/total 与 shared raw。仅 metadata 不可用时允许 `Unavailable/Unknown` 保留的 fresh 页数用于 BasicControl；busy/不一致/无活动证据不能当作 fresh。正常墨迹仍须 Valid + 可解析目标 + 可靠窗口，并保持 Present/UI ack 门禁。
3. 基础分页模式清除旧 Draw3 Presentation target、失效旧 buffer，不将原始数字写成 `PptInfoStateBuffer` ready，不给新文稿复用旧页墨迹。确知结束页但缺拓扑时可保留基础结束控制，不伪造 EndScreen canvas。
4. 同场 busy 保留已呈现画面和旧数字，显式 Pending 且暂停写入；新 binding/show 的未知状态不能借旧 target。
5. `pptControlAvailable` 与 `pptWindowControlAvailable` 区分对象定向控制和 OS 窗口能力。无当前可靠窗口时不执行 Focus；自动接管 Office 笔要求当前墨迹目标已呈现，避免基础模式退出 Office 笔后无可用画布。
6. 旧可信窗口锚保留为销毁/复用检测依据；除 PID 外，能读到进程创建时间时也比较，检测 HWND+PID 复用。查询失败不猜测退出。当前 fresh descriptor 无窗口时现有控件回到默认锚；同场重新取得窗口只更新锚点/Bar 重绘，不重放场次进入或位置恢复。
7. `PageStateMode` 明确区分 Pending / BasicControl / InkReady。追加仅 UI 内部使用的 `pageAckEpoch`：页面、目标或 mode 变化推进，heartbeat 幂等；该值随真正捕获的 PageControl render snapshot、commit tracker 和内部回执传递。Pending→InkReady 即使数字和 target 完全相同，也拒绝旧 epoch 的帧。Ppt 验证后对 native 仍转发原四参回执。
8. Ppt 的 epoch 验证与纯值 native 回执在同一 stateMutex 临界区完成，避免验证旧 epoch 后脱锁、跨过一次暂停/恢复再迟到转发。该回执只访问 native/Draw3 状态，不调 Office；现有 PptInfo 在 UI 发布前已释放 pptSession 和 Draw3 API 锁。

## 修改文件

- `Inkeys/IdtPlug-in.cpp`
- `Inkeys/Inkeys/Business/PptSession.h` / `.cpp`
- `Inkeys/Inkeys/UI/Ppt/Ppt.cppm` / `.cpp`
- `Inkeys/Inkeys/UI/PageControl/PageControl.cppm` / `.cpp`（主会话为内部回执代次授权的窄扩展）
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`（仅 PublishPptSession 窗口锚更新函数，主会话授权）
- `InkeysHeadlessTests/ppt_session_tests.cpp`
- `InkeysHeadlessTests/ppt_ui_tests.cpp`
- `InkeysHeadlessTests/page_control_tests.cpp`

## 验证

- 先补相关生产 helper 红例再实现；没有启动并发 MSBuild。
- 新增真实 parser 输入：Active/Unknown + Unavailable + 空 metadata/HWND0 + fresh current/total 能得到基础页，不能 ResolvePresentationTarget。
- 覆盖 raw 不一致、busy 非 fresh、未知活动、新文稿/新场次、HWND0 非退出、有效窗口销毁/PID/进程创建时间复用。
- 覆盖 Basic/Pending 不转发墨迹 ack、同数字同 target 的 pause/recovery 旧帧拒绝、新帧齐备后成功、相同发布 epoch 幂等，以及现有结束页合同。
- `git diff --check`（全部 owned 源码/测试）通过。
- 与 HEAD 原始字节比较后确认 11 个源码/测试文件的 UTF-8 BOM 属性均保持，当前全部为 CRLF、无 lone LF。没有大面积编码/换行 diff。
- 构建和实际 headless 执行待主会话集中进行：完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window`。本报告不将未执行测试称为通过。

## 待验收/限制

- 需以本轮配套 managed DLL/TLB 构建后验证 native，不能用旧托管产物替代。
- 无 GUI/真实 Office 启动授权；PowerPoint 2007/2010、WPP2013、健康和异常设备、后台退出/再开下一文稿均保留 NOT VERIFIED，不能以 pure helper 或现代 Office 文件盘点代替。
- COM 完全不可达时不承诺基础控制。show identity / 事件 / 轮询 owner 的时序正确性由 managed 修复共同保证；原有 legacy Next/Previous COM ABI 没有新增 expected-session 参数。
- 同一 COM show/HWND 的结束重开若完全发生于观测间隙且无可观测变化，仍属于提供方可观测性限制。
