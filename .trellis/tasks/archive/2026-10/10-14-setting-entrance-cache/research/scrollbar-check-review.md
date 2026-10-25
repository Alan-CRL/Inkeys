# 第三轮独立检查：缓存与终态滚动条一致

## 已确认根因与验收盲区

- 生产 `Setting.cpp` 调用 `ConfigureRowAnimations(navigationDelta, AnimationEnable, AnimationSpeedRate)`；原离屏主页 `Frame` 固定 `ConfigureRowAnimations(0,false,1)`，因此原像素对照未覆盖生产的滚动条进出动画。
- `ScrollbarStyleScope` 在 child 缺席后把 tween 重置为 0，用 `Pixels(ScrollbarTrackSize)*phase` 与同样缩放的 padding 参与真实 `BeginChild`。这是实际布局宽度变化，不只是装饰透明度变化。160ms tween 与两次捕获/恢复暖帧不能等同稳定终态。
- AutoResizeY 子页/主页区块的高度要经过多层实际 Begin/End 后传播，冷布局捕获必须确认 overflow 与最终 WorkRect；不能只热页面后断言。
- `NoInputs` 本身没有直接隐藏滚动条。生产外层 `NoInputs` 来自 navigation 的 BlockContentInput 或输入取消首帧；`CancelHomeEntryOnInput` 正常 active 且无输入时返回 false。fixture 应匹配该实际条件，避免把整段 EntryActive 当输入屏障。

## 检查边界

仅检查当前开屏事务：截图排除唯一字标/蒙版，但正文、滚动条轨道、页面宽度、hero 原点/宽度、文字换行必须与正常终态一致。不能为所有页面永久强制垂直滚动条，也不能通过关闭全局动画或扩大像素容差掩盖跳变。恢复时 child 缺席后的 phase 重置同样要受一致布局保护。

源码和测试由 scrollbar_implement 独占修改；reviewer 只写本文件，避免并行写源码。完整 ARM64 Solution 构建和无窗口测试由主会话统一执行。未运行 GUI/Computer Use、未 commit/push。

## 最终差异与验证

实现停写后已完成最终复核；本文件保留以下中途检查与失败诊断，最终通过结论及证据见末尾 Verification。

## 产品差异静态审查

- `BeginScrollChild` 增加默认 false 的 `settleScrollbarLayout` 参数，仅实际主页 active/Pending 开屏正文传 true；正常页面/导航与 popup 调用保持默认动画行为。
- 策略沿父窗口 `##scrollbar-settled-frame` 当帧标记传给嵌套 child，不跨窗口全局启用；每个普通 Finish 写 -2，旧帧标记不满足当前 FrameCount。因此开屏结束后不会泄漏到普通导航或其他宿主。
- 稳定策略在 Begin 之前提供完整 native 轨道样式，Finish 仍以真实 `ScrollbarY` 选择 0/1，不强制显示条。无溢出时 phase=0，不执行退出装饰 reserve；有溢出时同步 value/target/start/progress 并保存当帧，恢复 child 后首个普通 live 帧可从连续帧的 1 继续，不会再次从 0 起跳。
- 源码没有修改共享设备、RenderPipeline 限帧、Present、vendor、字标网格/时序、准备帧预算或 GPU cache 资源规则。新增关键逻辑有中文原因注释。

## 回归覆盖静态审查

- `CheckEntryScrollbarContinuity` 使用唯一 root 建立冷 child 层级，先原有两次实际暖帧，再缺席 12 帧，恢复两帧捕获，cached Dock 无 child 生成，最后原两恢复帧后立即比较第一 live，未用额外事后暖帧掩盖问题。
- 浅/深各覆盖正常倍率 1、760px 窄窗倍率 1.25、倍率 0.6 无 overflow；同时保留普通 scrollbar 动画控制，两个冷布局帧的宽度与 30 帧稳定宽度应不同，以证明修复前确实存在布局变化。
- 比较真实 ScrollbarY/reserve、可用正文宽度、content clip、hero/brand 目标和按钮命中；整个离屏 target 的精确 RGBA 比较只排除已验证固定几何的唯一内联字标 AA 矩形，未放宽全图容差，右侧滚动条仍参与逐像素检查。
- 生产外层 NoInputs 已接入 fixture；实际 Tab 取消首帧检查屏障与无 HomeAction，下一帧解除。自然结束后显式原生 ScrollY 目标可继续消费。
- 最终 fixture 已增加外层和内层 WorkRect 的 Min/Max 全字段精确比较，直接对应真实 item 可用空间验收。

## 当前结论

这一阶段产品静态差异未发现需修复的问题；当时尚待主会话统一验证。最终编译/Tests结果见末尾 Verification。

- 停写后检查：四个源码均 UTF-8 无 BOM、CRLF；bare LF=0、double CRLF=0；git diff --check 退出 0。同步 rendering-and-ui.md 的签名/主页开屏范围/当帧继承/实际 overflow/默认动画/NoInputs/冷回归要求与最终代码一致。

## 第一轮动态验收发现（已通过实际尺寸门禁修复）

- 主会话完整 ARM64 Solution build 退出 0，23.0087478s。首轮 headless 在已出现确定回归失败后被主会话停止（状态 -1，128.6466366s）；不能把该次认作全套通过。
- 新回归的四个 overflow 场景（浅深×scale1/窄1.25）均通过轨道宽、正文可用宽、hero/brand/clip/button几何，但 outer/page WorkRect 精确比较及缓存/首个live RGBA 仍失败；像素差异分别342/354/326/348。两例无条scale0.6的像素差异均为0。
- 普通animated控制 cold-width704→terminal696，cold-reserve0→terminal8，确认最初8px布局跳变被真实控制复现。
- 已反馈主会话和实现代理，建议诊断具体WorkRect轴/ContentSize.y与像素包络；vendor WorkRect.Max.y及thumb比例均依ContentSize.y，不能忽略Y字段或排除右侧thumb像素后宣称符合用户要求。该阶段尚无最终通过结论，后续最终结果见末尾。

## 原生高度就绪补充审查

首轮精确失败来自右侧thumb：capture的旧ContentSize与当帧正文高度不同，first-live已变为新高度，宽度修复本身不足。修复继续保留所有精确字段/像素断言，在控件条宽终态策略之外加入实际 native 布局就绪。

- `CompleteHomeEntryLayout` 在内层页面 EndChild 后、外层 content EndChild 前调用，CurrentWindow 正是正文 outer。只在 managed/fullFrame/EntryActive 事务执行，递归本帧 Active 且 LastFrameActive 匹配的真实 child 子树。
- 每层以 vendor `CalcWindowContentSizes` 同样的 collapsed/HiddenCanSkip 保留条件、ContentSizeExplicit 优先和 ImTrunc64(CursorMaxPos-CursorStartPos) 计算 current/ideal，精确比对 native 已消费 ContentSize/ContentSizeIdeal。未修改 vendor 内部尺寸，未跨帧保留窗口指针。
- 每个 Begin 清 `layoutReady=false`；完成当前真实子树后才重写，失败同样清 false。截图 Render 门禁和 Complete 发布都要求该本帧 ready；恢复至少两次成功且 ready 才发布。reveal/restore 至多六次成功提交不收敛即收束，失败沿原有 attempts 限制，不扩大固定暖帧常量猜测收敛。
- 热 cache Frame 不递归正文；requires actual submission 的准备/截图/恢复不会依重复提交 skip 伪造成功。无额外 GPU/CPU 快照资源或每帧容器。
- mode9/mode11 手工性能计数仍调用真实 Frame 的 CompleteHomeEntryLayout，layoutReady 来源实际子树；纯 CPU 状态测试显式设置 true 仅模拟完成事实，另验证假 captured=true 且 readiness=false 不得发布、第三帧实际 ready 后才能 capture/restore，以及六帧恢复不收敛收束。

此补充静态检查当时尚待主会话最新源码测试，第一轮失败不作为最终通过依据。

## Findings (fixed)

- File: Setting.Controls.h/.cpp、Setting.cpp。
  Issue: child缺席后160ms条宽tween从0重新出现，捕获及恢复的真实WorkRect宽度与正常终态不同；旧普通控制真实复现8px差距。
  Fix: 仅开屏正文完整帧启用settled策略，沿当帧父子标记继承；native依据实际overflow决定0/1并同步tween终态。普通页面默认false且无条窗口无额外reserve。
- File: Setting.Pages.h/.cpp、Setting.HomeEntryCache.h、Setting.cpp。
  Issue: 两次成功提交不证明多层AutoResize的native ContentSize已消费当前内容高度，第一版剩余326–354像素差异全在右侧thumb。
  Fix: 本帧正文子树按native preserve/explicit/ImTrunc64规则验证current和ideal尺寸，capture与恢复均要求至少2次成功且实际layoutReady。每帧/失败清就绪、六次不收敛收束，未扩大固定等待阈值或像素容差。
- File: setting_home_warp_tests.cpp、setting_design_tests.cpp、rendering-and-ui.md。
  Issue: 原fixture关闭row动画且缺少生产NoInputs/页面限宽条件，漏测跨帧条宽/高度一致性。
  Fix: 真实动画冷层级、浅深三场景、完整outer/page WorkRect及除唯一字标AA矩形外精确RGBA；普通动效旧控制可复现差距，滚动/实际Tab取消与CPU就绪/两种六次预算回归同步。人工capture/restore基准明确断言真实资源操作和readiness；规范接口和合同同步。

## Findings (not fixed)

无未解决代码或规范缺陷。真实GUI窗口/DWM/Present与Win7实机未测，属于用户授权及本次离屏验证范围边界，未外推通过。Reviewer只写本检查报告；上述修复由指定实现代理完成，没有并行改源码或运行构建。

## Verification

- Lint/静态门禁：pass。git diff --check退出0；8源码严格UTF-8解码、无BOM、CRLF，bare LF=0、double CRLF=0。此仓库本次未新增或运行独立通用C++ linter。
- TypeCheck/编译：pass。主会话最终完整InkeysRepo.sln Debug|ARM64、ARM64原生MSBuild退出0，25.0682736s，未更换工程或工具链。
- Tests：pass。主会话最终完整InkeysHeadlessTests.exe --no-window退出0，226.2470515s，pid36928，stoppedOnFailure=false，stderr长度0，末尾PASS animation correctness。SHA256=7B8EAC4A534A2D9A500C83C365EC9E3952DDA4B4FCA4B8AAF6EECBC9BADEC3F3。
- 六例浅深冷捕获/第一live精确背景差异均0；各例比较967932/1056944/1052440像素。正常/窄overflow的captured与first-live宽度分别696/696、380/380；无overflow803/803且reserve0。实际capture次数4/4/3（浅深一致）、restore均2，全部native-layout-ready=1，原60组测试完成。
- 最终scrollbar-validation.md、continuity-cases.json、task.json验证记录的退出码/耗时/hash/6例数据与新无前缀日志一致；qualityGate=passed、status=in_progress，无本轮commit/push/归档。

最终检查通过，没有需留待处理的代码问题。停写。
