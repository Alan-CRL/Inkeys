# 调查结论与待审批范围

- 日期：2026-10-18；基线13783fe7，任务保持planning。
- 用户补充：版本可能20261010b/20261014a，未确认；仅被拖动控件停住，主栏仍可操作；`-/-`可能已由刚完成COM修复覆盖。
- 本轮无源码/测试/spec/配置改动，无GUI/Office、构建或产品测试，无commit/push。研究者分别负责独立主题，结论由主会话交叉汇总。

## 四项需求结论

| 用户问题 | 生产证据与证据等级 | 审批方案 |
| --- | --- | --- |
| 抬手后微移 | PageControl.cpp:947-960 fallback会0.4秒缓动，2353-2406的Up不结束动画；2052的commit按候选版本/两面mask可早于实际几何到位。源码可推导余移时序，未实机复现 | pair级最终几何落定，含released pending；实际到位后才完成提交；Up合法终点不遗漏 |
| 侧竖控件吸附 | PageControl.cppm:1064-1103只有Clamp；1038-1039证明0偏移即主体中心对齐屏幕水平中线。Bar.BottomDock.h:17中轴常量40 DIP，:16底边20 DIP | 使用同一40常量，按现有布局屏幕DPI/96一次换算、不乘控件倍率；先Clamp再试中心碰撞，中心不可行不阻止可行自由移动 |
| 拖动局部停住 | 原始XY候选碰撞会整份回退；inputLocked/单client Retry可以与Bar正常并存；既有try_lock/latest-wins/transition续帧均已存在。暂无唯一故障根因 | 保留有效约束，复用trace并补少量限频拒绝/门禁/pending原因；用生产仲裁注入验证续帧/恢复，只修证实断点 |
| 翻页 `-/-` | 0eb1c7c5已支持同场busy保留旧可信数字、HWND0 BasicControl和独立重试；新session从未有可信数仍合法未知 | 本任务先回归，不重复改COM，不消除合法未知/不提前开放墨迹 |
| 控件被Inkeys画布盖住 | 8a25cfcd已修复静态/动态Freeze→Presentation→Drawpad→PPT/Bar链，是两个候选版本及当前HEAD共同祖先 | 保留现链并回归四控件/两个画布的组合；外部PPT放映窗穿插、HTTRANSPARENT、业务session/inflight拒绝分开定位 |

## 额外条件风险（不冒充局部停住根因）

PageControl.cpp:2258持非递归renderTransactionMutex后2273调用SetCapture；旧capture若为同owner线程另一个PPT控件，会收到WM_CAPTURECHANGED并在2428重入同锁。这是根据源码及微软消息语义得到的条件性自等链，未实机触发；影响共享owner/renderer，与主栏持续正常新交互不吻合。

建议批准后拆为“短锁决定→锁外capture→短锁复核并建新tracking”，旧capture取消完成后才建立新pair tracker，GetCapture依赖的长按判断放在capture完成后；保留Down立即操作一次、repeat timing快照与stale session/epoch门禁。不改成递归锁。

## 最小实施范围与边界

- 核心生产文件PageControl.cpp/.cppm；测试page_control_tests.cpp及PageControl.cpp已有离屏入口。常量header只include，不改Bar完整交互或新建共享层。
- Released pending和同pair两面都必须落定，不能只看被拖surface.dragging；保持dragCommitMutex→snapshotMutex顺序，callback/Flush继续锁外。
- 侧栏吸附40屏幕DIP是显式待批准选择；Bar自己的configZoom使同常量不保证所有倍率下相同物理px。当前PPT按主屏同源Display快照布局，沿用现有策略，不扩为跨屏拖动系统。
- 暂无额外COM/Window链新缺陷证据，默认不改PptCOM、ABI、配置、窗口TOPMOST策略或已归档任务。
- 旧交互稳定修复579d1c5e也已在两个候选版本中；PageControl.cpp/.cppm两版本blob一致，0eb1c7c5只接线ack epoch，不能归为最近COM提交引入的拖动变化。

## 验证计划与未验证

批准后用真实生产bounds/候选/仲裁接缝验证Up后多帧不再漂移、单client Retry恢复、两面提交/失败回滚、capture同步旧Cancel与新tracking顺序、DPI/倍率/边界/碰撞吸附。完整构建复用InkeysRepo.sln Debug|ARM64和ARM64 MSBuild，同invocation规范化PATH，至少5分钟（拟900秒）；运行现有--no-window。

真实鼠标/触摸、USER32捕获转移、四HWND层级命中与Office/WPP验收仍须另获GUI授权并记录，现为NOT VERIFIED。安装包/现场日志/线程栈缺失，不能宣称局部停住已复现或旧版本所有报告均已修好。

## 阅读入口

- prd.md：用户需求与本轮调查验收。
- design.md：具体合同与最小技术方案。
- implement.md：审批门、实施步骤、自动/GUI验证。
- research/drag-release-and-snap.md：微移、最终采样、吸附与常量/单位证据。
- research/drag-hang-locks.md：线程锁图、局部状态、capture风险与验证接缝。
- research/page-state-and-layering.md：页码和窗口链、已有修复及输入不命中。
- research/history-and-baseline.md、planning-baseline.json：只读Git证据和关键文件哈希。
