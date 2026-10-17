# F4 独立检查记录

日期：2026-10-15。角色：`trellis-check`；产品代码审查范围只包括 F4 的五个归属文件，最终追加父任务授权的 F1 GUI/全范围文档证据交叉复核。用户已批准实施，本检查不重新设置 planning 门，不创建 commit/push。初轮交接时的待验证清单保留为历史记录，当前结果以本文末尾的产品 GUI、解锁默认套件和最终 Verification 为准。

## Findings (fixed)

未发现需要本检查自修的机械或局部合同缺陷；没有修改产品源码或测试源码。仅新增本检查记录和忽略目录中的独立编译产物。

## Findings (not fixed)

当前五文件 diff 中未发现阻断问题。尚未解决/覆盖的项目如下：

- 默认完整 Headless HWND 套件解锁后仍退出1/11项失败。新增3项原有 root/continuous owner tree 顺序断言，加此前8项 Setting owner及相关前置断言；不能用独立捕获测试或完整产品 GUI 的通过结果覆盖为整套通过。预置顶 owner 对照在 HEAD 原版和当前实现均出现同一回滚条件，超出本轮 F1–F4，未改 Z序或断言；全部原始证据与因果边界见附录。
- 本机完整产品鼠标 F4 强触发、实际工具效果、重开及按住自然退出已通过；独立真实 `RunSettingCaptureTests()` 也通过。它们分别验证完整 Settings/ImGui 与 WindowService，不替代默认整套结果。`--no-window` 和 `--exit-visibility-gate-test` 均不运行新增捕获测试。
- Win7、真实物理触摸/笔和多屏均未由本检查验证；本项不证明原用户全部画布冻结已经解决。

## 实际审查的生产调用链

1. `Window::Service::Impl::HideSettingWindow` 仅从 Setting 的 Hide、HideAll 以及 Show 期间 shutdown 回退到达。`Submit` 将相应命令投递至 Setting owner thread；该线程内直接提交沿用原有 Execute 分支。捕获清理不是跨线程调用。
2. Helper 仅在 `GetCapture()==setting HWND` 时调用 `ReleaseCapture`，再次查询捕获后再隐藏并核对实际可见性；不依赖先前 visible 状态，重复 Hide 同样覆盖。其他 HWND 已持有捕获时不主动释放，不向 Draw3/RTS 发布 Cancel，也不改 owner/Z 序。
3. `WM_CAPTURECHANGED` 在逻辑 visible 检查和 `settingImguiMutex` 之前返回，不接触 ImGui context。`WM_SHOWWINDOW(FALSE)` 只在已有短状态锁内调用 `InvalidateSession`，在锁外 Request Settings；此路径没有拿 ImGui 锁、WindowService future 或等待 render。实际 `Scheduler::Request` 仅发布请求位并 SetEvent。
4. 原生隐藏通知只失效输入，不改 `visible_`，因此 FIFO 中迟到的 Hide 不会把较新的 Show 意图覆盖成 false。正常鼠标 Up 的 capture changed 不会触发会话重建。
5. SessionState 的隐藏失效由逻辑 Hide 或原生 Hide 锁存，Show 和 CommitEpoch 不清除；Resolve 对旧 session 产生 release，最终可见时同时 rebuild。旧 backend/context 仍只在 RenderPipeline 线程的真实 Drain 路径销毁。
6. Drain 不持有 state mutex；旧会话确实销毁后才能 Release 消费旧失效。确认无旧 context 的新建边界再 Release，随后新到的 Hide 不被本帧 CommitEpoch 清除。没有新增 owner→render 等待边或 vendor 修改。

## 竞态和回归覆盖复核

| 交错/边界 | 源码与测试结论 |
| --- | --- |
| Render 观察前 Hide→Show 合并 | invalidation 锁存；真实 SessionState 测试要求 release+rebuild |
| 重复 Hide | 单一 bool 失效保持幂等；Release 后稳定，无无意义循环重建 |
| 已 Resolve 后才 Hide→Show，再提交旧 CommitEpoch | CommitEpoch 仅写 epoch，新失效仍在，下一 Resolve 必须 release |
| 旧逻辑 Hide 已 Drain，新 context 已提交，FIFO 原生 Hide 才到达 | 原生通知重新 invalidates；保持最新 visible=true；下一轮再次 Drain/rebuild |
| 无旧 session、首次创建或创建失败后重试 | 在确认无旧 context 的新建边界消费过去 Hide；新建之后到达的 Hide 保留 |
| 无 context、旧 context 销毁、退出 | 新 owner 清理不依赖 context；原有 render Drain 和业务 FIFO 排空顺序保持 |
| Setting owned/unowned、隐藏后重复捕获、捕获转给 Bar | 新窗口测试走真实 Service/owner HWND；初轮待运行，后续独立真实入口通过，默认整套仍失败，详见附录 |
| ShowWindow 回调中 BeginShutdown、已关门后的 HideAll | 新窗口测试覆盖两个真实隐藏回退；后续独立真实入口通过，详见附录 |

Window 测试的 `SettingCaptureProbeProc` 不链接完整 Settings/ImGui，不能替代完整产品验收；状态测试也没有执行真实 HWND 隐藏通知，二者证明范围保持独立。

## Verification（初轮独立检查）

- Lint / 格式：通过本项 `git diff --check`；五个源文件均严格 UTF-8、无 BOM、纯 CRLF，无 lone LF/CR。未运行全仓格式化器。
- TypeCheck：独立 ARM64 原生 MSVC 编译生产 `Setting.SessionState.h` 及现有 `setting_session_state_tests.cpp` 成功，`/std:c++20 /EHsc /utf-8 /W4 /MTd /DUNICODE /D_UNICODE`，退出 0、无 warning。完整 Window/Setting module 编译待主会话，不能据此写成完整产品构建通过。
- Tests：调用原 `RunSettingSessionStateTests()` 的独立 console 入口运行退出 0；不复制状态机、不建立 HWND、不启动 GUI。此入口只存在忽略的 Build 目录，不加入产品或工程。
- Trellis：`python .trellis/scripts/task.py validate .trellis/tasks/10-15-canvas-f4-settings-capture-fix` 通过，implement 7 条、check 8 条。
- 证据：`Build/CanvasFreezeInvestigation/f4-review/session-state-build.log`、`session-state-build.exitcode`、`session-state-run.log`、`session-state-run.exitcode`，编译与运行退出码均为 0。构建输出仅在该独立目录，没有访问共享 MSBuild 缓存。

## 初轮交接清单（当前状态见后续附录）

1. 以动态发现的 ARM64 原生 MSBuild 按仓库既定命令构建完整 Solution，同 PowerShell invocation 规范化 PATH 并禁用 node reuse；不单独构建 Inkeys.vcxproj。
2. 运行 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`；另在授权桌面环境串行运行默认 `InkeysHeadlessTests.exe`，从输出确认 `PASS setting capture ownership`，不能只看旧 `--exit-visibility-gate-test`。
3. 在隔离产品副本重复 F4 强触发和正常关闭/重开/退出，采集实际 Settings/Bar TID、capture 和真实工具/笔迹效果，保存新二进制 hash。
4. 主会话最终规范同步建议在 `rendering-and-ui.md` 的 Setting/Window 合同记录：Settings owner 仅释放自己的捕获；捕获变化通知不取 ImGui 锁；逻辑/原生 Hide 的 session invalidation 不得被 Show/旧帧 CommitEpoch 擦除；纯状态、Service HWND 和完整 ImGui GUI 三种验证分别报告。本检查只拥有 F4 五文件和检查报告，未改 `.trellis/spec/`。

## 附录：默认 HWND 套件八项失败的 owner/Z 序追踪

主会话随后运行默认完整 Headless，原记录为退出 **1 / FAILED count=8**，位于 `D:/Project/Inkeys/Repo/Inkeys-draw/Build/CanvasFreezeInvestigation/fixes/regressions/headless-with-window.stderr.log`。五项旧 Window owner/Z 序断言失败，三项新增捕获用例的 owner 前置条件失败；这条完整套件结果保持失败，不能被下面的独立结果覆盖成通过。主会话另已根据真实 LockApp 前台窗口身份确定桌面锁屏；本检查没有向桌面发送输入，也不把 WTS 标志单独当作失败原因。

### 确认的原码失败路径

`SetSettingOwnedByDrawpad(true)` 先由 Setting owner 执行真实 owner 修改；随后在调用线程执行 `PrepareSettingZOrder`，根刷新完成后 `matches()` 还要求 Setting 位于两个 Drawpad 上方。层级不符时恢复原 owner 并返回 false。因此“attach 返回 false”不等于 SetWindowLongPtr 失败。原整套日志的首个 attach 失败前没有相应 Win32 command failed；立即根刷新计数断言通过，符合后置核验失败的路径，但原日志本身没有记录该次 `matches` 的逐项值。

为取得实际条件，在忽略的 Build 目录新建仅供观察的 `owner-validation-probe.cpp`，链接主会话刚构建的真实 Window/Message/FailedCleanupDeadline objects 和原 `window_tests.obj`，不复制生产 owner 算法。复用已有 `SetTopmostRefreshObserver`，在真实根刷新完成、`matches` 和回滚之前读取 owner、实际 WS_EX_TOPMOST、可见性与 Z 序。每个独立进程有 20 秒看门狗，只操作自己的屏外测试 HWND，正常结束时 Service 销毁全部窗口。

同一 Default 桌面的强对照如下：

| 条件 | 实际 owner 与样式 | 相对 Z 序/返回值 |
| --- | --- | --- |
| 当前版本，未预先刷新根，显示后 attach | owner=Drawpad；根与 Setting topmost=1 | Setting z=8，Drawpad z=10，Presentation z=11；`attached=1` |
| 当前版本，先刷新根为 topmost，再显示并 attach | 回滚前 owner=Drawpad；根与 Setting topmost=1，读取错误均0 | Setting z=46，Drawpad z=9，Presentation z=10；`SettingAboveDrawpads=false`，回滚后 `attached=0` |
| 当前版本，根预置顶且 Setting 隐藏时 attach | 回滚前 owner/样式同样符合 | Setting z=44，Drawpad z=9，Presentation z=10；`attached=0` |
| 当前版本，改为非 topmost 策略后 attach | owner=Drawpad；根与 Setting topmost=0 | `attachedNonTop=1` |

上述失败场景中，进一步在相应 HWND owner thread 直接调用相同 Win32 原语：`SetWindowLongPtrW(GWLP_HWNDPARENT)` 返回成功且即时 GetLastError=0，`GetWindow(GW_OWNER)` 等于请求的 Drawpad；根 `SetWindowPos(HWND_TOPMOST, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)` 也返回1/error=0，但相对层级仍未符合核验。这不是一个已取得非零 Win32 错误码的调用失败，具体失败的是服务读取到的窗口次序。

### HEAD 原版 A/B

按主会话授权，将 `git show HEAD:Inkeys/Inkeys/Window/Window.cpp` 的原始字节保存为 Build 下的 `Window.head.cpp`，以匹配当前 IFC 的原生 ARM64 MSVC 14.44 编译至独立 `Window.head.obj`，再用同一个探针 object 链接 `owner-validation-probe-head.exe`。`Window.cppm` 相对 HEAD 无差异；没有修改产品、IFC、工程或共享缓存。原版编译/链接退出 **0**。

同批、顺序、Default 桌面、WTS flags=0：

- 当前 Window：根预置顶后 `attached=0`，回滚前 owner正确、topmost=1，但 Setting z=46 / Drawpad z=9 / Presentation z=10。
- **HEAD 原版 Window：同样 `attached=0`，相同的 owner/样式与 z=46/9/10。**
- HEAD 原版 Window：未预置顶的 fresh root 下 `attached=1`；非 topmost 策略下 `attachedNonTop=1`。

这确认当前环境的这一 owner/Z 序核验失败在原版已有；本次 F4 Hide 修复不是它的引入点。预置顶对照在第一次 attach 前没有调用新增 Hide helper，也没有使用完整 Settings WndProc。尚未证明为什么该 OS/桌面状态会维持这一窗口次序，不能仅凭锁屏解释，更不能修改 Z 序策略或放宽断言来消除本次套件失败。

### 独立运行真实 F4 捕获用例

`owner-validation-probe.exe --capture-original` 直接调用刚构建的真实 `RunSettingCaptureTests()`，使用原 `window_tests.obj`，没有复制/改写该用例。Default 桌面、WTS flags=0，退出 **0**，stdout 明确包含 **`PASS setting capture ownership`**，stderr 没有 FAILED。它证明该独立调用的 WindowService 捕获合同通过；不等于整套 Headless 已通过，也不替代完整 Settings/ImGui GUI 强触发。

### 完整证据路径与退出码

所有下列文件基目录均为 **`D:/Project/Inkeys/Repo/Inkeys-draw/Build/CanvasFreezeInvestigation/f4-review/`**：

| 文件 | 内容/退出码 |
| --- | --- |
| `owner-validation-probe.cpp`、`owner-validation-probe.exe` | 仅观察现有生产事务的隔离探针；原 F4 用例通过参数调用 |
| `owner-validation-build.log`、`owner-validation-build.exitcode` | 当前探针最终编译/链接退出0 |
| `capture-original-default.stdout.log`、`capture-original-default.stderr.log`、`capture-original-default.exitcode` | 真实 RunSettingCaptureTests 单独运行退出0 / PASS |
| `owner-validation-default.stdout.log`、`.stderr.log`、`.exitcode` | 当前版本 fresh root 对照，运行退出0，`attached=1` |
| `owner-prepromoted-default.stdout.log`、`.stderr.log`、`.exitcode` | 当前版本预置顶/显示设置，运行退出0，**观测 `attached=0`** |
| `owner-hidden-prepromoted-default.stdout.log`、`.stderr.log`、`.exitcode` | 当前版本预置顶/隐藏设置，运行退出0，**观测 `attached=0`** |
| `Window.head.cpp`、`Window.head.obj`、`owner-validation-probe-head.exe` | git show 得到的原版实现和隔离产物 |
| `owner-validation-head-build.log`、`owner-validation-head-build.exitcode` | HEAD 原版编译/链接退出0 |
| `owner-current-prepromoted-ab.stdout.log`、`.stderr.log`、`.exitcode` | 同批 A：当前实现，运行退出0，**观测 `attached=0`** |
| `owner-head-prepromoted-ab.stdout.log`、`.stderr.log`、`.exitcode` | 同批 B：HEAD 实现，运行退出0，**观测 `attached=0`** |
| `owner-head-fresh-ab.stdout.log`、`.stderr.log`、`.exitcode` | HEAD fresh root 对照，运行退出0，`attached=1` |
| `owner-validation-ab-results.json`、`owner-validation-ab-source.json` | 同批运行结果、HEAD、原版/当前源码及未改接口 SHA256 |

观察探针的退出0只表示进程完成采样；表中的 `attached=0` 仍是明确观察到的失败，不是行为验收通过。其 HEAD 为 `5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb`，原版 Window.cpp SHA256 为 `122cebb731c0a705dcd2160471761fdad2d99cd5f00c05cac39e84470df90b7b`，Window.cppm SHA256 为 `6e5dd77e48e0117119993a614f03700f88fe586925be41a32099a523a27cddd2`。

原整套八次失败现场没有逐项后置核验采样，因此不把该独立 A/B 写成已经回溯了每条断言的每次 API。既有 owner 前置条件一旦回滚，后续“加入 owner 链”“位于两画布上方”“attached input queue 共享捕获”均不能独立归责于捕获清理。完整 GUI 及解锁后的整套复测仍由主会话保留；本检查未修复这个超出 F1–F4 的 Z 序问题，未放宽测试。

## 附录：解锁后的完整产品 F4 GUI 证据复核

主会话独占桌面与程序控制，使用已授权的原生程序化输入和隔离产品目录；本检查只读取已保存的 JSON/PNG/log，没有执行 GUI、构建或修改产品/测试源。以下是对本机 Windows 11 ARM64、真实鼠标路径的修复后验证，不外推物理触摸/笔、Win7、多屏或全部原用户现场。

产品 SHA256 为 **`4188e2eb39cca05f40ac68e1730cc85f343c1bfe122c1ee78c920a2dca075c7f`**，与主会话最终构建身份一致。ConsoleOutput 四项关闭，普通继承标准流诊断仍允许存在，因此不以 stdout/stderr 文件是否为空判定开关。

### 原强触发、真实工具效果及重开

证据目录：`D:/Project/Inkeys/Repo/Inkeys-draw/Build/CanvasFreezeInvestigation/fixes/gui-f4/`。PID **32436**，Setting HWND **0x50f80 / TID29956**，Bar HWND **0x60f5a / overlay TID12916**。

- `04-strong-trigger.json`：before-down 捕获0；held-down 时 Setting 和关联 overlay GUI thread 均查询到捕获 **0x50f80**；按住期间 Alt+F4 后、尚未正常 Up，Setting 已隐藏且捕获已为 **0**；正常 Up 后仍为0，主 Drawpad 取得焦点。不是仅凭 HWND 隐藏推断捕获释放。
- `01-baseline-ink.png → 05-actual-erasure.png`：同位置选择橡皮后原红线实际被擦除，不只显示工具高亮。独立读取原始2880×1920 RGB PNG，固定 ROI `(500,580,900,750)` 内强红像素由 **1271→138**，移除 **1133**，新增0；截图也可直接看到原线段消失。
- `06-before-new-pen.png → 06-new-pen.png`：切回软笔后真实新线出现。固定 ROI `(500,770,900,930)` 有 **1272** 个新增强红像素，不以旧墨迹或光标代替新绘制。
- `07-repeat-reopen-X.json`：普通重开第一次点击语言页有 `07-reopened-first-language-click.png` 的可见切页；普通 X 后 Setting 隐藏、capture=0；额外三轮均记录 Down 实际捕获该 Setting HWND，隐藏后捕获0。
- 首轮 `08-fast-reopen-first-home-click.png` 单独只证明最终位于 Home；若新 session 本来就是 Home，不能由这张图证明第一次点击成功。本检查已将该证据缺口反馈主会话，由下节的 Home→Language 对照补齐。GUI 的“快速”操作不证明恰好发生 Render 观察前合并；精确合并交错仍由真实 SessionState 确定性测试覆盖。

### Explorer/选择往返及 Clear/Undo

`09-explorer-selection.json` 中，原已有 Downloads HWND **0x6d0db0**；打开文件管理器后新增 This PC HWND **0x620d6e**，`created_and_closed` 只列这个新增 HWND。主会话仅请求关闭识别出的新窗，没有结束 Explorer 进程。`10-explorer-return-ink.png` 显示返回绘制后的新红线，`11-clear.png` 旧线全部清除，`12-undo.png` 原墨迹恢复；本检查已直接查看这些 PNG。上述一轮是附加正常路径回归，不是原始用户冻结的共同根因证明。

### 首轮超时必须保留

该 PID32436 的 `exit.json` 为 **exit=1、forced=true、reason=probe-time-limit、elapsed=600.188584s**。这是外部验证器达到自身上限后的清理，不是产品正常关闭，不能计入退出通过。此前已经采集的捕获/擦除/新线/重开结果保留各自证明范围。

### 快速重开首击与按住时自然退出的独立补测

证据目录：`D:/Project/Inkeys/Repo/Inkeys-draw/Build/CanvasFreezeInvestigation/fixes/gui-f4-exit/`。同一最终产品 SHA256，独立 PID **19092**。

- `01-fast-reopen-before-first-click.png` 显示快速重开后处于 **主页**；`02-first-language-click.png` 明确变为 **语言页**，补齐首次实际点击的可见结果，不以同页重复点击作正对照。
- `03-normal-exit-held.json`：Setting HWND **0x4d0bfa / TID13808** 可见且真实 capture=该 HWND；主会话在右键仍按住时，用左键实际点击 Settings“关闭软件”。进程 HANDLE **signaled=true、exit=0、退出请求到完成1.2342513s**。
- 同目录 runner `exit.json` 独立记录 **exit=0、forced=false、reason=normal-process-exit**；67.2298396s 是整个进程寿命，不是退出耗时。该结果未借助父进程强杀或15秒退出监督取得通过。

### 本次复核结论

修复后的完整产品鼠标路径已证明 F4 强触发后捕获在正常 Up 前即释放、同位置工具点击和真实擦除/新线继续、普通 X 和重开首击正常、快速重开有真实切页，以及仍捕获时正常退出。结合先前真实 WindowService 的其他 HWND 捕获保护和 SessionState 的精确竞争回归，本项未发现新的产品缺陷，无需修改代码。

锁屏时默认窗口整套 exit1及 HEAD A/B 仍是历史结果；解锁后的默认整套结果见下节，仍不计为通过。最终 F1 背压 GUI、全部任务 AC 和父验证报告由主会话统一更新，本检查不并发编辑父文档。

## 附录：解锁后默认窗口整套复测仍失败

主会话在确认桌面解锁后串行重跑默认 Headless HWND 套件，退出 **1**，耗时 **59.58s**，`FAILED count=11`。完整日志基目录：`D:/Project/Inkeys/Repo/Inkeys-draw/Build/CanvasFreezeInvestigation/fixes/regressions/unlocked/`，文件为 `headless-with-window.stdout.log`、`headless-with-window.stderr.log` 及同轮退出信息。本检查只读比较日志、源断言和 diff，没有另行启动窗口或构建。

与此前8项相比，原8项均保留；新增3项是 `InkeysHeadlessTests/window_tests.cpp:648–659` 中原有的下列断言，均在第一次 Setting attach 之前：

- `competing topmost starts above continuous owner tree`：owner 树连续且起始顺序位于 competitor 下方。
- `overlay root receives topmost style`：`RequestTopmostRefresh()` 返回后，根窗口应有 `WS_EX_TOPMOST`。
- `hidden root refresh raises continuous owner tree above competitor`：刷新后 owner 树应连续且整体高于 competitor。

三处均不在本次修改 diff 中，没有运行新增 F4 Hide helper 的必要条件。原5项 Setting owner/Z序及原3项 F4 fixture owner 前置断言继续失败；本轮没有新增 `hide releases setting capture`、重复 Hide、保留其他 HWND capture、HideAll 或 Show-shutdown 捕获释放断言失败。日志中的 `error=1400` 仍位于故意缺失 Setting/Drawpad HWND 的负例阶段，不能倒推为最早置顶/attach 失败的 Win32 错误码。

结论仅为：**完整默认窗口套件未通过，失败已在解锁后重现，不能归因于锁屏**。此前隔离 owner A/B 只确认一个预置顶条件在 HEAD 原版与当前实现均出现 attach 回滚；它没有逐项采样本轮全部11个失败，因此不扩大为本轮每次失败的已证根因。F4 单独真实 WindowService 用例 exit0，以及上节完整产品捕获/擦除/新墨迹/重开/自然退出证据，仍保留各自独立证明范围。不修复超出 F1–F4 的 Z序策略，不放宽断言。

## 附录：F1 完整产品 GUI 交叉复核

按父任务追加派工，仅只读检查 `D:/Project/Inkeys/Repo/Inkeys-draw/Build/CanvasFreezeInvestigation/fixes/gui-f1/` 的已保存 JSON、PNG 和输出文件。PID **9972**，相同最终产品 SHA256 **`4188e2eb39cca05f40ac68e1730cc85f343c1bfe122c1ee78c920a2dca075c7f`**；本检查未控制程序或桌面。

- `02-backpressure.json → 08-paused-ui-status.json` 的 elapsed **92.83→387.91s**，stdout/stderr 均 pause，读取计数固定为 **54548/9125**，stdout available 一直 **4168**；不是仅暂停标志而没有积压。
- 直接查看 `01-baseline.png → 02-paused-many-new-lines.png`、`03-paused-late-new-ink.png`，停读期间新红线持续出现；`04-paused-erasure.png` 原线段实际消失，`05-paused-clear.png` 全清，`06-paused-undo.png` 墨迹恢复，`07-paused-settings-language.png` 设置语言页响应，`08-paused-post-settings-ink.png` 关闭设置后又有新笔迹。
- `09-resume-and-drop.json`：恢复读取后 stdout 已读取 **241801**、stderr **20855** 且当次 available=0；生产输出器实际摘要 **full=11874**，证明队列曾满并按合同丢弃诊断。`09-resumed-new-ink.png` 继续出现新线。丢诊断不写成输入/墨迹丢失，也不把 lossy 日志当完整事件链。
- `11-paused-normal-exit.json`：主会话重新 pause 并形成 stdout **4149**、stderr **676** 字节积压后，Setting 仍真实 capture **0xb0f34**，实际点“关闭软件”；进程 signaled=true、exit=0，请求到退出 **0.9614759s**。runner `exit.json` 独立确认 **forced=false / normal-process-exit**，**577.5699905s** 是整段进程寿命；最终读取计数保持285421/23548，退出前仍 pause。

这补齐 F1 同一管道停读条件下的完整产品鼠标绘制/工具/设置/退出验收。stdout 实际满队列由生产 full 计数补强；GUI 这轮不声称 stderr 单端已满（仅见积压），该端满 pipe 和正常 Stop 的直接证明仍来自真实生产 CLI。没有把本结果外推 console/QuickEdit、物理触摸/笔、Win7、多屏或全部原用户现场。

## 最终 Verification 与文档交接

- Lint/格式：原五文件 `git diff --check` 和严格UTF8无BOM/CRLF检查通过；报告保持UTF8无BOM/CRLF，未运行全仓格式化。当前阶段只改本检查记录。
- TypeCheck/构建：初轮独立 SessionState 编译退出0；主会话最终 `InkeysRepo.sln / Debug | ARM64` 完整构建退出0，真实 Window/Setting modules已编译链接；本检查未重建共享缓存。
- Tests：真实 SessionState 独立运行0、主会话 `--no-window` 0；真实 `RunSettingCaptureTests` 单独运行0/PASS；完整产品 F4 鼠标强触发/擦除/新线/普通X/重开首击/按住正常退出及 F1 背压 GUI 的上述范围通过。**默认完整 HWND 套件仍1/11fail**，既有问题与尚未证明的部分均保留。
- Spec：新增 `canvas-freeze-boundaries.md` 的 F4 七节与已审实际实现一致；逻辑/native Hide、Release消费边界、早期消息及其他HWND捕获保护描述准确，未发现新的产品缺陷。
- 父文档交接：已请主会话统一更新父/子PRD、task状态及 `fix-validation.md` 的旧“锁屏待测/8fail”记录；并修正父design中“Cursor仍同步flush”的历史时态及“F3仅无deadline”的旧限制。本文不并发编辑父文档；最终是否完成由主会话按实际覆盖与遗留边界收束。
