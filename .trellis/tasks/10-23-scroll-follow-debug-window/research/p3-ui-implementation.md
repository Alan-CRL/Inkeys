# P3 UI / explicit export implementation handoff

## Scope and verification boundary

本角色只修改 `Inkeys/Inkeys/UI/ScrollFollowDebug/ScrollFollowDebug.h/.cpp` 与本报告；此前 P2 报告追加了 main 的安全门阻断结果。Business/CV/Capture/Mag/IdtMain/工程登记由其他角色负责，没有重构这些边界。

P2 的编译、offscreen contract、严格错参证据见 `p2-implementation.md`。main 的真实屏幕尝试在初始化前得到 `blocked=security-interface-before-initialization`，因此 P2 像素排除与 P3 GUI/控件/对话框仍为 **UNVERIFIED（环境阻断）**。P3 按 main 明确批准的 fallback 继续，没有绕过安全门。

本次交接前只做静态检查，**没有执行 P3 build、writer test 或任何 GUI**。完整 solution、短 headless case、真实产品/像素测试由 main 在全部源文件冻结后统一运行；下文区分已实现测试与已通过验证。

## UI behavior

- 原 `Open(HICON, const RECT*)` / private WindowSpec / WindowService lifecycle 不变。普通非 topmost、可调整大小、空 owner；WM_CLOSE 仍走 Service.Destroy，不 PostQuitMessage。
- Created 才 Activate Channel、建立字体与 100ms UI timer；WM_NCDESTROY 杀 timer、释放 mouse capture、销毁 GDI/backbuffer/fonts、Deactivate 自己 generation 的 channel 并取消自己的导出。默认关闭时无 UI timer / producer image copy / writer job。
- 左侧真实 Tracker 输入最大面积保持比例显示；右侧为可调整分隔线、自动折行、滚轮/PageUp/PageDown/Home/End/Up/Down/拖动滚动条的完整数值、stage、check 与 48 格明细。底部显示最近 30 条真实 numeric events，可滚轮查看较旧条目。
- Points / Flow / Grid / Bounds 独立开关；箭头 1x/2x/4x 仅改变绘图；鼠标滚轮围绕指针缩放，拖动平移，Reset / 双击恢复视图；审查已移除新增的 Space/0 功能快捷键，保留普通面板导航键。
- Freeze 停止 ConsumeLatest，图片与选中帧 tracker/statistics/geometry/id 保持同一快照；ReadLive、owner applied、计数和故障事件继续刷新，单独标 LIVE。Resume 消费真实最新帧，不合成 motion。Paused 没有新 Tracker 图像时，保留最后实际图像与拒绝结果。
- 列出实际生产 result、firstFailure、checks 原条件、comparison/threshold/measured、stage reached/completed。UI 不重算阈值，不用某个 condition 的真值代替整个 frame 的失败结论。AND/OR 的联合门有明确解释；短路未执行显示 N/A。
- MotionStatistics 全字段列出；未计算项用阶段/check/affine validity 标 N/A。FB percentile 需要真实 FbPointsLow 记录，不把 Backward 已进入当成 percentile 已完成。JSON 保留 returned raw statistics 并伴随逐字段 validity，不能把默认零解释为已计算。
- Capture accepted/rejected/API、Tracker calls/A/R、preview published/consumed/overwrite/drop 及 preview Hz 分开。ownerLatestSequence / 实际 ownerAppliedSequence+viewport 与 candidate/result/targetPublished 分开。显示 sample/Read/failure/status age、STALE 与实际 CAPTURE FAILURE badge。
- ownsCapture=false 时 Capture-specific 最新行与 JSON 为 N/A/null；刚拥有 session 但尚未分析时 quality 为 N/A/null，尚无 Read completion 时 Read durations 为 N/A/null。选中帧的真实 metadata 不因此被清空。
- NumericSummary.captureFailure 单独保留并完整显示/导出原始故障 stage/reason/time/native error；quality 仅 Quality stage 有效，native error 仅 nativeErrorValid 有效。后续 End/live 状态不能替代该事件。最近历史行和滚动详情均可看到原事件，Capture-only event 的 motion=null。

## Rendering and units

使用现有 GDI 与 DibSurface；raw BGRA 只来自 Channel 的自有同帧紧凑快照，不重新 Capture、不调用 Tracker。

- reference / forward / refined / backward / added.point / flow / bounds / affine sample 为 processing pixels：各轴各除实际 scaleX/scaleY 一次，再统一应用 fit/zoom/pan。401x303 -> 200x151 这种奇数尺度不能简单乘二；不叠加 DPI。
- anchors、candidate/result delta 与 total 已是原图/内容像素，只显示原值。Desktop ROI origin 用 metadata.sourceRect+roi 解释，不把桌面偏移再次加入预览。
- 8x6 是参考投票网格，保留/补点仍按实际当前格计数；未执行分组在格计数与 JSON 用 N/A/null + validity。Ambiguity 可为部分扫描并保留 ambiguityComplete=false。
- 绿色 main consensus 不等于整帧接受，灰色 near-static，橙色 production-evaluated competitor，红色 failed/excluded，青色 added；Forward 在 FB 前为浅黄色。颜色优先 competitor，再 near-static，再 main inlier，原始 flags 完整保留。
- 点形状：x 为失败，square 为 Forward，triangle 为 refine/backward intermediate，circle 为 FB-valid，plus 为已 committed refill；未提交补点仍是 triangle，并在 JSON 中保留 committed=false。retention/refill 在 Commit 前明确为 proposal。
- main reference/current support bounds 与实际 strongest evaluated competitor 分开；虚线 reference / 实线 current；若 actual rejecting competitor 不同，再单独绘制。主标签严格为 `Main Motion Support Bounds`；支持盒只是证据，不是自动 ROI。main 没有计算的 competitor-only originalCellRatio/separation 显示 N/A、导出 null。
- validation/geometry 早退没有 processing dimensions 时只显示真实输入，不沿用旧点坐标。非有限点/边界跳过绘图但保留 JSON 明确编码。

Capture Read 的正确语义：FreezeCapture 原计时在 Backend.Read 返回后立即结束，**排除 Analyze**，包含后端几何/PW/flush/alpha repair，不是隔离的 PrintWindow 时间；sample 时间也不是 compositor freshness。没有移动原计时点。

## Explicit export and lifetime

Export 只在用户点击时 CloneDisplayed，复制点击时 LiveSnapshot 与显示选项；worker 不持 Channel/front、UiState 或产品 HWND。图片、tracker、metadata 的 identity 因此保持一致，即使 UI 后续继续消费或关闭。

一个进程级 ExportManager 拥有一个 std::thread，绝不 detach；旧 generation 尚未完成时新窗口也不能覆盖/启动第二个任务。worker 自建 STA，包含选择目录和共享 PNG/JSON writer；完成时先释放 COM/GDI/克隆，再发布 done，后续 Start 才回收线程。

目录选择使用原生 IFileOpenDialog FOS_PICKFOLDERS/FORCEFILESYSTEM/DONTADDTORECENT/NOCHANGEDIR，Show(nullptr) 不引用可能销毁的产品窗口。独立 message-only cancellation window 的 100ms timer 在同一 STA 调 IFileDialog.Close。普通 WM_CLOSE 只置取消，不等待 worker，不让 Mag/UI 被目录选择重入阻塞；取消开始前、选择中、写入前和各文件之间均检查取消。worker 不向旧 HWND PostMessage。

`ShutdownExports() noexcept` 提供给主线程/fixture 的有序退出：取消当前 job，移走 owned thread 并 join，必须在 CRT/图像运行时资源全局销毁前调用。probe/main worker 已在正常与已分配调试窗口的 fixture cleanup paths 接入。manager destructor 另有兜底；普通关窗不做 join。文件系统/API 内部调用仍遵守操作系统完成行为，本实现没有强行终止线程或在 Mag 等待。

选定父目录后只建新的 scroll-follow-business-capture-sequence-tick-attempt 子目录，不覆盖既有用户目录。共享 writer 输出：

1. `raw.png`：点击时完整 Tracker input，原尺寸，保持原像素。
2. `preview.png`：同一输入在完整 ROI 原图 1:1 上绘制相同 marker renderer；保留开关/箭头倍率，交互 zoom/pan 记录但不裁剪 PNG。GDI 标记后的 alpha 明确置为不透明。
3. `snapshot.json`：完整同帧 metadata/quality/statistics/geometry/input/processing尺度、原生产 result、raw statistics+validity、全部 stages/checks/old+added points+flags/48 cells/candidates/support、click-time live status/counters/recent events/options/units/legend。unevaluated 为 null；非有限数为稳定字符串 NaN/+Infinity/-Infinity，保留其性质并保证有效 JSON。

失败/取消可以留下本次新建的部分目录，status 明示；不自动保存、不自动重试，不写日志截图到用户未知位置。

## Picker HWND exclusion integration

选择器不是 WindowRole 调试窗口，也不是其 owned popup。不能由排除 debugger 角色推断它也被 Mag 排除：Microsoft 的 MagSetWindowFilterList 定义的是显式 HWND 列表。[MagSetWindowFilterList](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowfilterlist)

新增 `HWND ExportPickerHandle() noexcept`：仅原子读取当前句柄，无跨线程 COM。worker STA 在 OnFolderChanging/OnFolderChange（初次打开也触发）、selection/type/file events 及 cancellation timer 中 QI IOleWindow/GetWindow，验证当前进程、当前 STA thread、IsWindow、非 WS_CHILD 后发布；所有 Show return、cancel、RAII cleanup 清空。Microsoft 说明可从 IFileDialog 查询 IOleWindow 后 GetWindow；OnFolderChange 会在首次打开时调用。[OnFolderChanging](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialogevents-onfolderchanging), [OnFolderChange](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialogevents-onfolderchange)

p1_review 已在原 BuildCurrentFilterList 每次取这个 getter，并沿用现有 IsWindow/current pid/top-level/dedupe 验证；没有缓存表、新 capture 或 WindowService 角色改造。接口/所有权静态成立；首次可见时序、实际 picker 像素排除、选择/取消及关闭/退出仍未实际 GUI 验证，不宣称零帧遗漏或 pixel PASS。

## Read-only real-window Probe

保留 `Probe ReadProbe() noexcept`，内容来自实际 Paint 后发布的观察结果，不允许注入图像、变更 Freeze 或伪造 applied sequence。

- open/frozen/exportBusy/hasFrame、points/flow/grid/bounds、arrowScale、zoom/pan。
- generation/business/capture/frame/reference identities；refreshes/consumed/paints/previewHz；displayAgeMs/copyMs/totalTrackerMs；实际 ownerAppliedSequence 与 viewport。
- 实际 client 坐标 previewRect/imageRect、10 个 button rectangles。button 顺序为 Freeze/Points/Flow/Grid/Bounds/1x/2x/4x/Reset/Export。
- previewBackground 为 RGB(22,28,38)，仅 Capture-only 无帧时适合作为中心实屏 GT。960x640 outer 初始窗口仍给出大于 256x192 的中央预览，waiting help 限于 top+10..30，zoom 提示在底部 20px。
- paintSamplePoint=(client.right-12,12)，paintSampleColor=RGB(28,34,46)，是自然顶栏文字裁剪区外的留白，没有新增人工测试画面。product fixture 用该真实屏幕 3x3 像素与 WindowFromPoint 验证可见性，已填满纹理的 preview 不能再假设中心背景色。

## Added small writer/mapping test (not yet run)

`#ifdef INKEYS_MESSAGE_BOX_TESTING` 提供 `int RunExportTests() noexcept`。cv_diagnostics 已将它加入现有严格 `InkeysHeadlessTests.exe --scroll-follow-debug-only` 分派；不创建 GUI/目录选择器/独立 exe，不模拟另一个 renderer。

测试在仓库 cwd 的既有 ignored Build/ScrollFollow 下创建全新私有目录并检查沿途不是 reparse point。使用真实共享 writer 和现有 DibSurface PNG decoder：

- 奇数 401x303 原图，200x151 processing，各轴精确不同 scale；带非零 roi offset、完整 identity、raw constant pixels。
- Clone 后清零原对象，验证 raw PNG 全像素仍为克隆内容，两个 PNG 都保持 401x303。
- 人工原图 GT 验证 cyan committed refill 像素(204,152)、4x green flow arrow 像素(170,100)、current support 像素(40,70)、未标记像素(380,280)。GT 直接按已知原图坐标指定，不只镜像实现的坐标公式；可发现简单乘二、二次 DPI、raw/preview 混用。
- JSON 可 parse、完整 identity、原 delta=5.25 与 display arrowScale=4 分离；JumpHigh 未执行 null、FB percentile 未完成 validity=false、main 的 competitor-only 字段 null、未执行 cell group null+validity、Capture-only motion=null。
- 原 API failure Stage Read/nativeError=5 保留，quality=null；Quality failure 的 NearBlack 保留且无效 native error=null；未拥有/no-sample capture 不出现伪造 InvalidBuffer/Read0。
- NaN timestamp / Infinity measured 可 roundtrip 为明确字符串，仍是有效 JSON。
- 写盘前已取消时没有任何 PNG。

成功精确输出：`PASS scroll follow shared export writer / odd mapping (no GUI)`，另打印本次目录。该输出只验证 writer/renderer/serialization，不等于选择器/GUI操作或真实 Mag 排除成功。

## Static checks actually run / handoff

- UI `.h/.cpp` UTF-8 无 BOM、纯 CRLF、无 trailing space：PASS（字节检查）。
- scoped `git diff --ignore-submodules=all --check`：exit0。两 UI 文件为本任务新增 untracked 文件，另做逐行 whitespace 检查，避免 diff 不覆盖新增文件的问题。
- 未启动 build、headless executable 或 GUI。没有提交/推送、没有安全门绕过、没有 SDK/PDB/toolchain 改动。

main 后续使用现有 ARM64-native MSBuild + 完整 InkeysRepo.sln Debug|ARM64、同 invocation PATH normalization、/nr:false；完成后先运行严格短 `--scroll-follow-debug-only`、offscreen exit-visibility case 与严格错参，再独立处理真实 GUI/pixel/product 控件/perf 证据。GUI 若仍阻断需保持 UNVERIFIED，不以 writer test 替代。
