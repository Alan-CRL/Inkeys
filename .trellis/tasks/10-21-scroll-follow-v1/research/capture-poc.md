# Magnification 捕获 PoC：实现、实测与未通过门槛

> 历史记录：以下独立服务原型已按用户要求移除。当前共用原定格的 Inkeys.exe 软件内测试另见捕获子任务 research/freeze-printwindow.md；旧失败不能替代当前实测结论。

日期：2026-10-21。范围：`scroll-follow-mag-capture`；未接入 Tracker、Draw3 或 Bar。

## 当前结论

独立捕获服务在本机 **Windows 11 原生 ARM64、无预存 Mag lease** 的两个真实会话中，已读取连续变化的外部测试图案，并排除本进程叠加窗。保持绘制生命周期的 alpha=0 Host 可行；这不等于生产接入门槛已通过。

**按实际 Window Service 顺序，先成功建立主线程 STA、再 Display/LoadLibrary/首次 Freeze lease 的最终对照中，独立捕获 owner 的 `MagSetWindowFilterList` 仍每帧返回 FALSE / `ERROR_NOT_SUPPORTED (50)`，没有有效帧。** 同线程原始探针可以接受同样的空表、单窗口和完整同进程窗口表。窗口站/桌面检查、逐个排除窗口验证，都未使共享租约服务成功。不能把结果归因于 ARM64 整体不支持 Mag，也不能把只有 API 成功的黑帧认作有效截图。

因此保留批准设计的最后一个全局 lease 退出才调用 `MagUninitialize` 的策略；未落地每 owner 提前 Uninitialize、未平衡的额外 Initialize、第二套截图 API 或产品按钮。**真实共享运行库门槛未通过，后续任务不得开始。** 同 owner 的两个独立控件在修正 fixture 初始化顺序后可以取得真实像素，但其 private-bytes 趋势尚未证明有界，也未证明可满足 UI 线程不阻塞等产品合同，不能直接视为已验收修复。

## 已实现接口与所有权

- `Graphics/MagCaptureService.h/.cpp`：`Start(options, callback)` / 非阻塞 `Stop()` / `Snapshot()`。从同一 `Inkeys.Display` 快照取得主屏 bounds、workArea、DPI、generation。
- 独立 Win32 owner 线程持有 Host/Child、STA COM、消息循环和复用的 `DibSurface`；每轮枚举同进程顶层 HWND，提交排除表、Source、重绘，再 `PrintWindow(child, dc, 2)`。无图像 FIFO，目标 20 FPS，慢帧跳过周期。
- 帧包含借用 BGRA span、尺寸、stride、Source/WorkArea、session、sequence、display generation、QPC 时间与频率、capture_ms、quality 和 API error。span 仅在同步回调内有效；alpha 不参与有效性判断。
- `MagnificationRuntimeLease` 动态解析 Mag API，共享 DLL 和进程初始化计数；Freeze 仅窄改为取得/释放同一 lease。正常释放只在最后 lease 退出后 Uninitialize。
- 监督线程区分 5 秒启动 grace 与运行后 1 秒没有完成结果；超时关闭发布并请求停止。Stop 不等待同步系统调用或已准入回调；真实 owner 和 monitor 线程退出前不允许重启，不提前释放 HWND、DIB、lease 或回调依赖。
- 日志走现有有界异步 `DiagnosticOutput`，统一 `[ScrollFollow]`。生产代码不创建额外 Session 或 Console；专项 Headless CLI 显式启动一个诊断 Session。

## 图像判定与状态

采样 luma 网格约 256×144，输出均值、方差、近黑比例和水平信息边缘比例。`mean<2 && variance<4 && nearBlack>=0.995` 判 NearBlack；`variance<4 || edgeRatio<0.002` 判 LowInformation。暗色有纹理图像可通过；纯白、纯黑、坏尺寸/stride/buffer 被拒绝。相同真实静止帧保持有效。

服务状态为 Off / Starting / Capturing / Stopping / Failed；API 和图像失败都有明确 reason。`Start` 在旧 worker 未真正退出时返回 false 并设置 `ERROR_BUSY`，不覆盖正在运行的会话身份。`resourcesReleased` 包含实际线程句柄已 signal 的条件。

## 真实环境与 fixture

- `WinSta0`，当前线程 Desktop=`Default`，InputDesktop=`Default`，捕获 owner Desktop=`Default`。
- Session=1，RemoteSession=0，DWM composition enabled=1；native architecture=12 (ARM64)，WOW64=0。
- 主屏 Source=(0,0,2880,1920)，32bpp top-down，stride=11520。
- 专用外部图案进程通过共享内存发布 requested/painted phase，只有真实 WM_PAINT 完成后才更新 painted。父进程叠加一个洋红色窗口，验证被排除后得到源图案颜色。
- fixture 放入 kill-on-close Job；父进程正常/失败均关闭专用子进程和窗口，图案自身另有 90 秒关闭定时器。未操作用户当前应用。

## 首轮与定位证据

| 实验 | 实际结果 | 证据 |
| --- | --- | --- |
| 首轮冷启动，旧 1 秒总计时 | 初始化期间超时，0 callback；后来完成初始化并正常清理 | `Build/ScrollFollow/capture-poc-arm64.log` |
| 5 秒独立启动 grace / 预存 Freeze lease | 130 个结果全部 Filter/error50；0 有效帧；Host alpha=0 可绘制，清理通过 | `capture-poc-arm64-warm.log` |
| 原始同线程 API 能力 | 空表、overlay、host、IME、all-own-windows 排除表均成功；alpha0+topmost 能读到非均匀桌面像素 | `capture-poc-arm64-filters.log` |
| 原始 offscreen-alpha255 / SW_HIDE | PrintWindow 返回 TRUE，但图像为 mean=240、variance=0 的常量；源 phase 不匹配 | 同上 |
| 仅补充 owner STA COM | 共享 lease 服务仍 Filter/error50；桌面/会话一致 | `capture-poc-arm64-com.log` |
| 无预存 lease，服务 owner 自己首次 Initialize | 两轮完整实际 source marker、4 次 phase 改变、静止、排除与重复启停通过 | `capture-poc-arm64-cold.log` |
| 最终 STA-before-Display/LoadLibrary/lease，保留 Freeze lease | main_com=0；独立 owner 仍 Filter/error50，0 valid，退出1 | `capture-poc-shared-sta-order-arm64.log` |
| 最终同序初始化的冷启动服务 | 两轮真实源和 phase 更新通过，退出0 | `capture-poc-cold-sta-order-arm64.log` |
| 最终同序初始化的同 owner 两控件 | 单控件正对照及三轮第二控件/幸存第一控件真实像素均通过，退出0；内存趋势未验收 | `capture-poc-owner-sta-order-arm64.log` |

冷启动真实读回旧轮次：cycle1 14 帧，13 valid/1 初始无信息帧，mean=59.475ms，p95=70.063ms，max=74.736ms，8.783 results/s；cycle2 12 valid/0 invalid，mean=60.516ms，p95=68.548ms，max=70.400ms，8.727 results/s。这是 2880×1920 完整采样与保守慢帧调度的实测，不承诺恒定 20 FPS，也不是 Tracker 或 Draw3 帧率。

一次较晚的 `*-final.log` 冷启动复跑曾出现 NearBlack/LowInformation 与 1234ms 慢调用触发超时，退出1，保留该失败。随后完全对齐主线程 STA 顺序的 cold 最终复跑再次两轮成功：cycle1 14帧/13valid/1初始无信息帧，mean=54.523ms、p95=67.716ms、max=70.441ms、10.302 results/s；cycle2 13帧/12valid/1初始无信息帧，mean=64.991ms、p95=79.128ms、max=79.312ms、8.491 results/s。启动分别109/156ms。不要把单次成功扩展为所有现场都稳定成功。

## 运行库实验：没有采用的方案

1. 测试局部每 owner 同线程成对 Initialize/Uninitialize，覆盖 main-first / worker-first 与重复周期。旧 dual-visible 实验没有同时获得两组可靠像素，保留失败日志。按 Freeze hidden 的实际互斥边界改写后，未初始化 COM 的原型仍未得到初始 active 像素；幸存控件在部分旧轮次能更新，不能据此放行。
2. 最终组合实验加入两个 owner 的 STA、独立 runtime-only CLI、source painted 确认及 API stage/error。第二 owner 无论先后次序都在 Filter/error50 失败；每个已记录源都 painted=1。`capture-runtime-thread-com-arm64.log` 保存四轮成对及四轮 final-only 结果。
3. “每 owner Initialize，但全局最后仅 Uninitialize 一次”的实验也未通过真实像素，并出现 GDI/handle 残留。成对实验 GDI 1→2，handles 220/220/220/222，private bytes 164216832/194736128/191545344/191606784；final-only GDI 2→4，handles 225/224/226/226，private bytes 187125760/123002880/25419776/25436160。该趋势不是可接受方案的证明，更不能以 8 Init/4 Uninit 的调用计数代替活着控件的正确性。
4. 原始 helper 与已通过服务的 WndProc 不同，故又对齐 WM_ERASEBKGND / WM_NCHITTEST / WM_MOUSEACTIVATE，并增加单控件正对照。该轮仍为 PrintWindow API 成功但实际像素 `0xff000000`、mean=variance=0、NearBlack；因此跳过同 owner 双控件推论。旧记录为 `capture-runtime-same-owner-preflight-arm64.log`。
5. 检查实际 `Window.cpp::RunGroup`，确认先 `CoInitializeEx(STA)`，再 `CreateGroup`，最先创建的 Mag Host `beforeCreate` 才加载/初始化 Mag。此前 fake Freeze lease 及 native helper 的第一次加载/初始化顺序不等价，故不是最终生产对照。最终 `RunMagCapturePoc` 先取得成功的 STA，再 Display、首次 DLL/lease；失败 HRESULT 直接退出，不能忽略 `RPC_E_CHANGED_MODE`。
6. 该修正后的服务共享场景仍失败；同 owner 原型则成功。`capture-poc-owner-sta-order-arm64.log` 的 preflight `main_com=1`（外层已成功初始化 STA，嵌套 S_FALSE）、pixel=`0xff8278a5`、mean=90.9521、variance=7152.13；三轮 second_pixels=1 / surviving_first_pixels=1 / cleanup=1。GDI=2、USER=4、handles=225 保持；private bytes=310784000→376082432→440078336。其 4 Init/4 Uninit 包括正对照，不以调用次数或像素成功代替内存验收。

这些是当前 Win11 ARM64 的实验结果，不是微软官方跨线程、跨系统行为保证。生产 `MagnificationRuntime` 没有采用这些失败策略。

## 构建环境

首次受限 runner 构建在 module dependency scan 后整整 4:59.73 退出 1，MSBuild 汇总 0 warning / 0 error，没有新产物。没有因该环境现象修改工程源码或配置。正常宿主、ARM64 原生 MSBuild、`/m:1 /p:BuildInParallel=false` 可完整构建。

通过 vswhere 定位当前 VS；同一个 PowerShell invocation 内去除重复 PATH 键、恢复单一 Path（包含宿主 PowerShell 路径）、`MSBUILDDISABLENODEREUSE=1`，TEMP/TMP 指向仓库内 `Build/ScrollFollow/TestTemp`。否则沙箱 TEMP 的既有配置原子替换测试会失败。未修改全局环境或依赖配置。

## 实施阶段验证（历史结果，检查阶段另列）

| 检查 | 退出码 | 实际证据（Build/ScrollFollow下） |
| --- | --- | --- |
| 最终 Debug/ARM64 完整solution命令 | 0 | `capture-build-arm64-stable.log`，0 warning/0 error；最终增量4.41s，首次正常宿主源码构建也成功 |
| 无窗口capture定向 | 0 | `capture-only-arm64-stable.log` |
| 完整 `--no-window` | 0 | `capture-headless-arm64-final.log`，PASS animation correctness；其后只修改显式PoC fixture的STA顺序，普通套件无改动 |
| 初始化/停止/超时生命周期PoC | 0 | `capture-lifecycle-sta-order-arm64.log`；Stop_ms=0、init4/uninit4、全部断言通过 |
| 最终同序cold服务PoC | 0 | `capture-poc-cold-sta-order-arm64.log`，两轮真实像素变化与排除通过 |
| 最终同序shared服务PoC | 1 | `capture-poc-shared-sta-order-arm64.log`，130结果/0有效，Filter/error50 |
| 同owner原型正对照与三轮 | 0 | `capture-poc-owner-sta-order-arm64.log`，像素断言通过，private bytes有界性未验收 |
| 跨owner runtime实验 | 1 | `capture-runtime-thread-com-arm64.log`，没有可采纳的替代方案 |
| diff空白、原文件编码/CRLF、新源码CRLF、三份工程XML | 0 | 无无关整文件格式变更，未检查子模块内容 |

生命周期用例使用专用类名碰撞确定性触发异步初始化失败，验证无发布/无遗留窗口后可再启；通过抛异常回调验证发布关闭，再用真实已进入的阻塞回调触发运行1秒监督，确认Host/DIB/lease继续保活、Stop立即返回、ERROR_BUSY阻止新会话、解除阻塞后无迟到回调且再次收到真实有效像素。这里没有模拟系统内部永久挂死，不能声称已验证永久阻塞PrintWindow自身的返回行为。对应会话启动375/203ms，也说明启动grace与运行监督须分离。

上述表格为implement阶段记录。随后check阶段复验见文末及子任务research/check.md；最新失败不能由历史通过覆盖。未启动产品主窗口；没有采用额外未平衡Initialize、每owner提前Uninitialize、第二套截图技术或新产品入口。任务保持未完成，后三阶段未进入。

## 可重复命令与验收边界

```text
InkeysHeadlessTests.exe --mag-capture-only
InkeysHeadlessTests.exe --no-window
InkeysHeadlessTests.exe --mag-capture-poc-cold
InkeysHeadlessTests.exe --mag-capture-poc
InkeysHeadlessTests.exe --mag-capture-poc-freeze-control
InkeysHeadlessTests.exe --mag-capture-lifecycle-poc
InkeysHeadlessTests.exe --mag-runtime-thread-poc
InkeysHeadlessTests.exe --mag-runtime-owner-poc
```

只有前两项不创建 HWND。其余六项显式执行自动关闭的专用窗口验证。共享 lease 和fresh-hidden对照均未通过，不能把 cold-only 历史成功报告为产品门槛通过。最新验证以文末、子任务research/check.md和对应capture-check-*日志为准；实施阶段通过及失败日志全部保留。

未验证：实际 PrintWindow 永久阻塞（生命周期测试使用已进入的阻塞回调验证同一监督与保活路径）、真实浏览器/Word/PDF、笔/触摸、Win8.1/Win7、x64运行（最终Headless链接失败后无可用测试产物）、产品定格活跃/退出端到端。原有 Draw3/UInk/Bar 没有接入改动，不声称通过相应功能验收。

## 检查阶段：更完整的产品前提与最新复验

顺序trellis-check补强元数据断言（固定Source/WorkArea/generation、尺寸/stride/span、QPC单调与耗时），并在阻塞回调用例直接核对managed lease保活。生产捕获、运行库及定格源码没有因检查阶段修改。

新增独立CLI `--mag-capture-poc-freeze-control`：主线程成功STA COM、Display、首次managed lease后创建与产品样式相符的first-owner Host/Child，设置identity Transform/ColorEffect。原控件从未Show，也不预先Source/Filter/采样；使用全局仅一次Initialize。真实服务由独立owner运行，Stop后才首次显示原控件并读取新phase90。这补上bare-lease未覆盖的实际隐藏控件前提，不抹掉原失败对照。

| 最新检查 | exit | 实际结果 |
| --- | ---: | --- |
| 完整solution Debug/ARM64 | 0 | 8.68s，0 warning/0 error，capture-check-build-arm64-final.log |
| --mag-capture-only / --no-window | 0 / 0 | 基础定向及完整无窗口回归通过，capture-check-only-arm64.log / capture-check-headless-arm64.log |
| --mag-capture-poc-freeze-control | 1 | 130结果/0valid，全部Filter/error50；元数据、Stop及lease/窗口清理通过。20.1457为快速失败结果/s，不是有效FPS |
| --mag-capture-poc-cold | 1 | 55帧全NearBlack/error0/mean0/variance0；metadata通过；raw alpha0-topmost同样黑，不能由历史成功替代 |
| --mag-capture-lifecycle-poc | 1 | 初始化失败/回调异常/真实1秒监督/lease保活/Stop=0ms/busy/清理通过，末尾真实有效像素再启失败；init4/uninit4 |
| --mag-runtime-thread-poc | 1 | 外层main_com=0、nested=1，两种策略各4轮第二owner仍Filter50，source_painted=1 |
| i18n / XML / diff / 物理编码EOL / PE导入 | 0 | en-US/zh-TW各425/425；原BOM/CRLF保留；无新增较新API或OpenCV/FFmpeg DLL硬导入；不代表旧Windows实跑 |

fresh-hidden对照的原控件Stop后首次读回亦为黑帧，API error0。缺少相同样式、完全独立的先验正对照，不能推定独立捕获破坏了定格。冷启动较早成功与本次失败均保留；检查确认新增fixture在cold路径没有额外DLL/窗口调用，Headless新旧PE无Mag硬导入。当前证据不足以将波动归因于某个SDK内部机制、硬件或环境条件。

本轮没有确认生产数据竞态、COM配对或借用缓冲提前释放问题；该审查结论不能替代失败的真实像素门槛。x64首次完整构建在依赖安装及Headless链接成功后因C1060 compiler heap失败；唯一命令级CL_MPCount=1重试11:44.66后在Headless链接LNK1140/PDB上限失败，未改调试信息、源码或项目配置绕过。低并发重试的产品Inkeys.exe成功链接；Headless失败链接删除旧exe，未运行x64测试。ARM64/x64产品PE静态导入均通过；完整solution x64及功能验收仍未通过。详细证据见子任务research/check.md。

## 当轮快速定位补充（用户追加20分钟后收尾）

详见捕获子任务research/quick-probe.md、quick-check.md和computer-use-observation.md，独立探针源码保存在research/probes/。没有修改生产捕获路径，以下不覆盖原生产PoC失败。

- 无WGC小尺寸单点59/60、全屏单点57/60；无效包含首帧灰色及旧phase。全屏PrintWindow最高109ms，未证明20FPS。
- 修正父Host背景混淆、仅SOURCE更新painted后，90个唯一phase的PrintWindow2/WGCsource/WGCMagHost均为九点90/90匹配；其中透明30、不透明60。只证明采样点的新内容，不代表完整图像每个像素。
- 排除实效正反对照：单owner九点19/20当前图案（首次无效）；双ownerFilter返回50后20/20采样为蓝色覆盖窗，0/20当前源图案。不能忽略Filter失败继续产品接入。
- WGC顶层HWND路线实验可用，子Mag HWND返回E_INVALIDARG；没有设置禁止捕获属性，没有改变录屏墨迹可见性。尚无产品替换。
- Computer Use确实观察了SOURCE与MAG窗口；早期视觉探针Mag下沿异常仍单列，不能与后来的九点通过合并成完整屏幕呈现验收。所有专用窗口已退出，未完成锁屏覆盖测试。
