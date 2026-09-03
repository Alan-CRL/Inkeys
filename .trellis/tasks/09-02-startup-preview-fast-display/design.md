# 技术设计

## 1. 设计约束

- 最低运行环境为 Windows 7 SP1 + KB2670838；D3D11/D2D1.1、FL11.0、WARP 必须可用。
- Preview 只复用 `Inkeys.UI.RenderPipeline`；禁止第二套 device、context、D2D factory 或图形调度线程。
- 生产路径不使用 GDI+、WinUI Runtime、DirectComposition、Win10-only API/effect，也不静态链接 Win8.1+ Shcore API。
- T0 前不产生新线程、窗口或 Tracker。开关关闭时不创建 Preview，并保留 RenderPipeline 原初始化位置。
- 任何失败回调、进度回调或 topmost observer 都只能做原子写/无阻塞 post，不在关键启动线程形成反向同步等待。

## 2. 模块边界

计划新增以下内聚模块，最终名称在第二阶段遵循工程 module 命名检查后落地：

- `Inkeys.Startup.Progress`：纯 C++、无 HWND/D2D 依赖；定义 immutable plan、milestone、单调 tracker、失败快照和观察接口。
- `Inkeys.UI.StartupPreview.Format`：显式 little-endian BIN/cache serializer、defensive parser、IEEE CRC-32、visual signature 输入和分类。
- `Inkeys.UI.StartupPreview`：owner thread、窗口消息、render callback、动画状态、cache reader/writer、Bar frame proxy 与 shutdown。
- Bar 只负责发布启动状态、committed frame snapshot、稳定性和 presentation alpha；它不控制 Preview 窗口。
- Window Service 只提供可注销的 topmost-refresh observer；observer 成功路径只向 Preview owner queue post。

## 3. 启动序列

1. 最终进程越过 `IdtMain.cpp` SuperTop 块。
2. 主线程记录 `steady_clock::now()` 为 T0，构造 `StartupProgressTracker`，报告 `SuperTopCrossed`。
3. 读取只含 StartupPreview Enable 和 Bar 视觉兼容字段的 mini config；构造本次 immutable progress plan。
4. 在任何 HWND/Display 前确认 process DPI awareness。Win8.1+ 动态调用 Shcore；Win7 回退 `SetProcessDPIAware`；`E_ACCESSDENIED` 表示已有来源，不再错误覆盖。
5. 开关开启时：读取/分类 bounded cache，验证 embedded header，条件化提前初始化 RenderPipeline，启动 Preview owner thread 并注册 StartupPreview client。任一步非核心失败都跳过/降级 Preview。
6. 继续现有日志、COM、Display、full config、I18n、插件、PptCOM、字体、Window Service、Draw3、Setting、Whiteboard 和 topmost 流程。原 RenderPipeline 位置接受 `S_FALSE`。
7. 启动 Bar/Freeze/PPT 等并行阶段。所有生产者只报告自己真实完成的 milestone。
8. Bar 首个完整 committed frame 报告 100%，发布安全 proxy，并按 cache 分类进入交接。
9. 交接完成后注销 topmost observer 和 StartupPreview render client，在 owner thread 隐藏/销毁 HWND 并停止线程；cache writer 可继续处理最后稳定 revision，但退出前必须 bounded join。

## 4. 线程所有权

| 对象/状态 | 唯一 owner | 允许的跨线程操作 | 禁止 |
| --- | --- | --- | --- |
| Tracker milestone bits、真实 work units、失败快照 | lock-free/短锁共享状态 | 任意启动生产者用 noexcept 原子报告；UI 只读 snapshot | 回调里日志刷盘、等待窗口或执行 D2D |
| Preview HWND、消息循环、z-order、show/hide/destroy | Preview owner/message thread | 主/渲染/Window observer 只向 owner queue post；命令可带 bounded completion | 渲染线程直接 `DestroyWindow`、同步反调 Window Service |
| Preview D2D target、bitmap、effect、brush、Bar proxy | RenderPipeline 唯一渲染线程 | owner 只提交 plain command/state；generation 变更时渲染线程重建 | 跨线程共享 mutable target；cache writer 持有 COM bitmap |
| Bar target 与 frame transaction | RenderPipeline 唯一渲染线程 | committed 后复制成非 target proxy/CPU staging；发布 plain metadata | 失败 attempt 发布、缓存或推进 alpha |
| Cache writer | 单个后台 writer | 接收普通内存 + revision；完成 temp/flush/replace | 访问 D2D resource；阻塞启动线程；并发写多个 revision |
| Window Service HWND | 各自现有 owner thread | observer 在成功 topmost refresh 后异步 post | observer 等 Preview；Preview 等 Window owner 形成环 |

Preview window 使用 `WS_POPUP` 与 `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，不使用 `WS_EX_TRANSPARENT`；`WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATEANDEAT`，client hit/click 被吞掉。创建时 `HWND_TOPMOST`，显示用 `SW_SHOWNOACTIVATE`。

## 5. StartupProgressTracker

### 5.1 数据合同

- `StartupPlan` 在 mini config 后冻结，保存本次真正会执行的 milestone 和总 work units。禁用 Preview 时移除 cache/Preview 专属 milestone，重新以所选真实 work units 为分母；绝不把 skipped 阶段伪报为完成。
- 每个 milestone 有稳定 ID 和唯一权重。报告通过 atomic bitset/CAS 将“首次完成”变成唯一增量；重复、乱序和迟到报告无效。
- snapshot 包含 `completedUnits`、`totalUnits`、`actualRatio`、`failed`、`failureCode`、`T0` 和 revision。失败用 CAS 固定首个致命快照；后续完成报告不改变实际进度。
- UI 的 `displayedRatio` 只能单调追赶 `actualRatio`，每帧取 `min(eased, actual)`；elapsed time 只决定进度条是否可见和动画相位，不增加 work units。
- 即便所有先行阶段已完成，target 在 `BarFirstCommittedFrame` 之前强制小于 1；只有该 milestone 可使 ratio 等于 1。
- optional preview/cache 的“尝试已完成但结果为降级”可以完成对应真实工作；致命主启动失败则冻结。不能为未执行代码补权重。

### 5.2 启用 Preview 时的 nominal 权重

总计 1000 work units。权重表示已观察到的启动工作边界，不是耗时预测。

| Milestone | 权重 | 精确报告点 | 生产者线程 |
| --- | ---: | --- | --- |
| SuperTop crossed / T0 recorded | 10 | 越过 `IdtMain.cpp:743` 后 Tracker 构造成功 | 主线程 |
| Mini config + immutable plan | 15 | `ReadMini` 已应用默认值并得到开关/视觉字段 | 主线程 |
| Cache/embedded classification | 20 | bounded 读取、header/CRC/compat 分类或确定 Missing 完成 | cache reader；主线程只接收结果 |
| Preview owner + first ULW commit | 40 | owner HWND ready 15；StartupPreview client/资源 ready 10；Preview 首次 ULW 真正成功 15 | owner + RenderPipeline |
| Logging | 30 | logger 可用且启动清理完成 | 主线程 |
| DPI + legacy surface resize | 25 | awareness 已确认 15；原 drawpad/tester resize 完成 10 | 主线程 |
| COM | 15 | 主线程 COM 初始化成功 | 主线程 |
| Display | 35 | `Display::Initialize/Refresh` 枚举并发布 snapshot | 主线程 |
| Full config | 85 | defaults/read/兼容迁移/必要写回全部结束 | 主线程 |
| I18n | 25 | 当前 locale resources 初始化成功 | 主线程 |
| Plugins | 25 | shortcut 与 DesktopDrawpadBlocker 启动尝试结束 | 主线程 |
| PptCOM activation | 30 | resource 221 activation context 与 COM service load 成功 | 主线程 |
| RenderPipeline | 90 | D2D factory/DWrite 20；WARP epoch 40；scheduler render thread ready 30；提前完成后原位置不重复 | 主线程 + render thread ready handshake |
| Fonts | 45 | font files/collection 成功建立 | 主线程 |
| Window Service | 100 | overlay owner ready 55；setting owner ready 25；`Start()` 成功返回 20 | 两个 Window owner + 主线程 |
| Draw3 | 150 | HWND attach 15；graphics device 35；presenter 30；RTS 25；controller 15；首个 Draw3 present 30 | 主线程 + Draw3 drawing thread |
| Setting | 35 | Setting 初始化完成且 client/session 可用 | 主线程 |
| Whiteboard | 35 | PageControl、Freeze client 和 Display subscription 注册完成 | 主线程 |
| Initial topmost refresh | 15 | Window Service 的首轮 refresh 成功 | Window owner/主线程调用方 |
| Freeze first committed surface | 25 | Freeze 的 ULW 返回成功；不得仅以提交调用结束计数 | Freeze thread |
| PPT UI clients registered | 20 | PPT UI PageControl client acquire/publish 成功 | PPT thread |
| Bar initialization | 75 | window 5；UI graph 20；media 10；preset/components 20；state/position 10；mouse/interaction ready 10 | Bar init thread |
| Bar render client registered | 15 | `RenderPipeline::Register(Client::Bar)` 真正成功 | Bar init thread |
| Bar first committed frame | 40 | 首个 `presentCompletion.IsCommitted()` | RenderPipeline render thread |
| **合计** | **1000** |  |  |

并行阶段按首次完成单位直接合并，不按 wall-clock 或线程数量平均。Draw3、Window Service 和 Bar 使用上表子 milestone；若某平台无法安全观察某个子点，该子权重合并到该阶段最终成功点，而不是按时间插值。

## 6. 状态机

### 6.1 Preview 生命周期

`Disabled -> Stopped`

`Preparing -> ShowingEmbedded | ShowingValidCache | ShowingCorruptFallback | ShowingIncompatibleFallback -> WaitingForBar -> Handoff -> Stopping -> Stopped`

任意非致命 Preview 错误进入 `Bypassed` 并让正式启动继续。任意主启动致命错误进入 `FailurePending`：若 Preview 可呈现，则 `FailureRedRequested -> FailureRedCommitted/Timeout -> Stopping`；否则直接走现有 popup。

### 6.2 Bar 启动状态

`NotStarted -> Initializing -> RenderClientRegistered -> FirstFrameCommitted`

终止状态为 `WindowMissing`、`ClientRegistrationFailed`、`StartupFailed`、`StoppedBeforeReady`。状态发布单调且只允许一次终止；`Rendering()` 改为显式 bool/result，silent return 必须映射为终止状态。

### 6.3 Cache 分类

- `Missing`：目标不存在。
- `Valid`：v1 结构、CRC、layout epoch、visual signature、DPI/monitor/window/anchor/progress geometry 均匹配。
- `Incompatible`：文件是受支持格式且完整，或能安全识别为其他受支持族版本，但视觉/环境不适用。
- `Corrupt`：截断、overflow、长度/stride/矩形/pixel format/CRC 非法。未知格式若不能安全验证完整性也按 Corrupt，不尝试猜测。

## 7. Embedded BIN / Disk Cache v1

Embedded resource 与磁盘 cache 共用同一 160-byte little-endian header 和紧随其后的预乘 BGRA payload。每个字段逐字节读写，不使用 `reinterpret_cast<Header*>` 或直接 dump struct。

| Offset | Size | Field | v1 合同 |
| ---: | ---: | --- | --- |
| 0 | 8 | magic | ASCII `IKSPRVW\0` |
| 8 | 2 | formatVersion | `1` |
| 10 | 2 | headerSize | `160` |
| 12 | 4 | layoutEpoch | 可见默认布局/格式变化时递增 |
| 16 | 4 | pixelFormat | `1 = BGRA8_UNORM_PREMULTIPLIED` |
| 20 | 4 | flags | bit 0 embedded canonical；bit 1 disk cache；其余必须为 0 |
| 24 | 8 | captureRevision | embedded 为 0；disk cache 单调 revision |
| 32 | 32 | visualSignature | canonical visual input 的 SHA-256 |
| 64 | 4 | width | 像素宽，1..8192 |
| 68 | 4 | height | 像素高，1..8192 |
| 72 | 4 | stride | v1 必须等于 `width * 4` |
| 76 | 4 | reserved0 | 必须为 0 |
| 80 | 8 | payloadSize | checked `stride * height`，最大 64 MiB |
| 88 | 4 | captureDpiX | canonical embedded 为 96 |
| 92 | 4 | captureDpiY | canonical embedded 为 96 |
| 96 | 4 | monitorPixelWidth | 抓取显示器像素宽 |
| 100 | 4 | monitorPixelHeight | 抓取显示器像素高 |
| 104 | 4 | monitorWorkWidth | work area 像素宽 |
| 108 | 4 | monitorWorkHeight | work area 像素高 |
| 112 | 4 | windowOffsetX | signed int32，相对 monitor origin |
| 116 | 4 | windowOffsetY | signed int32，相对 monitor origin |
| 120 | 4 | anchorX | signed int32，相对 payload origin |
| 124 | 4 | anchorY | signed int32，相对 payload origin |
| 128 | 4 | progressLeft | signed int32，payload 内像素 rect |
| 132 | 4 | progressTop | signed int32 |
| 136 | 4 | progressRight | signed int32 |
| 140 | 4 | progressBottom | signed int32 |
| 144 | 4 | crc32 | IEEE CRC-32；计算时本字段视为 0，覆盖完整 160-byte header 与 payload |
| 148 | 4 | reserved1 | 必须为 0 |
| 152 | 8 | reserved2 | 必须为 0 |

DPI 是真实兼容输入：缓存像素、Bar zoom 与 window/anchor geometry 都依赖抓取 DPI，所以保留 X/Y。主题和语言不设重复字段；它们进入 visual signature，当前只有中文/深色。文件长度必须恰好等于 `headerSize + payloadSize`，禁止尾随数据。

### 7.1 Defensive read 顺序

1. 打开并取得 64-bit file size；小于 160 或大于 `160 + 64 MiB` 直接 Corrupt。
2. 只读固定 header，检查 magic、headerSize、版本和 reserved bits。相同 magic 但不支持的 version/header 若文件长度仍受限且能由公共 envelope 安全识别，则 Incompatible；否则 Corrupt。
3. 使用 checked arithmetic 验证 width/height、`width * 4`、`stride * height`、payloadSize 和 exact file size；此时才分配 bounded buffer。
4. 检查 monitor/work/window/anchor/progress rect：维度非零、right > left、bottom > top、进度 rect 在 payload 内，所有加减不溢出。
5. 完整读取 payload，计算 CRC；短读或 CRC 不符为 Corrupt。
6. CRC 后比较 layout epoch、visual signature、DPI 与显示几何；不匹配为 Incompatible，全部匹配才是 Valid。

## 8. Visual Signature、资产生成与缓存

### 8.1 Visual Signature

用稳定 canonical serializer 按固定顺序编码：format/layout epoch、中文语言标识、深色主题、Bar zoom、A1/extension/A2 按钮 ID/size/visibility/order、默认 expanded/dock/position、图标/颜色/圆角/阴影资源版本和 pixel/alpha 规则，再用仓库已有 SHA-256 wrapper 计算 32 bytes。不得直接 hash JSON 文本、内存地址、unordered container 顺序或平台相关 struct。

### 8.2 默认 BIN 可复现生成

第二阶段增加仅开发者可用的 capture 模式。它启动正常 Bar 场景和正常 RenderPipeline，应用干净配置默认值，强制校验中文、深色、默认按钮、expanded、无 hover/menu/capture/animation，且 primary monitor 为 96 DPI。首个“稳定 committed” Bar frame 经同一 crop/staging/serializer 写出候选 BIN，再用生产 parser 反读并核对 signature、layout epoch、CRC 和像素尺寸。验证通过后才把单个文件作为资源 ID 306、专用 `STARTUP_PREVIEW_BIN` 类型登记进 RC/project。生成命令和预期 signature 记录在资源旁文档，禁止手绘、AI 或 mock renderer。

下列任一可见/格式变化必须递增 embedded layout epoch 并重新生成：浅色模式或默认主题；i18n 完成；默认语言/文字；默认按钮序列、尺寸、显隐、扩展项；默认布局、图标、颜色、圆角、阴影、启动状态；BIN pixel format、alpha 或序列化格式。

### 8.3 稳定 committed frame

候选帧必须同时满足：

- `presentCompletion.IsCommitted()`，frame generation 仍为当前 epoch；
- 无 active animation、pointer capture、press/hover、menu/panel、临时提示、debug overlay；
- 当前 state/zoom/buttons/dock/position 是持久状态，crop/viewport/monitor geometry 完整；
- 自最后一次持久视觉 revision 起安静 750ms。防抖只决定缓存写时机，不改变启动进度。

持久视觉变化（zoom、按钮/extension、主题/语言、dock/position、未来 UI3 外观）递增 desired revision。渲染线程复制 exact crop 到普通 bitmap，再复制到 `CPU_READ | CANNOT_DRAW` staging，Map 后形成普通 byte vector；writer 永不持有 D2D COM 对象。

### 8.4 Durable latest-only write

单 writer 接收 `revision + header + bytes`；若已有待写任务，只保留最高 revision。它在目标同目录用 PID/revision/random suffix 和 `CREATE_NEW` 创建 temp，循环写满，调用 `FlushFileBuffers`，关闭 handle，再用 `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 替换。替换前后若 revision 已落后则删除自己的 temp，不覆盖更新结果。目录不可写、flush/replace 失败只限频记录并保留旧 cache；退出发 stop、清队列并 bounded join，超时不得让线程访问已销毁状态。

## 9. Preview 渲染与动画

### 9.1 Render callback

- Dispatch order 为 Bar -> StartupPreview -> 其余现有 client。早期仅 StartupPreview 注册时也可独立呈现。
- 每个 epoch 创建 Preview layered target、上传 bitmap、GaussianBlur、gradient/brush 和 proxy；generation 变化先释放全部旧资源。
- Embedded 先按目标像素几何用 cubic interpolation 放大，再用 `CLSID_D2D1GaussianBlur`，初始 sigma 约 6 DIP、Balanced、Soft border。用 `GetImageLocalBounds`/扩展目标空间容纳负 origin 与约 `sigma*6*DPI/96` growth。
- Blur 创建/绘制失败降级到普通 cubic image；bitmap 失败则 bypass Preview。降级不改变主启动结果。

### 9.2 Shimmer

源 BGRA alpha bitmap 作为 opacity mask。保存当前 antialias mode，切到 `ALIASED` 后 `FillOpacityMask`，无论成功失败都恢复。渐变含宽低 alpha 柔光、窄高亮核心和低 alpha 尾光，整体倾斜；相位使用 `0.5 - 0.5 * cos(pi * t)`，周期 1.75s，起止均在画面外。普通阶段约 30 FPS；progress/handoff 约 60 FPS。失败时仅关闭 shimmer。

### 9.3 确定进度条

- 可见门严格为 Preview 首帧 ULW committed 并请求 owner 显示后满 3s 且尚未完成；T0 不再驱动该视觉门；180ms 淡入。
- fill 来自 tracker snapshot，displayed 只追赶、不越过 actual；实际值不变时 bar 停住，shimmer 可继续。
- 进度条在 blur/shimmer 后最后合成，保证处于 Z 轴最上层；坐标以包含主按钮的完整主栏内容矩形为基准水平、垂直居中，宽度为 192 DIP（窄目标允许时保留至少 48 DIP 双侧边距）。按 WinUI 3 默认模板使用 3 DIP、1.5 DIP 圆角的彩色指示条覆盖 1 DIP、0.5 DIP 圆角的中性轨道。当前 canonical dark Preview 使用 `#60CDFF` 指示色、`#8BFFFFFF` 轨道色和 `#FF99A4` 错误色。
- 已显示进度条的成功路径在 tracker 达到真实 100% 后直接补齐 displayed ratio，满格保持 300ms，再用 140ms 隐藏并进入 handoff；Preview 显示后 3 秒内完成则从未显示，也不增加完成停留。
- 致命失败无视 3 秒门，立即以当前实际 fill 和错误红请求一帧；等待 Preview ULW committed event，并在 350ms 总预算的剩余时间保留已提交红帧，然后 popup。

## 10. Bar Frame Bridge 与 Alpha Transaction

### 10.1 成功帧发布

Bar 只有在 `presentCompletion.IsCommitted()` 后才构造 `CommittedBarFrame`：candidate source crop、viewport、screen destination、monitor/work geometry、visual signature、device generation、stable flag 和 revision。复制在同一 render callback 内完成，目标是非 target bitmap；失败则本次 frame 仍可作为 Bar committed，但不发布 proxy/cache，Preview 等下一帧。

### 10.2 Presentation alpha

新增 `requestedAlpha`、每次 frame snapshot 的 `attemptedAlpha` 和只在完整 commit 后更新的 `committedAlpha`。alpha 变化单独形成 presentation demand，强制 ULW 的 dirty rect 为 null/full window；它不虚构业务 scene dirty。任一 GetDC/ULW/ReleaseDC/EndDraw 失败都保留旧 committed alpha，并把下一帧标为 full-dirty retry。

有 Preview 时初始 requested alpha 为 0；无 Preview 时保持现有 255。交接只等待 committed alpha，不以 setter/queue 完成为准。

### 10.3 Layered window 合成约束

两个相同像素的 layered HWND 若同时以全局 alpha `a`、`b` 叠放，有效覆盖近似 `a + b(1-a)`；0.5/0.5 会得到 0.75，并加深透明边缘。因此禁止两个 HWND 做长时间中间 alpha cross-fade。

- **Valid**：Bar 先以 alpha 0 committed 并发布 proxy；progress 隐藏；在单个 Preview target 内用 120-160ms 从 cache bitmap 混合到 live proxy。完成后请求 Bar alpha 255 的 full ULW；其 commit 后立即向 owner post 隐藏 Preview。只允许一次调度 tick 的全量切换，不存在双窗口半 alpha。
- **Missing/Incompatible**：显示 embedded blur；Bar alpha 0 committed 后，在高 sigma 下约 80ms 把 Preview 内输入换成 live proxy，随后 320-420ms 从 sigma 6 平滑降到 0。期间新 committed Bar frame 更新 proxy；sigma 0 后 Bar alpha 255 committed，立即隐藏/销毁 Preview。
- **Corrupt**：忽略 cache，显示 embedded blur；progress 隐藏后 Preview alpha 用 160ms committed 到 0，保持 40ms 全透明，再让 Bar 以多次 full ULW 在 160ms 内从 0 committed 到 255。此路径禁止 live proxy deblur。

## 11. DPI、Topmost 与 Shutdown

- awareness 先于 Display 和任何 Preview/Window Service HWND；`E_ACCESSDENIED` 记录“already configured”而非错误。旧绘图 surface resize 留在其依赖就绪处，不能再承担 awareness。
- Preview 位置由 cache geometry 与当前 monitor snapshot计算；embedded canonical 使用当前 monitor/Bar 默认 anchor 高质量缩放。显示变化时 owner 只应用最新 revision，旧 move 命令丢弃。
- Window Service 暴露可注销 observer；每次 owner-root topmost refresh 成功后 observer 只 post `RevalidateTopmost`。Preview owner 用 `SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE)` 保证在真实 Bar 上方。初次主线程 refresh、周期 TopWindow 和 PPT visibility refresh 都经过同一路径。
- 正常退出/成功交接顺序：停止接收 frame/cache revision -> 注销 topmost observer -> 从非 callback 线程 unregister StartupPreview client并等待 active callback 归零 -> owner hide/destroy HWND并退出 message loop -> stop/join cache worker -> 再按现有顺序停止 Bar/Window/RenderPipeline。

## 12. 错误、Device Loss 与有界等待

| 失败 | 处理 |
| --- | --- |
| Preview HWND/首帧失败 | 注销并 bypass，正式启动继续；Bar 初始 alpha 恢复/保持 255 |
| Embedded header/CRC 无效 | 不尝试危险解析；若无安全最低 Preview 则 bypass |
| Cache Missing/Incompatible/Corrupt | 分别选择对应视觉分支；均不阻止主程序 |
| Blur/shimmer 失败 | 降级普通图像/无 shimmer |
| Cache writer/只读目录失败 | 限频日志，保留旧 cache，主流程继续 |
| RenderPipeline 初始化失败 | 维持当前致命语义，走 red-frame bounded wait + popup |
| Bar/Window/Draw3/Setting/Whiteboard 致命失败 | 显式发布失败，Tracker 冻结；不报 100%，走同一错误流程 |
| Device loss | render thread 清空旧 generation；恢复后重建 Preview 与 Bar proxy。handoff 暂停并有上限，超限时在正式 Bar 可见后直接销毁 Preview |

所有 promise/event wait 都定义有限上限和 stop token；unregister 不在自身 callback 内调用；owner queue 关闭后拒绝新命令；cache worker 只捕获共享生命周期对象或在 join 前存活，防止 callback use-after-free、跨线程 HWND 销毁和悬挂线程。
