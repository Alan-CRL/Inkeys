# E03 初始化失败身份：实施与交接

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。负责人：`contact_failure_identity`。本单元仅修改 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`；Controller 写入权现在交还 root，后续 Draw3 metrics 写入者必须保留本修复。

## 修改与根因

- 基线 HEAD `e32a5fc06096c1e4ab88a29866c60ebe323fd722`。合法 RTS 串行 `A.Down → A.Up → B.Down` 后，consumer 迟到初始化 A 失败，原三分支按同 tablet/contact key Cancel 会取消另一 record 的 B，然后回收 A。
- 三个真实失败入口（扩展 stroke 获取失败、modeler.Reset、modeler.Update(kDown)）现在共用 `RejectStrokeInitialization`。最终 helper 只执行精确 `input.DiscardUntilTerminal(handle)`，不重查 key、不读新 record 的 Down、不伪 Cancel、不再额外 Recycle。
- 正在 Producing 的失败接触隔离到真实 Up/Cancel；Closing 交给 producer 延后单次回收；旧 ConsumerOwned 可直接回收，stale generation 无法修改新 route。
- Reset/Update 两分支的错误报告、cancelled、speed-eraser handback、handle 清空、inUse=false 和 return 原样保留。成功路径、模型频率/输入样本、宽色/pressure/cursor/history/GPU/保存未改。
- 在现 `RunParkedDesktopExitAutoSaveTest` CLI 中加入 `[Draw3InitRejection]`，实际测试共用生产 helper：I01 同 key 已接受新 B 与 stale 槽复用；I02 Producing 物理终态前占槽和重复拒收；I03 真实 Close CAS 后暂停 Up/Cancel、有界交权、单次释放。所有正常失败断言先 resume，再 join 全部引用夹具的线程。

## 分阶段证据

所有构建/CLI 由 root 串行执行，worker 未运行构建、CLI 或 GUI。以下原始日志已实际读取，不把源码阅读当动态 PASS。

| 阶段 | 配置 / 结果 | 原始证据（忽略目录 TestResults/release-hardening/） |
| --- | --- | --- |
| red harness | 完整 `InkeysRepo.sln Debug|ARM64` build exit0，0 Error / 10 个既有第三方 Warning | `e03-contact-identity-red-debug-arm64-build.log` |
| red 真实产品 CLI | `Inkeys.exe --draw3-parked-desktop-exit-test`，PID17248 自然 exit1；原 DesktopExit/FatalClosing/FatalActiveInk/LaserIgnoredTouch 四 tag PASS，I01 5 条身份/Move/Up/stale FAIL，I02 6 条提前释放/真实终态 FAIL，I03 无 FAIL | `e03-contact-identity-red-debug-arm64.stdout.log`、`.stderr.log` |
| green | 同配置完整 Solution build exit0，0 Error / 同 10 Warning | `e03-contact-identity-green-debug-arm64-build.log` |
| green 真实产品 CLI | 同参数，PID34784 自然 exit0；原四 tag 与新 shared-production `[Draw3InitRejection]` PASS，无 FAIL | `e03-contact-identity-green-debug-arm64.stdout.log`、`.stderr.log` |
| 严格无窗口 | root 执行 `InkeysHeadlessTests.exe --no-window` exit0；末段 `layouts=216 failures=0`、`PASS animation correctness` | `e03-contact-identity-green-debug-arm64-headless.log` |
| 独立实际复审 | GREEN；审真实 diff、三调用者、Coordinator、夹具寿命及 raw red/green；不只是复述本报告 | `research/contact-failure-identity-code-review.md` |

red 版本将三段旧逻辑真正提取为共享 helper、由真实三个调用者和 probe 共用；未提前改 Discard 或加屏蔽错误目标的新 guard。green 只替换该 helper 内的清理，原 probe 不改。I03 原先 F057 可能已通过，不把它包装成 E03 新 red→green。

## 冻结身份与静态检查

- 最终 Controller SHA-256：`57bc84e47c40e24a20fb246b178bc18ed7626319bb7a31ffe6a442f4e0ce6597`；Git blob：`07af3dd61ce85cf7bae500afa52087996db5d157`；CRC32：`fdfcd9e2`（仅辅助内容核对，安全身份使用 SHA-256）。
- UTF-8 BOM，CRLF 10979，裸 LF 0；无全文件格式变动；`git diff --check -- <Controller.cpp>` exit0。
- 总本单元 diff 为 195 行新增 / 16 行删除，多数为保留的正式回归夹具。保留未命名 const Down 引用仅作为 red→green 内部最小接缝，无参数复制/读取、无公开 ABI 变化；独立 reviewer 未要求审美性删参。

## 结论与保留范围

**本单元已实现、Debug ARM64 自动验证通过、独立实际复审通过。** 当前结论只覆盖三个真实失败调用者共用的路由拒收 helper 和身份/寿命合同。

- 未分别令真实 acquire/modeler.Reset/modeler.Update 返回错误；三个调用者的 runtime/session 清理是实际 diff/call-chain 静态证据，不写成三个完整失败分支动态通过。
- 普通 Controller::Run/PPT 页边界、成功 Present、旧页历史/UInk 与新页隔离、真实 RTS quiescence/Host Reset/Abort 等继续独立验证。
- 后续 Release 三架构/hidden 集成由 root 统一执行；本轮未提供这些新结果。Draw3 metrics 后续改同文件需重跑受影响 CLI/Headless 与独立复审，不沿用旧 PASS 冒称最终 HF。
- Win7 SP1 仅 KB2670838、Hardware/WARP/ULW FLIP、真 Touch/Pen、用户现场唯一根因及 HC/H2 体验仍未验证。
- 未改 Main/Host/Window/ContactInput/Settings/工程/spec/设备/gate；未 commit/push/切支/worktree/archive/结束任务。所有用户 PID/真实配置/Office 文档未动，未使用 computer-use。
