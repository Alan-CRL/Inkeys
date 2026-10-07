# UI3 U04-B/F 真实呈现合同独立设计复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 只读真实 AGENTS、保存 hook/check.jsonl 引用、PRD/design/implement、父 handoff/performance、U04-R 独立报告及实际生产 Bar/Scheduler/输入/SVG/MessageBox 代码；只写本报告，不改产品、工程、spec、账本，不构建或运行 EXE/GUI。

审查对象：`research/ui3-real-presentation-contract.md`，SHA-256 `604B4C65F1332BE7EFDD7C741C5A06D76C85D5284326B7DE690ACB0800D6FF2B`。该文档包含下一实施单元，新的真实 Bar CLI/目标 observer/资源 payload/输入 seam 尚不存在。

## 结论

**整份合同 NEEDS_REVISION；B1 单元 GREEN_DESIGN；B2/B3/F 待接口冻结；F 暂无运行许可。** B1 的真实提交点与固定分段已能在实际代码中定位，可由唯一 writer 先补实现并独立复审。完整目标/资源/source 合同还需下列最小补充；不需要因此重做 Scheduler、UI 图、动画/缓存算法或全局注册所有 animation。

root 已接受分单元门：B1 可先行；后续 B2 将 SVG 资源 ready 纳入有限 proof 或标 unverified，输入 token 围绕实际业务接受发布，Raw Input/GetMessagePos/hover/cursor source 另冻结。此接受不是本报告对未实现接口的 GREEN。

## Findings (fixed)

本 reviewer 未修改产品或原设计。旧 U04-R 的 raw capture 已有独立源码 GREEN；本轮没有新的产品机械修补，也不重复给 R 的编译/测试计 PASS。

## B1：真实事务和分段可以先实施

- `Bar.RenderLoop.cpp::CalculateDirtyAndDrawPresent` 约 12623、12695、12701 的实际顺序是 GetDC→UpdateLayeredWindowIndirect→ReleaseDC→EndDraw→HandleFrameEndDrawResult→presentDecision.CompleteAttempt。只有 CompleteAttempt.IsCommitted 才写 `presentCommitted` 并进入 dirty/viewport/eraser/几何/startup 成功发布。deferred 分支约 12670 已提前 return，失败保留请求。新 steady_clock stamp 放在 CompleteAttempt 后的 diagnostics 块、任何成功快照/Preview 通知之前成立；名称为 software Bar transaction commit，不能叫光学像素可见。
- `RenderPipeline.cpp::RawRecordCallback` 406 附近仍把 `activity.commitTicks=sample.endTicks`，它是 callback-end proxy；旧链保留，另设 hasBarCommitStamp/true ticks/独立 success serial。失败/defer/backoff/Idle 未调用不得拿 endTicks 补真成功。stamp 必须验证 start<=commit<=end，非法/缺失保持分母；同 run/generation/epoch/activitySegment 连接真实成功，idle/重注册/换 epoch 切断。
- `FrameStage` 当前六项在 `RenderPipeline.cppm` 91 附近，formatter 的 stageNames 在 `.cpp` 106 附近也是六项；追加阶段必须同步大小、名称、镜像/tests/schema，保持原六个序号。FrameStageTimer(nullptr) 在 281 附近确实不读钟。新 raw payload 仍预算 `sizeof`/64MiB，不能扩大未记录容量。
- OnFrame 的实际链是 WakeAndSnapshot→ApplyDisplayTransition→SubmitTargetsAndLayout→AdvanceAnimationsAndDeriveLayout→PrepareLightingAndDemand→CalculateDirtyAndDrawPresent。DirtyAndPrepare 结束在原 Draw timer/BeginDraw 9880 前；EnsureDeviceResources 9202 是其 Resources 子段。Draw/GetDC/ULW/ReleaseDC/EndDraw 和 present lock 继续原边界。Resources/SVG/path 是 inclusive 子段，报告不得加总成另一个整帧；未细分 direct drag 等留 callback remainder，GPU 等待/执行不从软件 wall 猜值。
- 默认 off 验收仍要核实际 diff：raw/诊断都关时无新数组/TLS/阶段时钟，target/source/resource sidecar 只在明确 capture/fixture 配置存在；不改 Tick/duration/dt clamp、Request/idle/pacing、dirty/retry、cache key/budget、分辨率/画质/输入。B1 红绿用实际共用 CompleteAttempt+stamp helper；假 callback 手写 presentCommitted 只能证明 Scheduler，不能代替真 Bar 四 API。

## Findings (not fixed)

### P1-U01：SVG 目标完成必须有实际资源/内容 proof

**设计缺口，未改。** B2 第5项目前是 domain pending=0、target 已消费、revision 一致和完整事务成功才 complete。对 SvgContent，仅这些条件不能证明目标语义已经画出。

实际 `Bar.Rendering.cpp::BarUIRendering::Svg` 约 2723–2800 先按 color/size/transform 查 cache，再调用 `svg.CacheBitmap`。颜色变化刷新失败或无旧 bitmap 会 return false，该 SVG 本帧没有画出；尺寸质量刷新失败且有相同语义旧图才保留旧图继续 DrawBitmap。其余 Bar 绘制和 EndDraw/ULW 仍可能全部成功。`Bar.UI.cpp::CacheBitmap` 400 附近分别可能 parse/raster/CreateBitmap 失败；cW/cH/cColor 的成功缓存值仅在最后写入。content transition 在 329 附近也有独立 generation/parse 结果。domain 的值已 settled 不等于 D2D 使用了目标内容。

最小修订：每个场景仅为实际涉及的 SVG/cache producer 旁挂纯数值 ready/semantic revision/epoch/生成尺寸或颜色 proof，锁存本次 candidate 实际使用的成功 bitmap proof；失败/旧质量 fallback/跳过绘制分别记录。无法证实预期内容或质量时记 unverified/unpresented，不能完成 SvgContent，更不能丢这些 step 缩小成功分母。无需持 COM、hash 用户文稿或每像素读钟；业务刷新/保留行为保持原样。暂时只做 Main/DrawAttribute observer 时明确未覆盖 SvgContent。

验收：同一实际 Svg/CacheBitmap 的首次成功、复用、颜色/content/size/epoch失效；parse/raster/upload 失败而 Bar ULW 成功不能确认 target，恢复后使用匹配 proof 的真实成功帧才 complete。BGRA 质量只在独立等价验收回读，不污染正式采样。

### P1-U02：accepted/consumed/candidate 的有限接口与发布次序尚未冻结

**接口门，未断言当前产品新增 bug，未改。** `Bar.Interaction.cpp` 3298 附近 mainButtonClickPulseSerial 先递增，随后才写 barState.fold/清浮层/UpdateRendering；它只证明一次 branch 接受佐证。`BarRenderLoopState` 和 Interaction 持有 shared state 引用，不能将 pulse 或一个随 wake 变化的 demandGeneration 当作目标 revision。

合同说“前后 revision 不一致标 ambiguous”方向正确，但还没有冻结 typed payload、writer 次序、各 domain 的有限 signature/pending 属性和读取规则。若业务值在 token 发布前已变化，render 前后读到同一个旧 token 也不能直接证明同一目标。不得 commit 时重读最新 state 倒填旧帧。

最小修订：先冻结 Main+DrawAttribute 的数字 request/accepted/consumed/candidate/complete DTO、真正接受分支及其修改前后发布界限；可用有限 publication serial 和实际 target signature 比对使中途写入不可确认，也可复用现有已锁定目标快照的真实 serial。render 只从稳定有限快照锁存 candidate；未知/混合/superseded 保留分母。列出生产 AdvanceAnimation/result.active、IsSame、相关 timeline/keyframe、Dock spring/FineDial physics 的具体成员归属，避免再扫描全部动画。

验收：接受分支在业务更新前/后暂停，render 不能错绑；快速反向、late batch restart、同 target/no-op、最后 settled 帧失败→恢复、epoch/idle 与 hover/light持续的反例。same-target 已成功态单列，不伪造0ms动画。动画完成要等该目标最终成功 commit，不能用第一次 Idle 或单一 timeline 结束。

### P1-U03：F 只是来源方案，exact capability/source 接口未获运行 GREEN

**运行前设计门，未改。** 现 `ShutdownSupervisor` auth capability 只覆盖 fatal 四站点；它不能直接授权 benchmark。F 必须有独立 purpose/version/mapping byte 布局、有限 scene/capture/capacity 的 exact 解析与继承 parent/PID/file identity/private 非 reparse bin/current EXE 校验；任何拒绝都在 config/互斥/HWND 前直接 early return。仅复制 EXE 不构成副作用隔离。

真实 Bar 所需内部 init 能由同 module 的 `Bar.Presentation.Test.cpp` import :Main 调用，不必导出 InitializeWindow/UI。`Bar.Initialization.cpp` 121–208 的完整 Initialization 启动 MouseHook，所以 early fixture 要按实际子步骤组装字体/I18n/UI/buttons/Display/Rendering/Interact，再返回 Main。`Bar.Button.cpp::Load` 1287 附近会 config.Write，private globalPath/config seed 必须先建立并核路径。所有 builtin callbacks 仍在图中，source allowlist 只能触达已审的本地工具/layout/颜色/粗细；Explorer/Lock/ESC/AltF4/PPT/Office/系统业务不得被轨迹触发。

实际消息路径可行但须具体接线：WM_TOUCH 785 分支生成带 screen marker 的 ExMessage→Window::Enqueue→PrepareBarInteractionMessage 368→真实 BarInteractionSession 5432→Seek 6061。单纯普通 WM_MOUSE* 不成立：QueueWindowMessageInLayoutSpace 6772 读取 GetMessagePos；WM_MOUSEMOVE 944 调 ActivateBorderCursorTracking 并可能 RegisterRawInputDevices。source 私有消息只能携数字 index，读取 child 内预建 immutable 轨迹，必须调用生产 marker/队列/交互，不传任意指针或伪造系统 WM_TOUCH HANDLE。保留 enqueue 拒绝/丢消息/Down-Up-Cancel/contact 分母与业务接受证据。

Seek 的 Touch-tag 可用真实屏幕点绕过 mouse 的 GetAsyncKeyState/GetCursorPos 循环，但 final indicator veto 6732 仍读 OS cursor；SuppressHoverUntilPointerMove 1416、CommonHover 2381、WM_TIMER 552、FineDial/颜色拖动 4293/4698、cursor light 5711/5774/5897 也会读 OS。仅 screen-point/left-state 的概念 provider 尚未证明覆盖所有需要的 scene；synthetic touch 也不自动激活真正 mouse light，而原 light 有 Raw Input 注册状态前提。

最小修订：冻结只在 exact auth child 内、绑定 active synthetic contact 的有限 source DTO/lifetime、own HWND dispatcher 和上述每个 scene 实际读取点/方法；生产默认仍调用原 OS。mouse/light 需明确复用哪个真实采样入口且不启用全局输入/改用户系统状态；无法隔离的 scene 标 AMBIENT_INPUT_CONTAMINATED/NOT VERIFIED。不能删 veto、关光影、直接改目标或跳 Seek 做快结果。F 完成实际 diff 后另做独立运行前 safety review，先一 scene 再三轮。

### P2-U04：B3 资源 producer/DTO 需冻结，统计口径可用

**接口细化，未改。** 文件选择正确：SVG parse/raster/upload 在 Bar.UI.cpp，最后 draw/cache lookup 在 Bar.Rendering.cpp；只加 Rendering 的 timer 会漏 parse。rounded/geometry/exact 的容量删除与 epoch/reset 失效是不同事件，成功 create 必须和 attempt/failure 分开；SVG 旧图 fallback 仅同语义质量刷新允许，不能泛称所有失败保留。

资源合同目前列 family，未冻结固定数值计数/阶段/父子关系与各 producer 的真实 API 成功提交点。先限定一个 family 再扩展，明确 capacityEvict/replacement/invalidate、ready entries/logical bytes、不可得值。InitializeUI 的 parse 在无 callback TLS 的线程，冷资源用有限 scope汇总，不倒填第一帧或造全局跨线程 map。保留现有缓存 key/预算/质量，不顺手优化。lunasvg公开 renderToBitmap 不能拆出内部 geometry/tess/GPU 时间。

## 实施和验证门

| 单元 | 最小出口 |
| --- | --- |
| B1 | 实际四 API + CompleteAttempt 的成功一次/任一失败/defer无 stamp，时钟因果/epoch/idle/overflow、formatter/schema/预算；默认 off 实际 diff 和完整编译/strict no-window。 |
| B2 | U01/U02 typed finite proof 冻结后生产 observer 红绿；最后失败帧不完成，same-target/reversal/drop/timeout/unverified 全部有 terminal 分母。 |
| B3 | 单 family actual lookup/create/failure/evict/epoch API 和像素等价；冷/warm资源分别记，不加所有高频 primitive 计时。 |
| F | U03 exact auth/early-return/source/lifetime 实码独立 CLEAR；真实 Bar role/WndProc/HiMsg/Rendering/Interact/ULW，hidden/visible flag逐项核，不把offscreen手搭Scene升级真Bar。 |
| F 退出/等价 | source stop/Cancel→实际Close/原15s监督→Interact join→StopDisplayTracking→StopRendering同步Unregister→自有Setting Shutdown如有→Window.StopAndJoin→Scheduler shutdown/join→Take一次→离线输出。owner join前不得撤 probe。on/off同轨迹/业务终态/Down-Up-Cancel/像素等价，采样off不能从poll造帧分位数。 |
| 数据 | child自然exit0+sealed+scene terminal+无overflow/invalid污染、原始callbacks/batches/targets/resource summary；parent清理不能当PASS。零样本/不足1000的P99为null，前缀掉样不能当全程尾数据。 |
| 性能 | 每scene同一Release/设备/效果/轨迹fresh三轮，cold/first/warm/reversal/idle/long分群，至少16个完整目标warm-up与200个目标变化；callback/attempt/truecommit/Retry/idle分开。真实render GetThreadTimes才是线程CPU，wall/GPU/逻辑cache bytes分别命名；采样期间root独占slot停构建/其它bench。 |

门槛沿父冻结：median 超轮间噪声且>5%、P95超噪声且>10%才是实质退化候选；尾延迟/输入/画质分别验收。32K容量是否足够由drop决定，不能挑最好一轮。Settings竞争必须自有真实Setting/ImGui hidden activation另冻；首次Bar-only或fake Settings不能关闭该场景。

Oray/hiddenULW只能支持当前环境的软件事务与可得CPU/资源，不能推导光学延迟、真实笔/Touch、实体屏主观流畅度、GPU成本或指定HC/H2胜出。H0加诊断补丁须标instrumented；旧binary没有新raw，内部tick不可与旧主观评分直接排序。Win7 SP1仅KB2670838、FLIP与两个DWM禁用保持独立真机门，设计/本机编译不记Win7 PASS。

## Verification

- Lint：未运行；研究分工只读，未改产品。
- TypeCheck / Build：未运行；完整 C++ Solution 类型/链接门由 root 串行执行，不能以文档 GREEN 代替 MSBuild。
- Tests / EXE / GUI / 基准：未运行；无本轮动态 PASS。已核实际生产 API/owner/资源调用和冻结文档 hash。
- Spec：R 的 sink||raw TLS、B1真实stamp/schema、后续finite proof/resource/source边界应在实码完成后由root同步 ui3-render-diagnostics；不提前写成已实现。
