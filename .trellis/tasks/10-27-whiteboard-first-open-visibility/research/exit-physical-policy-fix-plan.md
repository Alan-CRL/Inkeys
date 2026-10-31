# Research: 退出白板的最小物理交接接线

- Query: 返回Desktop、鼠标点击的退出异常，如何复用已有物理相对排序，移除退出对全局置前的依赖，同时避免继续增加切换层级和判断框架？
- Scope: internal；仅源码/已有研究与规范，产品、项目及Git均未修改，未编译或运行GUI/测试。
- Date: 2026-10-27
- Baseline: ab79dcb9；本方案细化 exit-native-window-investigation.md，以下简化边界优先于其较宽备选建议。

## Findings

### 1. 本次必要修改可以限于一个私有物理提交方法

建议在Window.cpp复用已存在的 `ReconcileRelativeOverlayGroup`，新增一个小型private方法，例如 `CommitOverlayGroupPolicy(command=nullptr, promoted=nullptr)`。它只负责：

1. 保留原来必要的root存在、命令当前性、promoted/Bar等入口校验。
2. `CaptureGroupZOrder()` 获取这一次操作的固定HWND/generation和真实PPT顺序。
3. 用 `CapturePptZOrder(*before,promoted)` 调用已有 `ReconcileRelativeOverlayGroup`；内部仍有owner、band、代际、Bar/PPT和子序列读回、两轮上限、NOACTIVATE/NOOWNERZORDER及固定root继承。
4. 成功后更新既有 `ApplyOverlayFullscreen(root)`，最后确认当前命令。失败保持false，不吞真实API/读回错误。

它不是新状态机，不需要scope枚举、额外phase/epoch、策略层级、公开API或新后台任务。业务依然是已有两态显示/隐藏及必要的一次窗口政策交接。实际APPWINDOW/TOOLWINDOW、owner、root band和CanvasPolicy语义保留，以免退回此前焦点/任务栏问题。

### 2. 最小接线表

| 当前入口 | 建议调用 | 不能改变的行为 |
| --- | --- | --- |
| Execute `SetWhiteboardWindowMode`（2866–2867） | 原Apply mode成功后调用私有物理提交 | mode/CanvasPolicy真实失败仍完整恢复 |
| Execute `SetOverlayTopmost`（2868–2871） | 写既有policy后调用私有物理提交 | 读取实际目标band，不只检查缓存 |
| Execute `SetOverlayFullscreen`（2873–2876） | 写既有policy后调用私有物理提交 | Shell最新绝对值、拒绝迟到TRUE保持 |
| 完整Restore `snapshot.group`（2649） | 把最后普通Refresh替换为私有物理提交 | Native/Canvas/显隐真实回滚仍执行，不以物理排序代替它们 |
| 白板模式下普通Refresh/Promote | 可直接复用同私有方法 | promoted调整PPT子序、Bar缺失/代际拒绝保持 |
| Desktop/PPT正常RefreshTopmost、Promote、Setting附加/PrepareSettingZOrder | 保持原默认全局刷新 | strict continuous、原生owner联动、Setting点击/带内Raise合同保持 |
| 已有Z-only Restore（2588–2602） | 保持按保存zOrder的恢复 | 失败Promote必须恢复请求前PPT原序，不能改成从当前重新采序 |

无需给所有调用增加新purpose或scope标记；三个政策命令和完整回滚本来就是可以直接选择调用的私有入口。正常全局Refresh保留为另一条已有方法。

### 3. 保持既有生产顺序，并覆盖整段重入

rendering-and-ui.md:1644与IdtState.cpp:2859–2862要求：

`Fullscreen(false) → Leave mode → Topmost(true) → ClickThrough`

不修改该顺序，不把topmost=true前置。新方法适用于mode=true和mode=false的政策交接，因此：

- 第一次Leave时policy仍false，真实目标为非置顶；只收敛内部关系即可提交mode=false。
- 后续Topmost(true)真实把root/owned链恢复置顶band并读回，但不要求Bar去争全局TOP。
- 若Topmost或ClickThrough后半失败，下一轮从已经mode=false的Fullscreen(false)重入，也只走同一物理方法，不重新进入Desktop全局门。
- 幂等Leave(false)和Topmost(true)重复同目标仍核验真实内部状态；没有新的APPWINDOW切换或普通全局置前作为前置条件。
- 完整回滚无论snapshot.mode=true还是false，最后的物理完成条件都不再依赖foreground/外部continuous；原请求过期仍须先撤回真实副作用。

不能只修第一次Leave，留下mode=false重试和完整Restore继续调用全局Refresh。

### 4. 本修复不必扩大完整mode快照

当前完整模式快照Native列表只保存逐窗身份、owner、bounds、style、visible/iconic；原实现没有请求前PPT子序的专用恢复目标。其最后普通Refresh本来就是采集**回滚当时**的PPT序再保序，见Window.cpp:2541–2585、2649、1976–1993。

这次目标是替换最后的全局完成门，并不新增Promote语义，也不承诺增强完整mode事务的PPT原序回滚。因此可以保留完整Native capture/恢复语义，只让最后私有物理提交同样保留当次PPT序。相对排序可能修复Bar/PPT违法前态；真正原Promote失败的旧序责任仍由现有Z-only保存的GroupSnapshot负责。

若为完整mode新增请求前GroupSnapshot并将其作为全链恢复写集，会进一步涉及Native写回筛选、父代际退休和整Canvas回调的目标过滤，超过本次最小修复。当前没有必要为移除退出全局硬门引入这组扩展。

### 5. 必需安全边界与相邻旧风险分开

**本次必须保留**：

- 新私有方法的排序写入固定于本次Capture得到的HWND/generation；仍用既有Current/Read/两轮真实校验，不写新的角色代际。
- 原完整Native恢复的逐窗generation检查，既有safetyOverrides对后到Hide/穿透的覆盖，shutdown隐藏收束和原命令回执/恢复责任均保留。
- 私有物理提交的有效band继续来自当前已恢复的mode/topmost政策；只在root真实band不符时切band，非root继承同一固定root，不增加逐成员全局TOPMOST。
- Shell仍只发布最新绝对状态，不能为了让退出完成而虚构COM成功、主动激活或改变全屏协议。

**相邻已有风险，不为本次扩大**：完整Restore逐窗gen核对之后，`EnsureCanvasPolicy(snapshot.mode,true)`仍是当前pair/Freeze+Drawpad策略写者。如果以后要把完整恢复严格绑定请求前整组身份，必须一并审它对替换代际的影响，不能只过滤最后relative而声称全恢复已具备同样保证。该问题在本次前就存在；本次不新增capturedGroup写者、不改变完整Canvas目标语义时，应保留其既有边界，不能顺手引入新的快照/退休架构或以跳过真实Canvas恢复换取false→true。

`CanChangeChainRole`（3517–3541）也限制带存活后代的chain角色动态替换，且普通Create/Destroy受现有恢复屏障约束；不能把任意代际混合当成已观测的本次现场。但新的排序方法自身仍须保留现有严格固定身份。

### 6. 调用面核对与变更范围

产品源码搜索显示，SetOverlayTopmost/SetOverlayFullscreen的实际业务调用只有IdtState白板进入、清理和退出（2693–2706、2859–2861）。Setting准备经PrepareSettingZOrder→RequestTopmostRefresh，不依赖这两个setter周期全局抬顶。因此让政策setter提交物理状态而保留显式全局Refresh，是较小的行为收敛。

涉及的实现边界可限于Window.cpp及其现有私有策略头/无窗口测试（如果确需共享调用逻辑）；无需改公开Window.cppm、UI主栏布局算法、背景token或更多状态变量。IdtState原生产顺序保持。

### 7. 不用纯predicate冒充私有dispatch验证

最简方案不必为测试新增层级框架：

- 动态继续调用真实 `ReconcileRelativeOverlayZOrder`，FakeOps仅提供原生读写环境。增加topmost=true/false目标、外部穿插、后台全局TOP被拒、连续同目标、后半失败后的再次物理收敛、mode=false恢复目标，以及真实owner/band/代际/API/readback错误。
- 原生私有方法自身只是实际Capture→同helper→Shell/Current；三类命令及完整Restore的调用映射必须独立源码审查与完整solution编译确认。报告明确：这没有动态穿过整个Service私有ExecuteTracked/Capture/Restore。
- 若现有私有头中确有必要抽取生产共用的callback调用接缝，应让生产方法实际调用它，测试physical回调继续执行上述真实helper、global回调注入拒绝，证明调用了正确分支和实际物理算法。只测一个“是否Physical”的predicate、复制一个退出状态机、旁置布尔不变或源码字符串匹配都不足以验证私有dispatch。
- 不为追求“动态全私有Service”而创建第二套窗口模型/宏替换全Win32 API/新公共测试接口。已有生产helper动态测试+私有接线静态审查的边界应如实记录。

## Caveats / Not Found

- 已确认返回Desktop+鼠标，仍未记录此次第一个失败API/后置条件。此补丁针对确定存在且可阻止整段退出完成的全局硬门，不能把点击墨迹机制也写成已被唯一定位。
- 原有输入/工具/Draw3-ready研究为独立问题；本计划不增加白板专用广泛输入暂停或提前宣告Inactive，不靠一笔新墨迹完成正常退出。
- 本报告不实施产品、不运行编译/CLI/GUI、不修改Git或其他报告。UTF-8无BOM、CRLF。
