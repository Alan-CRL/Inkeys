# Research: 最小 external 空画布实屏入口

- Query: 在现有 native product live harness 上，以尽量小的源码差异验证真实 Edge → 原共享 Mag → Tracker → owner/ACK → Off；CDP 只作独立 GT，不声称墨迹验证。
- Scope: internal（读取既有代码及本任务 browser adapter 设计；未构建、启动 GUI 或执行新协议实验）
- Date: 2026-10-23

## Findings

### 结论与建议形状

**先单独增加 external-empty case，明显小于改造既有墨迹 oracle。** 复用 `RunScrollFollowProductLiveCase(false)` 的产品启动、实际窗口服务/Mag/Business/owner/ACK/结束链路；只替换专用底层 Source 的就绪/激活、一次滚轮、最终 GT 和清理。浏览器进程、CDP 以及真实 OS wheel 由独立私有 runner 持有；C++ 不加入 HTTP/WebSocket、浏览器截图解码、通用 source adapter 或新产品 API。

建议内部函数增加有限 case kind（或一个 externalEmpty bool，禁止与 ink/long 同时为真），新增严格 `--scroll-follow-live-external-test`。不要复制整段 370 行 RunCase；也不要求把现有所有分支抽成通用框架。新增少量 `ReadExternalReady / WriteExternalPhase / WaitExternalResult / CheckExternalWindow` 测试 helper 即可。真实 C++ 改动规模不能事前保证；大头是 runner 的进程/协议/输入所有权，不是产品运行路径。

### 直接复用与必须隔离的位置

下列为本次读取时 `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl` 的行号；其他代理正在修改文件，实施前按函数/锚点定位，勿硬套数字：

| 区间/锚点 | 最小处理 |
| --- | --- |
| `Fixture` :27–47 | 少量 external 生命周期/IPC 路径、root/content HWND、持有的只读进程 handle、double GT 字段；不要覆盖 native 的 x/y、paintSequence、taggedWheels 伪造 SourceProc 事实 |
| `CheckFixture/CheckDesktop/Wait` :177–238 | 完整保留安全层、几何/DPI、线程失败、offSignal/WM_QUIT 检查；external 模式另查持有的 Edge process handle 未退出、HWND/PID/归属和 IPC run/阶段身份仍一致 |
| `CreateRoot` :682–700、RunCase :703–749 | 保留原私有日志/AutoSave root、COM MTA、Display/主屏几何门、看门狗；external IPC root 由 runner 另建，验证普通目录及祖先无 reparse、位于仓库 `Build/ScrollFollow` 下，拒绝缺失/过长/不合法路径 |
| Source 注册/创建/显示 :750–757 | external 从 runner 完整验证后交付的 ready 文件接管只读 root/content HWND；不注册 SourceProc、不 CreateWindow、不 setuserdata/subclass 外进程窗 |
| WindowService/Mag :758–789 | 原样复用：host/child callbacks、beforeCreate 初始化、同线程所有权、BuildCurrentFilterList；external root/content 都必须不在原排除表，不手改列表让页面进入图像 |
| Source foreground/GT/activation :790–825 | native 原样；external 等 runner 的专用页面实际前台/命中/画面身份完成，不调用 TexturePixel GT、不复制本地 activation click。SetForegroundWindow 被拒绝就有界停止，避免增加 ALT/AttachThreadInput 等绕过逻辑 |
| ProductHost/StateMonitoring/MagnifierThread :826–875 | 全复用。仍内存配置、真实 Draw::StartProduct、ChangeStateModeToPen、SyncDraw3State、实际两线程/Business；不用 TestPorts、手工 ACK 或目标写入口 |
| 初始内容/RequestToggle/Tracking :896–908 | native 的 ScreenSourceGt 由 external 只读 oracle/窗口身份检查替换；真实 RequestToggle/Tracking/FollowChannel 及空页 pair(0,0)、currentPageHasContent=false、精确 ACK 必须保留 |
| 原空页 wheel :943–983 | external 不用 f.taggedWheels、f.paintedY 或固定 wheelStep=16。写 `tracking-ready` 后等 runner 的真实 wheel + 稳定 CDP GT；然后用现有实际 channel Read 检查，预期 `latest = -deltaPhysical`、`viewport = base + deltaPhysical`，仍要求 distinct seq、applied、!clamped、至少3新accepted捕获帧/两次收敛 |
| Off/结束断言 :984–1026 | RequestStop/真实 Off/窗口 pair(1,0)/精确ACK/捕获drain/final.sequence屏障保持。把 native source/tagged计数/固定16px断言放回 native 分支；external 检查自己的稳定最终 GT、viewport、tool revision、empty content revision、不曾写目标。最终日志明确 `visible_ink/persistence=not_run` |
| 清理 :1030–1068 | 公共清理全保留；native 才 DestroyWindow(f.source)/UnregisterClass/reset texture。external 只释放持有句柄、发完成/失败阶段；runner finally 关闭其自己的 Edge root/profile 实例，不能 DestroyWindow 外进程窗、taskkill /IM 或关用户 Edge |
| 包装入口 :1069–1075 | 增 external 包装与 harness-off return2 桩，保持旧三个入口语义 |

目前 `Facts()` :661–680 已有捕获/Business/owner/ACK/内容/成对窗口充分事实。首个 external case 不需要给 Product/Host/ScrollFollow 增新接口或运行时统计。`Draw::ProductHost().FollowChannel()` 只 Read，GT 只参与夹具断言。

### 严格早退入口与文件范围

- `Inkeys/IdtMain.cpp:1382–1418`：将新有限 flag 纳入整个 argv 的 prefix/malformed 扫描、准确单参要求、harness gate、EnsureProcessDpiAwareness 和唯一 DiagnosticOutput::Session；当前循环常量 `test < 8` 须随新增项更新。多余/重复/前后错位参数均先返回2，不准回落正常 Main。
- `Inkeys/IdtMagnification.h:30–35`：新增仅 harness 包装声明，保持原文件 BOM/EOL/末尾格式。
- `Draw3.ProductLiveTest.inl`：上述有限 helpers/分支。它已被 IdtMagnification.cpp include，不需要新增 vcxproj/filters 源登记。
- ignored `Build/ScrollFollow/run-live-external.ps1`（名称建议）：单独 orchestration；可配同目录有限 CDP helper。不得把 browser 控制逻辑塞入产品模块，也不需要 npm/新依赖/构建系统。
- ignored `browser-fixture/index.html` 只在需要可识别 run title/小画面标记时最小追加；现有静态标题不含 run token，不能误写“已唯一标题核对”。URL query token 已可用于 CDP 精确唯一 target，但 Win32 映射仍需进程证据。

参数仍严格一个 flag。额外有限运行信息通过仅子进程继承的私有 env（例如 `INKEYS_SCROLL_EXTERNAL_ROOT`）传入，不写注册表/用户配置。env 缺失、路径不合法、协议错误必须 return2/失败；不能因此普通启动。测试入口路径读取在创建 Display/窗口之前完成。

### 最小串行握手：runner 负责浏览器，夹具负责产品

使用一个全新 run root 和单次创建的有界 JSON 阶段文件足够；不需要通用 RPC。每个文件 `CREATE_NEW`，runner 完成写入并关闭后才通知/发布，或临时文件完整写后同目录原子重命名。读取限制字节数（例如16KiB）、固定schema、run token、phase seq、Inkeys PID、Edge PID/创建时间；不反复读部分 JSON 后当正常零GT。旧文件、重复阶段、URL改变、target丢失、超时都失败。

1. runner 先建 fresh root/profile，启动唯一专用 Edge app 窗口、保存根进程 handle/PID/创建时间/image/profile/启动参数。连接只属于这个私有实例的 CDP，精确匹配 fixture URL+run token 的唯一 target；等页面可见/稳定，记录版本/target/schema。先记录真实页原点/尺寸/DPR/resizeCount/scrollX/Y/wheelCount。准备 `browser-ready.json`，再启动严格 Inkeys 外部测试入口（私有 TEMP/TMP、stdout/stderr/hash/超时同现有 launcher）。
2. C++ 校验 ready、窗口/进程、主屏几何，正常启动上述真实产品链。待实际 Tracking + ACK、空页 pair(0,0)，写 `inkeys-tracking-ready.json`，包含本run/session、actual channel base/捕获 accepted 基数、Inkeys PID及阶段序号。这里的 base 是用于审计产品事实，不是 runner 用来操纵页面的目标。
3. runner 只在读取本 PID/阶段 ready 后取 CDP **before** 快照，核浏览器实际foreground/content hit/物理左键未按/安全界面，向该专用浏览器内容发一次真实 OS wheel。不能 CDP Input.dispatchMouseEvent、JS scrollTo 或 PostMessage WM_MOUSEWHEEL 代替真实系统输入。记录 SendInput 返回、发送时实际 cursor/hit/window。浏览器 wheel 事件没有 native extraInfo 标记，不声称带 tag。
4. runner 只读等待同 target 的 wheelCount/scrollCount 增加、非零且稳定 scroll 差、resize/DPR/viewport/URL 不变；用实际差值而非 wheelDelta 推导物理 GT。写 `browser-wheel-result.json`，包括 before/after、时间/计数、稳定证明、物理映射及原始数值。C++ 只在其 `Wait` 中读这些有限观测并等实际 Tracker/owner 收敛，旧序号不接受。
5. C++ 完成实际 Off/结束屏障后写 `inkeys-off-ready.json`；runner 再读取同 browser target 的稳定最终快照/真实 window facts，写 `browser-final.json`。C++ 核前后页面未再滚动、最终 Viewport相符，再PASS/正常清理。runner 只能在子进程退出和已取得其状态后关闭自己 Edge；中途任一侧退出/安全层出现，另一侧有界撤销，不继续 wheel。

可从独立 runner 的日志 tail 看 ready，但有限阶段文件比自由文本解析更容易做身份和重放拒绝。没有必要把 CDP before/after 连续流逐帧喂 C++，更不能将其写为 Follow Target 或补偿 Tracker。

### HWND关联与最小可执行边界

沿用已有 `live-browser-adapter-design.md` 的全部所有权规则，尤其 CDP windowId **不是 HWND**。最小 first case 可限定：私有 Edge 根进程 handle 活着，唯一 owned 顶层 app HWND；进程启动身份/profile与目标URL唯一；实际内容 child HWND 归该 root，child PID属于已验证本profile进程树；`WindowFromPoint` 的 `GA_ROOT` 与 owned root 相同。每次输入前重核，不按类名/窗口标题单独认领。

首次为减少 CSS/屏幕映射分支，可明确要求能找到并验证真实内容 HWND：`ClientToScreen` 原点、其物理 client size 与 `innerWidth/innerHeight × DPR` 在既定整数边界内一致；页面 CSS visual viewport 无缩放，DPR/resizeCount 全程不变。找不到、尺寸解释不一致就记环境/适配受限，不用主窗口 caption/client rect冒充网页内容原点。不通过新增 deviceScaleFactor、关闭GPU或裁剪产品ROI规避。

真实画面身份不能由 JS stable/rAF 单独替代。最小方式：runner 在自己的专用内容 HWND 内，取已有页面上已知局部像素/DOM标记的实际屏幕小块并记录，或将已限制的自建浏览器 screenshot仅作独立 oracle。若需要为首例加固定纯色/图案小标记，必须在专用fixture内明确记录位置/面积，并核真实屏幕标记；这不是图像数据源替换。比较需要实测映射，不能假定CDP screenshot尺寸即屏幕物理像素。**不必为了这个空画布case建立墨迹前后背景差分、A/B ROI、pen core mask或PNG全图对齐框架。**

独立DOM GT能证明页面状态移动；实际屏幕标记/画面观察可证明专用内容在桌面上。二者都不能证明Mag某个具体帧逐像素无误。产品经真实共享Mag取得的Tracker total与独立页面GT吻合，是首例所要求的链路证据；日志里区分这三种事实。

本case只验证一次非零垂直小滚动，实际wheel受系统配置/平滑动画影响，最终Δ应实测；若第一次输入导致大于现有追踪收敛/跳变范围，诚实失败，不拿CDP回填位移或改用户滚轮配置。后续反向/边界/水平/长程单独扩展，不在首例一次全做。

### 为什么比完整browser ink adapter小

复用：原Host/窗口对/工作线程/Business/原Mag/Channel/诊断/安全门和退出；新代码仅需有限外部输入观测与阶段身份。

省去：`ReadInkScreen/CompareInk/CheckInkRoi/DrawOsInk/ReadInkExit` 的网页背景适配，滚动后browser-only底图及合成屏幕对齐，抗锯齿/色彩/文字噪声差分，A/B与Exit两笔坐标和codec证明，以及新多态source框架。因此该方案足以先回答“真实Edge内容能否驱动现有追踪与owner正确结束”。它不能回答“浏览器上墨迹始终可见、位置正确和保存可编辑”。

## Related specs / references

- `.trellis/spec/native-desktop/scroll-follow-product.md`：空页pair(0,0)的证据边界、真实ACK、绝对目标和结束屏障。
- `.trellis/spec/native-desktop/scroll-follow-capture.md`：共享原定格Mag、排除列表、security门，图像/显示真值区分。
- `research/live-browser-adapter-design.md`：已有私有profile/CDP/Win32根进程映射、官方Microsoft Edge与CDP链接；本文件不重复扩展外部协议设计，也未运行这些API。
- `Build/ScrollFollow/run-live-capture.ps1`：已有严格进程空闲检查、私有TEMP、hash、日志、超时、只返回本子进程状态。

## Caveats / Not Found

- 当前只读设计，无Edge启动/协议/窗口/像素新实测，没有确认首例能通过。
- 尚无external case、协议schema或runner代码；上述函数名/文件名是可实施建议，不是现存API。
- `Fixture::source` 当前由native拥有，任何共享清理改动必须显式区分ownership，避免对外进程DestroyWindow。
- 不改原native短/long/ink预期，不声称空画布证明墨迹、硬件笔、UInk或普通tab工具栏兼容。
- 其他代理正在修改GT取样；本研究未回退/覆盖其修改，未改源码、test、build、配置或spec，未执行Git。
