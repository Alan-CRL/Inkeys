# F1 独立检查记录

日期：2026-10-15。检查对象为当前工作区补丁，基线 HEAD `5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb`。用户已批准实施 F1–F4；本检查仅负责 F1，不改动共享 Controller/PresentationAutoSave 的 F2/F3 行为。完整构建和运行验证由主会话串行执行，本记录不把静态检查写成运行通过。

## Findings (fixed)

- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp`
  - Issue: `LogFrameTiming` 在 stdout 不可用时仍取得 QPC 并格式化 Debug 帧日志，不符合无输出端时在格式化前退出的合同。
  - Fix: 在 `_DEBUG` 分支最前增加 `DiagnosticOutput::Available()` 早退及简短中文注释。修改后按字节核对原 BOM 状态和 CRLF，`git diff --check` 通过。
- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp`
  - Issue: 全局 `DrawpadMsgCallback` 的两处迁移日志使用未限定的 `DiagnosticOutput`，首次完整构建报告 C2653/C2065/C3861。扫描该轮完整日志，全部源码 error 均来自这两个站点。
  - Fix: 在回调内增加指向 `Inkeys::Drawing::Draw3::DiagnosticOutput` 的局部 namespace alias，保持原异常和默认消息处理。原 BOM 状态和 CRLF 保持；已交主会话增量完整构建复核。
- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.DiagnosticOutput.cpp`
  - Issue: 新 Session 的 `StartHandles` 在检查 `lifecycleBusy` 前阻塞取得 route 独占锁。若旧 Stop 因 route 读者未静止而隔离，后续拒绝启动仍可能等在旧锁上。当前产品使用单个串行主 owner，没有据此认定普通退出已经可复现死锁。
  - Fix: 在 `AcquireSRWLockExclusive` 前增加 acquire 读取的 `lifecycleBusy` 快拒绝，保留锁内复查。不扩大同一 Session 多 owner 并发合同，不启动第二 worker。原 BOM 状态和 CRLF 保持。
- File: `Inkeys/IdtMain.cpp`
  - Issue: 新早退 CLI 传入 `RunProductionTest` 的 helper lambda 未包 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS`。harness-off stub 虽然返回 2 且不执行 lambda，但主 TU 仍编译固定 helper 轨迹和测试环境变量接线，未满足发布边界中移除固定 fixture 的合同。
  - Fix: 严格 argv/count 检查后，用既有 harness 宏包住 helper lambda 和实际调用；off 分支直接返回 2。主会话完整构建退出 0 后才获准修改，完成后立即重新冻结，交统一增量构建。BOM/CRLF 按字节保持，`git diff --check` 通过。

## Findings (not fixed)

- 没有剩余待修的 F1 产品代码发现。未扩大同一 Session 多 owner 并发使用、全仓日志、专用 tail trace 或独立 debugger-only 路径。
- Spec sync: `.trellis/spec/native-desktop/input-and-ink.md` 的光标诊断合同仍写“绘制线程输出”，应更新为 try-only 生产者/独立输出 worker，并同步有界停止及丢失/序号解释。主会话拥有最终规范更新，等待新二进制运行结果后统一记录；本检查不并发修改规范或把既有调查结果记为修复通过。

## 核对范围与结论

- 已读取子任务 PRD/design/implement/check manifest、实施交接、父专项设计和两份调查报告，以及 manifest 中的 native spec。大型 `draw3-integration.md`（440 行）和 `input-and-ink.md`（674 行）均分块读到末尾；没有以截断注入代替完整规范。
- 审查实际生产路由、固定队列、writer、Start/Stop、格式化和所有 F1 callsite diff。生产者只 try route shared/queue exclusive，保护延续到 `SetEvent`；worker 在锁外输出并持有独立记录副本。Stop 只在 route 已解除且真实 thread signaled 后释放 state/event；取消间隙由重复 `CancelSynchronousIo` 与同一绝对 deadline 处理。超期 state 保留、关门且不建立第二 worker，没有发现正常路径的 event/state UAF。
- 逐项审阅 stdout/stderr 目的地、console/file/pipe 分流、部分写与 0 进度、坏端禁用、GetLastError 恢复、固定容量/截断、默认无 sink 无 worker、共享文件迁移和 UInk 保存边界。新接口没有逐记录动态字符串或同步输出 fallback；命名 `Line` 的生产用法没有跨越额外业务操作改变错误快照或输出生命周期。
- 残余标准流扫描中，Controller/ULW/Geometry 的 Run* 与 benchmark 报告属于测试；UInk/索引/RuntimeMetrics 的 WriteFile 是真实持久化；专用 tail trace 和独立 debugger-only 为已批准 deferred 范围。未发现本次已审计产品标准流旁路漏迁移。原标准流配对 debugger mirror 均随记录移到 worker。
- 主程序、`InkeysHeadlessTests`、`inkStrokeModelerTestTests` 三个实际直接消费者均登记同一 `DiagnosticOutput.cpp` 一次；五份相关项目/filters XML 可解析，没有复制实现或改动工具链。
- 真实生产自检代码覆盖正常双流/文件、满活管道、饱和、恢复/断管、初始化失败、生产者与 Stop、真实最后检查后写入间隙及注入隔离寿命。注入不可取消等待只证明隔离寿命，不冒充自然故障；测试报告输出由外部持续排空。实际是否通过须以主会话的新二进制运行记录为准。

## Verification

- Lint: **pass（静态适用范围）**。`git diff --check` 无输出；五份消费者 XML parse/单次登记通过。21 个引用新接口的已跟踪修改文件当前均为 CRLF，BOM 与 HEAD 一致。Git `core.autocrlf=true`，HEAD blob 的 LF 是规范化结果，不能凭 blob 推断修改前工作区 EOL；本检查四处自修另外保留了修改前后的原始字节证据。详情 `check-static-audit.json`。
- TypeCheck: 主会话 `solution-fixes-final.log` 对应完整 `Debug|ARM64` solution 已退出 **0**，覆盖 Product namespace 和 StartHandles 快拒绝修正。最后 IdtMain harness 编译门在其结束后才加入，**最终增量构建 pending 主会话**；不能以 implementer 先前 harness=0/1 的独立 `/Zs` 通过代替最后源码状态的完整编译结果。
- Tests: **pending 主会话运行**。本检查遵守统一串行 build/runtime 所有权，没有启动 GUI 或并行运行产品自检。最终须核新 CLI、坏参数、headless、同停读条件的完整产品新笔/Clear/工具/正常关闭及 exe SHA；普通 pipe 的 Stop 必须真实 Stopped，不能由 15 秒监督或父强杀替代。
- Coverage limits: 正常 console、QuickEdit、实际 `DRAW3_RTS_DIAGNOSTICS` 宏开启运行、真实物理 Touch/Pen、Win7 和多屏分别记实际证据；不能从匿名 pipe、静态扫描或无窗口测试外推。
