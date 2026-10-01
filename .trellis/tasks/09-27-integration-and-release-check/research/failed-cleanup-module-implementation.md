# E02 C-P2：已知失败清理模块接线交付

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。实施者：`failed_cleanup_module_impl`。

状态：**PATCH_READY / 源码冻结，尚未构建或运行 C-P2**。本交付只覆盖已审合同的模块接线；C-P1 primitive 的 Debug/十类 case GREEN 不作为本次模块接线的运行证据。

## 变更边界与设计身份

- 冻结合同 `failed-cleanup-lifetime-contract.md` SHA-256 `4A7EDE9C5C44AEF068F892BDC489B3203D270D1A10A8288CCEA577D16CCBF683`，独立 `failed-cleanup-lifetime-design-review.md` 为 GREEN_DESIGN；C-P1 `failed-cleanup-code-review.md` 为实码 GREEN。
- 接线前已读完整保存 hook、AGENTS、implement.jsonl、PRD/design/implement、上述合同/review/implementation、父最新 handoff/人工清单、native-desktop build/C++/错误资源/Window/Draw3/input、native quality/shared guides/CPU-GPU 合同；`get_context.py --mode packages` 为单仓库。
- 最小差距：已知 Window/Host/RTS/presenter 初始化失败后的日志、rollback、GPU/COM 释放、failed-start join/drain 原在 bool 返回前；Main 返回后启动监督无法覆盖它们。现将同一 owning Signal 传到真正 owner，失败先 Begin，再按原路径清理。
- 唯一写入八个约定模块源与本报告。Main/ShutdownSupervisor/新 helper/工程/公共测试/HiddenWindowTest/AutoSave/Controller/RuntimeMetrics/Bar/spec/父账本及原有 E01/E02 A/B/E03/U1/B1 编辑均由其原 owner 保持；本 worker 未写。未 commit/push/archive，未构建/运行 EXE/GUI。

## 冻结接口与所有权

| 接口 | 本次定义 |
| --- | --- |
| Window | `Service::Start(std::vector<WindowSpec>, Shutdown::FailedCleanupSignal failedCleanup = {})` |
| Host | `HostStartOptions.failedCleanup`，绘制 lambda `[this, failedCleanup]` 按值捕获 |
| RTS | `Initialize(HWND, ContactInputCoordinator&, DrawingCursorEventSink* = nullptr, Shutdown::FailedCleanupSignal failedCleanup = {})`，Impl 按值保存 |
| Presenter | `TransparentPresentationOptions.failedCleanup`；私有 `TryInitialize(mode, releaseSignal = {}, terminalOuterSignal = {})` |

六个 module 接口/实现均在 global module fragment include 普通 `FailedCleanupDeadline.h`，Host 通过普通头包含。模块没有 Main 栈 context、共享全局当前 scope、窗口/设备 owner 合并或自行管理 helper HANDLE。旧调用的默认空 Signal 不 Prepare、不增加 timer/monitor；动态 Window Create、Recover/Resize/Present 仍使用空默认值。

## 实际调用点与早退清单

### Window

- Start 将 Signal 按值捕获到两个 owner，再传 `RunGroup → CreateGroup → CreateStartupWindowFor/CreateWindowFor → RollbackCreation`。
- `RollbackCreation` 第一操作 Begin，覆盖 beforeCreate false/throw、RegisterClass 失败、Bind 失败与 created throw；CreateWindowEx 失败先锁存 GetLastError，再 Begin、诊断、rollback。beforeCreate catch 也立即 Begin。
- required owner 缺失与 required CreateWindowFor false 先 Begin 再返回；RunGroup 的 group false/catch 先 Begin 再 readyPromise/DestroyGroup/CoUninitialize。catch 将 created 置 false，避免 promise 异常后误入成功消息循环。
- Start 的 false ready 和 thread/promise 部分启动异常先 Begin，再 StopUnlocked 原 owner join；异常转换为 Start=false。StopUnlocked 的早退同时检查两个 event，确保 promise 创建异常时仍关闭已建立但没有 owner 的 event。
- optional role 只有确实进入创建且有效 Publisher 时建立局部 Dormant scope；返回 false 已完成 rollback，成功或可恢复失败均 Complete 真 join 后再下一 role。optional 未处理异常使 group 失败时，outer 继承 local tick 后再传播给 RunGroup/DestroyGroup。missing optional owner 未创建 scope。
- shutdown/running 拒绝、非法/重复 role 或非正尺寸属于创建 owner 前的早拒绝，保持原 bool 行为；Main 外层 false span 负责后续动作。普通消息 loop 没有激活 startup Signal。

### Host

- AttachExternal false 在附着标志复位前 Begin。
- 绘制 owner 的 graphics bool false、presenter false、首帧 lastPresentSucceeded=false、两种初始化 catch 都在下一个握手/失败日志前 Begin；共同 !initialized 钩子在 startupCompleted 前，随后 EndDrawingActivity/controller/presenter/renderer/device 释放仍受同 tick。
- presenter options 传入 owning Signal。RTS false/catch 在 stylusDecision 发布前 Begin，失败 join/drain/detach 分支再幂等 Begin。
- 绘制线程创建异常先 Begin，再原 worker drain/活动结束/解除附着/false；为保留旧 lambda 缩进，先构造按值 lambda，再在窄 try 中交给 jthread。
- 禁用 DWM、已运行/仍附着/无效 HWND 的入口早拒绝没有业务释放，Main false span 覆盖后续动作。正常 drawing Run/运行期 catch/Stop 未装 startup timer，正常保存排空顺序和等待保留。

### Presenter

- startup 每次模式 TryInitialize 前，只有有效 Publisher 才 Prepare 独立 Dormant release scope。
- disabled/API unavailable、ConfigureWindow/CreateSwapChain/Renderer.Init/InitializePresenter 返回失败后，先 local Begin；required/最后模式同时 outer Begin 继承该绝对 tick，再失败 cout/return。相关 GetLastError 在 Begin 前锁存，避免 SetEvent 改写诊断。
- 对应失败 ReleaseAttempt 和“failed; fallback to”诊断都在 local Armed 期间。只有 cleanup、失败诊断和 Complete 真 join 都返回，才下一普通 mode；成功 mode 的 Dormant 也真 Complete。
- 意外异常终止整个 startup 时 outer 先继承 local tick，再向 Host catch 传播；最后“All modes failed”输出前 outer 幂等 Begin。Recover 的 TryInitialize 默认仍为空。
- 本次没有改 FLIP_SEQUENTIAL、DComp→ULW 顺序、两个 DWM gate、像素/alpha/quality、Resize/Present 或运行期恢复逻辑。

### RTS

- 初始化入口保存 owning Signal。CoInitialize/CoCreate、HWND/all-tablets、最终 packet description、多点配置、plugin allocation/marshaler/Add/Enable 的终止失败，均在 LogHResult/trace/plugin Release/Shutdown/清业务指针前 Begin。
- 可恢复的 extended→required packet description、flicks/query diagnostics 不把普通后续初始化置于旧失败时钟；未改这些兼容语义。
- pluginAdded=true 且产品有有效 Publisher 时，Disable FAILED 在失败 Log 前 FailUnprovenProducerStop；Remove FAILED 在 Log、removedPlugin 临时引用 Release、pluginAdded 清除和任何 CloseAll/plugin/stylus Release/coordinator 清空前同样 noreturn。成功后保留 Cancelled Signal 的静态 Publisher，供后续正常停止的失败处理。
- 不带 Signal 的旧独立调用保持原默认行为。成功 Disable/Remove 的完整 callback in-flight 静止没有由本接线证明。

## 验证与准确边界

- 已逐项审八源实际 git diff、导出/实现签名、默认值、所有新调用点与终止早退。八源 `git diff --check` exit0，整体 `git diff --check` exit0；共享其他作者文档的 Git EOL 提示未作修改。
- 八源仅局部必要变更，合计 273 insertions / 77 deletions；没有全文件 EOL/编码变化。Window Start 的原局部块缩进随必要 try 增一层，其余现有格式保留。
- byte 检查严格 UTF-8、原 BOM 状态、纯 CRLF（无 lone CR/LF）均通过。六个 module 的 normal helper include 均在 module 声明前。
- 未执行 MSBuild、Headless/PptCOM/CLI、C03-C11、GUI/性能：按 root 的唯一 build/EXE/output 槽分工等待其完整 Solution 和真实夹具；本报告不预填 PASS。
- 尚未返回的 GraphicsInitialization/TryInitialize/renderer/RTS/COM/API、内层 helper 的日志/局部 COM 析构、异常到此 catch 前的栈展开和普通 cold start，仍为已审低层边界；不是全面 cold-start 截止。例如 CreateSwapChain 的内层 LogHResult 在该 helper bool 返回前，此最小接线没有扩成每个 HRESULT 的计时。
- 成功回退不误杀、真实 Window rollback/Host owner/RTS failure、正常 Armed render/Desktop/PPT 停滞与 fresh UInk/index Load、三个架构链接及 Win7 真机，都仍需独立对应证据。没有自然驱动卡死复现，不能宣称用户现场问题的唯一根因已修。
- 当前没有新增 module test gate 或普通产品配置/env fault；必要的 C03/C05 可复用 outer Prepared scope 的已审 Begin-CAS 测试门。后续 C04/C08 需要新门时须先冻结字段/鉴权与借用寿命，再另交源码身份。

## PATCH_READY 源码身份

| 文件 | Bytes / BOM / CRLF 行数 | SHA-256 |
| --- | --- | --- |
| `Inkeys/Inkeys/Window/Window.cppm` | 7236 / UTF-8 无 BOM / 213 | `AA08A3555FCECBC6EC24BE40D41ACD539444A94F71465202A180B727B4CE2101` |
| `Inkeys/Inkeys/Window/Window.cpp` | 74609 / UTF-8 无 BOM / 2239 | `43A242A00B37B98F1AFE29B91DE1A76A40B7F2CA652E255E5C7245A838E94FD3` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h` | 9824 / UTF-8 无 BOM / 285 | `C2EE47AF7198B26BA4BA8BFCC782300E46D1539E0180F1031083674F8CEF9B2E` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 83125 / UTF-8 无 BOM / 1716 | `60E84B339CB16B8A17AA72A6A62CC83E02A15CAE5513E3292ADFE9C0617FC595` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cppm` | 4558 / UTF-8 BOM / 120 | `567C222DAAB18B05A1F43DB420464F2CD67AFC0624C82C1D3EB20A0775EB055E` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | 109871 / UTF-8 BOM / 2752 | `895F98BDAA19847C608B859BA57A3F1D7F6C12ED0F2F2196F70C9ADF7CFFAC07` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cppm` | 4802 / UTF-8 BOM / 125 | `AC6EEFFF0770E5E86CAB02A4A7B664F39256833DC750201EA9F355FE6BD085C7` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | 46204 / UTF-8 BOM / 1197 | `091C68ABA549A98F9CB0913CCA93DAE4CAA162ACA87813FF3AADA47C7B75867B` |

八源自本次 PATCH_READY 起停写；root 可在此冻结身份上接 Main/测试门、独立源码复审并串行构建。后续源码变化必须重新记录身份并复验受影响路径。
