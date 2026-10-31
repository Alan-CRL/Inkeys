# 第三轮：开屏缓存与终态滚动条连续性验收

## 结果与实现

接续当前任务，修复艺术字标归位后垂直滚动条出现、页面横移和滑块闪动。BeginScrollChild增加默认false的settleScrollbarLayout，仅主页Pending/Playing完整准备/截图/恢复帧启用。Begin前native使用完整轨道尺寸，Finish按实际ScrollbarY写回0/1；同帧向嵌套正文继承。普通设置页保持160ms显隐动画，无内容溢出时仍没有滚动条或额外留白。NoInputs只依实际导航阻断/取消首帧，未解除或扩大输入屏障。

CompleteHomeEntryLayout在内层EndChild之后、outer EndChild之前仅递归本帧提交的正文子树，按vendored CalcWindowContentSizes的preserve/explicit/ImTrunc64/Ideal规则检查当前测量是否已被native消费。GPU捕获和revealReady发布均要求本帧layoutReady；恢复同样至少两次提交且实际ready。每帧、失败及缓存失效清理就绪，capture/restore各六次不收敛会收束，不能无限等。没有增加固定暖帧常量、全局时钟/设备/shader/纹理或改字标精度。

## 精确回归结果

真实row时钟和动画开启，unique root冷child。先冷准备，再实际child缺席，再捕获，缓存归位，恢复，直接比较第一个实时页面，没有事后追加暖帧。outer与page WorkRect四坐标、ScrollbarY/reserve、content width/clip、hero/brand目的地及按钮bounds均精确相同。截图本来没有内联艺术字标，故只排除已经验证坐标相同的单份字标AA矩形；整个其余target（包含右侧thumb）精确RGBA比较。

| 主题 | viewport宽/倍率 | 滚动条轨道 | 缓存/首实时内容宽 | 背景比较像素 / 差异 | 捕获/恢复提交次数 |
| --- | --- | --- | --- | --- | --- |
| 浅 | 1000 / 1 | 8px | 696 / 696px | 967932 / **0** | 4 / 2 |
| 浅 | 760 / 1.25 | 10px | 380 / 380px | 1056944 / **0** | 4 / 2 |
| 浅 | 1000 / 0.6 | 0px | 803 / 803px | 1052440 / **0** | 3 / 2 |
| 深 | 1000 / 1 | 8px | 696 / 696px | 967932 / **0** | 4 / 2 |
| 深 | 760 / 1.25 | 10px | 380 / 380px | 1056944 / **0** | 4 / 2 |
| 深 | 1000 / 0.6 | 0px | 803 / 803px | 1052440 / **0** | 3 / 2 |

4/3次捕获是这些实际布局的收敛结果，不是新固定阈值；恢复为2次。普通动效控制cold-width704→terminal696，cold-reserve0→terminal8，复现原8px横向跳变。实际Tab取消首帧NoInputs、后续解除、终态SetScrollY、原普通滚动条出现/消失/反转回归通过。CPU值状态回归验证两成功提交但layoutReady=false不能发布缓存、首次true才发布、恢复同样受门禁，以及六次不收敛有界退出。

## 验证

- 最终完整InkeysRepo.sln Debug|ARM64、动态定位原生ARM64 MSBuild，exit0，25.0683s；保留现有warning，未改工具链。进程内Path/PATH规范化与MSBUILDDISABLENODEREUSE=1，无全局环境修改。
- 最终全套InkeysHeadlessTests.exe --no-window exit0，226.2471s，stderr空，无early-stop；SHA256 7B8EAC4A534A2D9A500C83C365EC9E3952DDA4B4FCA4B8AAF6EECBC9BADEC3F3。60组原性能/像素回归及新增六例均完成。
- i18n en-US/zh-TW 384/384，git diff --check(ignore-submodules=all)及8源码UTF-8/BOM/CRLF/结尾审计通过，format-audit.json保存。既有PptCOM.dll SHA256 AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105保持。
- 不GUI/Computer Use，未运行真实窗口人工观察或Win7实机；离屏排除原生caption/业务图片/共享客户端/Present，验证范围不推广为实机FPS。

## Trellis复盘：为何前次未发现

1. 根因类别D（测试覆盖缺口）与E（隐含假设）：前次fixture关闭row动画；固定两成功帧被当成nested AutoResize已经稳定。条宽参与WorkRect，native ContentSize来自上帧，缓存与恢复的跨帧合同缺失。
2. 首个修复直接稳定轨道，横向width/hero检查通过，但新完整WorkRect与RGBA检查仍发现326–354像素差异。诊断capture外层ContentY1214、内层已消费1199/本帧测量1348；首live外层/内层1348。thumb2..896→2..811，342差异像素全部bbox994,842..997,927。字体/成功提交并不证明实际内容高度稳定。
3. 预防：运行时按真实native尺寸门禁捕获与恢复，冷animated真实夹具同时比较X/Y/完整background thumb；默认动效控制证明修复前行为可复现。规范已同步，不能放宽像素容差/只比较宽度/裁掉右侧thumb。
4. 扩展边界：类似缓存/冻结child应分别确认当前测量和native消费尺寸；本轮仅修当前设置入场，没有顺手修改其他页面或vendor。当前8源码最小必要修改，研究记录见research/scrollbar-fix-notes.md与scrollbar-check-review.md。

## 历史失败证据与工作树

attempt1-*为第一版（仅稳定轨道）失败并主动停止的日志，状态-1/128.6466s，不冒充全suite结果。diagnostic-*为保持精确门槛定位thumb的16.7757s快速失败采样，stoppedOnFailure=true。当前无前缀日志对应最终源码，不沿用旧状态。

用户前次commit8a4bdd89仍为HEAD。本轮未commit/push、未归档；任务继续in_progress。原工作树/stash及既有DLL保持，源码/报告待用户继续审阅。
