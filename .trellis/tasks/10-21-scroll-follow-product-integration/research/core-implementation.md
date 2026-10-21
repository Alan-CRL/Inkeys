# ScrollFollow Core 实施与交接

- 日期：2026-10-21；源码冻结：22:20 +08:00。
- 分支/提交基线：`feature/move` / `3ace726b`；没有 commit/push。
- 依赖：Capture 与 VisualMotionTracker 已由前序实施/check 完成，本阶段没有修改其生产源码或冻结 API。
- 本轮只执行无窗口或所有自建 HWND 全程不可见的验证，没有 Computer Use、普通产品启动、可见/前台测试，也没有运行旧 Freeze PrintWindow、Draw3 hidden/eraser 入口。
- 本文是实施记录，独立 check 尚未完成；允许的软件检查通过不能替代最终实屏验收。

## 文件与职责

新文件：

- `Inkeys/Inkeys/Business/ScrollFollow.h/.cpp`：会话控制、同代 Capture 消费、借用帧 Tracker、动作凭据与控制 worker、诊断。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ScrollFollow.h`：固定大小会话协议、身份、latest/final、单调终态与 Capture session 相关性纯函数。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ScrollFollowTest.h/.cpp`：无 HWND 协议回归。

修改：

- Draw3 `Bridge`：Begin 正常可靠命令、独立 End 固定槽与序号；原退出 final 槽保持独立。
- Draw3 `Host`：Begin 前预约 End wake；聚合准入、动作 hold、普通/shortcut 命令前置 End、显示/退出退休、窄入口与状态发布回执。
- Draw3 `DrawingController` / `WindowControl`：安全点基准、绝对 base-total、原恢复尾部、跨帧 End 阻挡、full-present 回执、CPU final、明确的不可见专测恢复预算。
- Draw3 `Product`：工作区/文稿/页面发布前退休；状态发布返回本次真正取得的 Bridge revision。
- `IdtState.cpp/.h`：独立临时 selection epoch、原 Publish/Reconcile/guard 与真实成功 ACK、setter 同步拒绝兜底、线程本地实际发布回执、投影生产回归。
- `Bar.Button.cpp`：新官方 More 注册白名单、Setting 前 explicitMore、最终 Selected/Disable、异步 toggle。
- `Bar.Interaction.cpp` / `Bar.KeyboardShortcuts.h`：完整原动作留原 owner 等 End；同工具分支前门、保留 haptic 凭据、原 shortcut pending/expected/lease/receipt。
- `IdtMain.cpp`：严格早期专测 CLI、唯一 DiagnosticOutput Session、两个诊断开关、早期正常/致命退出通知。
- 主工程/filters、Headless 工程/runner：登记新源码、资源、资源实现者的配置专测；协议纳入 `--no-window`。

UI 资源实现者独立拥有并已完成的文件是 `Bar.Button.cppm`、`Other.Config.cpp/.cppm`、`Other.Config.ViewAdjustTest.h/.cpp`、SVG/rc、三语源与生成 header/snapshot。本 Core 统一登记其源文件和严格 CLI，没有接管这些文件的实施。

## 实际接口与线程边界

`Inkeys::Business::ScrollFollow`：

- `State { Off, Initializing, Tracking, Paused, Stopping }`。
- `RequestToggle()`、`RequestStop(EndReason)`、`GetSnapshot()`、`Available()`；请求函数不等待 PW/owner/窗口完成。
- `PrepareAction(EndReason) -> ActionPreparation { ActionTicket, ActionState }`、`PollAction(ticket)`、`CompleteAction(ticket, publishedStateRevision=0)`、`CancelAction(ticket)`。
- 固定 64 个动作槽；id 在进程内单调。Off 的逻辑 hold 阻止新 Business Begin；已有 follow 的动作另持 Host 输入原因。执行原 setter 时不持服务锁，真实 bool 不改成 queued-success。
- `RetireScene(reason)`、`RequestShutdown()`、`RequestShutdownNoWait()`；最后一个只发单调原子标志并唤醒已经存在的控制 worker，不构造 worker、不取业务锁，供致命退场使用。
- `SetDiagnostics(enabled, verbose)`。

Host：`BeginFollow(session)`、`FollowChannel()`、`EndFollow(disposition)`、`WakeFollow()`、`HoldFollowAction(bool)`。普通命令保持原 `CommandResult`；shortcut 保持原具名 canvas receipt，没有把 ActionTicket 冒充 canvas receipt。

`Follow::Identity` 按值保存真实 workspace/page GUID、Host/scene/device/geometry 代与 client 尺寸。`Target` 保存 session、递增 sequence、identity 和累计内容 client px。Channel 的 Publish/Seal 共用短锁；可靠 End 自带被冻结的 final，latest 不是 FIFO。Begin/End 的 session 贯穿 Bridge/CanvasCommand；owner mutator、Presented/Handoff 以及 pending barrier 的续接核验 session。

Mag 回调独占当前 Context 的 Tracker。Initialize/Process 的 CPU 运算在服务锁和 Channel 短门之外；图像 span 不出回调，Context 由 shared lifetime 保活。Frame 还核验成功 Capture::Start 返回的 session。控制侧只有 `captureStarted && ownedCaptureSession == snapshot.session` 才解释捕获 reason/统计，因此上一轮 Freeze/Geometry/Unavailable 终态不会退休尚未 Start 的新会话。

启动先通过平台、非 WOW64、Desktop、Freeze、Mag ready/非 occupied 检查；Host 最终门还核 shortcut scope/lease/原输入原因。关闭新 Down 并沿原 seal/quarantine 收尾后，由 owner 取得基准。首 F0 前保留原 Capture 800ms 预热及总 5s 初始化窗口；有效 F0 初始化 Tracker 后才请求临时 selection。只有原 StateMonitoring 的真实 output-ready 和 WindowService 成对成功 ACK 才进入 Tracking。

运行新 Capture rejected/API failure/无有效发布或 owner clamp 会 Pause；普通视觉三连拒绝只由 Tracker 决定。Capture 的 GeometryChanged/FreezeActive/Shutdown/Unavailable 属生命周期退休。Paused 保最后可信位置，无自动恢复。

Tracker total 已是原图像素，按 Frame 的实际 sourcePixelsPerPixel 乘一次。当前 Capture identity=1，owner 直接使用 client/canvas px；没有 96/DPI 换算。owner 从基准减 total，再复用现有 `ApplyCanvasContentTranslationChecked`、SetViewport、热前像失效与预算 Tile 恢复。极大有限 double 先按原 viewport 硬限位裁剪，避免窄化成 float infinity 后被当成零位移。

正常 End 等指定 final 的 full present、recovery/refresh 结束、当前真实工具输出及 Window pair ACK，才释放会话输入原因。后继普通/shortcut 命令仍在原 FIFO 中；等待 End 时继续纯状态泵、终端回收和渲染，不继续执行后继命令。Shutdown/退休可以单调升级并跳过可见 ACK；同页仍有效时先提交最后可信 CPU viewport，再让原场景/退出保存屏障继续。

临时 selection 独立 epoch，不改真正 modeRevision/shortcutIntentEpoch。Bridge/Host/Product 的状态发布返回本次在原发布锁内取得的 revision，IdtState 在线程本地保存实际回执；Bar 原动作完成时把它交给 CompleteAction。控制侧等待同 projection、当前真实工具代和不早于该回执的真实窗口 ACK；取消/过期只归还自己的凭据及 hold。

## 构建与产物

复用现有 `Build/ScrollFollow/build-motion-tracker.ps1` wrapper，完整 `InkeysRepo.sln` / `Debug|ARM64`，vswhere 定位原生 ARM64 MSBuild，同 PowerShell 单一 Path 与禁 node reuse，normal host、10 分钟期限，没有修改 SDK、依赖、工具链或项目构建配置绕过错误。

最后成功：

- `motion-tracker-build-product-core-verification-*`，`BUILD_EXIT=0`，26.661 秒。
- 输出 `Build/ARM64/Debug/Inkeys.exe`：82,928,640 bytes；SHA-256 `B618641AE883AADE726B018EFD0894C112502CA2C5D9B39D05A9608CA0B7C8C2`。
- 输出 `Build/ARM64/Debug/InkeysHeadlessTests.exe`：24,245,248 bytes；SHA-256 `ED17B8B6AF6FECE1F6FF697F61874FC209FDFEADCE181625514497F6C720FCB8`。

早期两项局部 C++ 接线错误已修。另两次 native link.exe 在 `IncrBuildImage` 内 `C0000005/LNK1000`，日志在 `product-core-third` 与 `product-core-seal-test`；仅删除已核实工作区内的生成 `Build/ARM64/Debug/Inkeys.ilk` 后原配置重链成功。这是独立工具链增量缓存故障，没有源码/项目/SDK 规避。已有 unrelated hashlib/旧 controller 等 warning 未顺手清理。

## 实际执行结果

最终结果取 `Build/ScrollFollow/*-verification*`：

| 入口/检查 | 结果与边界 |
| --- | --- |
| `Inkeys.exe --scroll-follow-protocol-test` | exit 0，0 failures。session/seq/scene/nonfinite、latest 4096、final 竞争、End 必须应用/呈现/ACK、迟到 End/ACK、启动前取消/失败终态、Shutdown、满普通队列独立 End 与原 final、旧 Capture session 相关性。无 HWND。 |
| `InkeysHeadlessTests.exe --scroll-follow-only` | exit 0。同一生产协议/helper；也已纳入 `--no-window` runner。 |
| `Inkeys.exe --state-mode-production-test` | exit 0。原 State/PPT focus/shortcut business/FineDial 回归；新增实际临时投影进/出不改变真实工具、modeRevision 或 shortcutIntentEpoch。没有 Host/Window Service。 |
| `Inkeys.exe --bar-view-adjust-config-test` | exit 0，0 failures。真实 ReadAll/ReadMini/原子落盘、默认/缺项/未知插件及额外 JSON 字段保留、Visible、旧布局拆分、冻结复制。只用任务私有文件。 |
| `Inkeys.exe --scroll-follow-hidden-test` | exit 0。真实唯一 ULW Host/RTS、所有自建 HWND 不可见；细节见下。 |
| 新 CLI 坏参数 | 产品 12 形状 + Headless 3 形状全部 exit 2。包括 flag 错位、额外参数、重复 flag、`=bad` 后缀。产品 stdout/stderr 均为 0 byte，记录 `strict-core-argv-summary.log`。 |
| i18n check | exit 0，en-US/zh-TW 均 426/426。 |
| 格式/工程/XML | 20 个现有文件 UTF-8/BOM/CRLF 与 HEAD 格式保持，5 个新 Core 文件无 BOM/CRLF；XML/SVG 和唯一登记通过；`git diff --check --ignore-submodules=all -- Inkeys InkeysHeadlessTests Scripts` 通过。 |
| PE | ARM64 AA64；`core-verification-dependents.log` 无 OpenCV/FFmpeg/highgui 动态依赖，MAGNIFICATION 仍 delay-load。不是 Win7 运行证据。 |

直接对整个源码目录跑未加 ignore 的 git diff 遇到 `HiMsg` 子模块 `.git/modules/HiMsg` 权限错误；改用主树 `--ignore-submodules=all` 只检查主树。没有修改或宣称验证子模块。

本实现阶段**没有启动 full `--no-window`**；主会话安排由独立 check 在冻结后统一执行一次，避免重复离线 Tracker 全矩阵。共享 UInk/AutoSave 四组既有 CLI 基线已由主会话完成，本阶段没有重复执行，也没有把它们算成 follow 文件持久化证明。

## 不可见 Host 的真实覆盖

`--scroll-follow-hidden-test` 在配置、单实例、普通 HWND 初始化之前严格早退。复用原 metrics-smoke 的 MagnifierHost/Freeze/双 Drawpad 不可见模板，没有调用 SetDrawpadSurfaceVisibility、ShowWindow、前台或置顶。autoSaveRoot 为空，不读写用户配置/画布，也不声称落盘成功。

- 真正 mailbox 输入 A，owner 取得真实基准和 GUID；拒绝 session=0 的 Begin 归还其预约。
- 实际辅助 selection output ready，关闭 Down 后被拒接触直到真实 Up 回收。
- 累计目标 seq 1..200 的覆盖、重复/错 scene 拒绝；不先等 latest 消费就可靠 End。
- 原 Undo 可靠接纳在 End 后，最终 `viewport=(baseX+140, baseY-45)`；End 期间 Undo 计数不变、页身份/原历史可用性保持。
- 真实预算恢复限制为一 Tile/帧；最终本次记录 `end_frames=19`、`recovery_frames=17`。早轮 14/13、12/10 也通过，帧数受调度影响，不能当 FPS。
- 原工具实际 output ready 后，仅使用**明确标记的严格 ACK seam**，才释放后继真实 Undo；迟到目标拒绝。
- 新的真实 Down/Move 保持物理按下，再 Begin；accepted contact 被 seal 为 Stored，但 inputRecycled 未增加；收到真实 Up 才回收，原 redo 按新 Stroke 语义清除。
- `1e100/-1e100` 目标在真实 owner 得到 `-1048576/+1048576` 且显式 clamp；随后 final 有效目标仍从同一基准计算。
- Shutdown 的 final 进入原退出屏障前，真实 CPU viewport 已到最终值，未等待可见恢复 ACK。
- 创建/销毁及轮询检查均为 `IsWindowVisible=false`；最终日志全部 `visible=0`。

本测试没有运行实际 Mag Capture/Tracker，没有模拟实屏输入穿透，ACK seam 不能证明系统成对显隐、滚轮到底层或屏幕视觉无闪烁。

## 未验收项与 check/保存阶段交接

1. 真正 `Capture F0 -> Tracker -> Business Tracking -> 临时 selection -> Window pair ACK -> Stop/原工具 ACK` 整链没有在普通产品运行中执行。纯协议/原 State/隐藏 owner 测试不替代该链；控制 worker 全部初始化失败点、Pause、回滚与取消/退休交错也没有完整软件注入矩阵，需 check 明确审查/补测。
2. 真实连续 Mag worker 的变化像素、排除、帧率/长时间资源、真实 Freeze 前后、浏览器/Word/PDF 等没有执行；旧基础 PrintWindow 证据不能扩大为本轮整链通过。
3. 正常产品的 WindowService 成对 ACK 是实际生产路径，本轮隐藏专测只验证 real output-ready 与严格 ACK seam。物理笔/触控、视觉切换、屏幕穿透、DPI/显示/device/文稿退休的实机事件、Windows 旧版本/WOW64 等仍需对应设备验收。
4. 没有改 Shader、导航产品总门禁、Stroke 点/UndoRedo 算法/UInk 格式。隐藏测试证明页身份/历史顺序和 CPU viewport；A 几何逐点/style/GUID 与退出文件重载尚未做，本阶段不据源码未改就宣称保存完整正确。
5. harness-off 对新增 CLI 的严格早退由同一早期 parser/编译分支保护，未另建一个 harness-off 产物执行；最终运行产物为 Debug/harness-on。
6. 同步 PrintWindow 的不可取消、极端永久阻塞与系统 HWND 销毁边界沿 Capture 原合同保留，不强杀、提前释放或声称所有 join 有界。

保存阶段可复用严格不可见入口、Host Begin/Channel/End 与本轮的纯值 Snapshot（真实 identity/base/viewport/seq/recovery），原 `HiddenPersistenceSnapshot()` 只辅证计数。没有新增任意 Desktop 冷载替换 API。本阶段已读 `viewport-only-save-current.md`；后阶段应在私有 root 沿真实 Clear committed -> 释放 fallback -> Undo 磁盘安装 -> 仅 follow -> Stop -> 严格 Exit UInk 读回，另覆盖 A -> follow -> B -> history。viewport-only 保存必须单独证明；完整快照 hash 含 viewport/fileGuid，不能当纯 A 几何摘要。
