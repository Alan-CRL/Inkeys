# 白板退出异常：调查汇总与下一步边界

## 已提交与本轮范围

已验证的白板首开/后台闪烁修复已按用户要求提交：`ab79dcb9a6466236f1170469104964b0cafab711`，标题 `fix(whiteboard): stabilize background entry and window recovery`。使用既有SSH签名配置，commit含SSH签名，未push；用户两份Draw3 CSO和旧09-06设置任务未纳入。

随后按用户“先调查可能出现问题的地方”仅做源码调查。没有修改新问题的产品代码，没有GUI/窗口/进程/PDB采样，没有为只读调查重跑构建或测试。此前两项测试通过属于刚提交的旧修复，不等于新退出问题已覆盖或已修复。

用户新观察：退出白板后背景和分页消失，主栏未归位，层级/任务栏持续跳变；点击屏幕产生墨迹后主栏恢复，显示选择/有墨迹穿透。此次返回Desktop还是PPT、输入设备类型仍待文本澄清，不要求新增GUI复现。

## 1. 可直接确认的退出中间态

| 时机 | 已经发生 | 仍未发生 |
| --- | --- | --- |
| IdtState.cpp:2727–2735 | 收分页、隐藏背景、phase=Exiting；开始恢复目标工作区及工具 | Bar的独立whiteboardActive仍为true |
| :2822–2843 | 等目标Draw3真实ready、背景inactive ACK | 尚未完成原生窗口模式/层级/穿透交接 |
| :2859–2872 | fullscreen(false)→LeaveWindowMode→topmost(true)→主/辅穿透，任一false继续Exiting | 不发布Bar false或Inactive |
| :2873–2879 | 所有必要步骤成功才发布Bar false、Inactive | 此时Bar才能按目标工作区恢复 |

Bar.Main.cpp:503–521关闭白板标记会主动请求渲染。Bar.RenderLoop.cpp:12916在该标记仍true时保持Whiteboard；Desktop基线恢复位于:12937–12953、13057–13073。分页使用另一个active/expandedLayoutTarget，提前消失与Bar仍保留白板位置可以同时成立。

返Desktop应Leave并恢复保存几何；返PPT应Switch并保留原Desktop基线。这两个结果不能混为同一种“上移”。未见正常Bar退出通知必须靠点击补一次唤醒的证据。

## 2. 首要候选：退出仍依赖旧的全局排序硬门

确定源码链：

1. Window.cpp:2867要求`ApplyWhiteboardWindowMode(false) && RefreshOverlayGroup(...)`。
2. Apply在:2283先把mode写成false；Refresh:1978只有mode=true才使用刚修复的相对排序。
3. 因此退出落到:2003–2043的普通全局TOP及strict continuous/Bar>PPT门。
4. 生产在Leave成功后的IdtState.cpp:2861才恢复topmost=true；所以Leave时有`mode=false + overlayTopmost=false`这个组合。
5. mode命令不属于四类Z-only。失败进入完整Restore，在Window.cpp:2612–2649恢复mode=true并强制CanvasPolicy/激活样式、Hide/FRAMECHANGED和显隐恢复。
6. 回滚可能成功，状态线程又立即重试退出；于是能够反复切窗口APPWINDOW/TOOLWINDOW身份及其他画布策略，而背景/分页逻辑早已隐藏、Bar退出标记仍未提交。

该结构是确定存在、与现象高度相关的剩余风险。它来自77f76038引入的退出排序/通用恢复链；ab79保留了mode=false的普通分支，没有直接修改实际State退出尾段。新观察到问题，不足以断言ab79新改坏了Bar或退出顺序。

但本次没有首次false的具体stage、原生BOOL或退出前后快照，不能断言本次必然失败在Leave而非更早的ready/ACK/SetFullscreen或更晚的SetTopmost/ClickThrough。

正常退出先撤fullscreen，再Leave，所以Leave失败的快照通常已是fullscreen=false。任务栏跳变不能直接写成Shell MarkFullscreen TRUE/FALSE振荡；窗口身份/样式/Hide/FRAMECHANGED与全局重排反复，是更直接的源码候选。

## 3. 点击、墨迹、选择外观仍有证据缺口

### 已确认的分层

- SwitchWorkspaceStateMode在IdtState.cpp:1129–1176早期恢复Desktop/PPT记忆，目标无记忆才默认Selection；普通关闭白板并不一律强制Selection。
- stateMode/Bridge先发布，Host.cpp:1619–1724后续Pump才写实际WindowController选择/工具，存在异步区间。原有ControlWake会主动推进，不能仅凭此断言它持续到点击。
- Host基础输入准入:807–836不读取WhiteboardPhase；activationAllowed=false仅影响激活，不等于禁止落笔。若Desktop已ready且目标记忆为Pen，窗口仍接收输入，Exiting未收尾也可能落笔。
- Mouse fallback在实际Selection时明确拒绝；RTS/Controller在Desktop Selection依赖正确窗口穿透，旧不穿透样式被恢复时有另一个条件候选。输入设备未知时不能把二者合并成一个确定原因。
- Bar落笔通知只更新内容、重绘或按配置自动折叠，没有“落笔自动切Selection”的规则；视觉变化不证明工具在那一刻刚切换，也不证明phase在那一刻才完成。

### 两类候选

A. Draw3 ready已满足，但原生窗口尾部未收敛；点击改变最近输入/全局排序条件，让下一轮事务完成。与持续窗口样式/任务栏扰动相符，但本次前台许可变化未被观测。

B. 返回场景还欠一次真实输出/auxiliaryFullFrameClean回执；点击触发帧/输入活动和runtime wake后收敛。此门在IdtState.cpp:313–330、Host.cpp:990–1019，不能仅凭“点击后正常”排除。

选择图标、旧实际工具、旧穿透样式和墨迹归属需要分别验证。目前不能唯一确定此次墨迹来自哪条路径，也不能声称已定位实际首个失败项。

## 4. 稳妥的最小修复方向（尚未实施）

优先保留现有`Fullscreen(false)→Leave→Topmost(true)→ClickThrough`顺序，把模式/政策事务必须完成的真实物理合同与普通Desktop周期全局置前分开：owner、band、代际、内部PPT/Bar顺序、样式/几何/显隐/CanvasPolicy仍按实际结果核验，普通全局Refresh独立。完整group回滚也不能以外部全局连续性作为物理已恢复的必要终点。

必须覆盖后半失败的整段重入：Leave已成功而Topmost/ClickThrough失败后，下一轮fullscreen(false)已经处于mode=false；不能再次掉回全局硬门。仅改单个白板if、调换一次Topmost顺序或提前清Bar标记，不能证明这条链完整收敛。

工具/输入部分先补生产定向回归，核对目标工具、实际工具、原生穿透及Host admission；在这些身份确认前避免过渡落笔，不能把activationAllowed当作输入闸。没有本次明确阶段前不扩大成背景token/公开API/整套Draw3重构。

## 5. 必要的离屏回归

- 严格按生产退出顺序构造mode=true、topmost=false、隐藏Middle/背景分页已退场，模拟全局TOP受限或外部穿插；内部物理合法时无需落笔即可完成并触发Bar归位。
- 每一窗口步骤失败多轮、资源健康后恢复；覆盖Leave后Topmost/ClickThrough失败和mode=false完整回滚，保留真实恢复责任与新epoch取消。
- 真owner/band/style/region/visibility读回失败不得假成功，纯排序不能重复建立APPWINDOW以取得前台。
- Desktop基线只恢复一次，返PPT保持Switch；不以AcceptedStroke替代退出通知。
- Desktop Pen/Selection两组记忆、延后真实Host Pump/输出ACK、mouse与RTS路径分别验证，区分原生尾部门和新成功帧门。
- 使用真实生产helper/现有CLI接缝，不复制私有Service算法冒充动态测试；GUI和跨系统实机结论继续留空。

真实Window fixture原先预置topmost=true并Show全部成员，与生产Leave前仍false的顺序不同；--no-window也不运行它。此前PASS不能排除此缺口。

## 分报告

- [原生窗口退出调查](exit-native-window-investigation.md)
- [状态、工具与输入调查](exit-state-input-investigation.md)
- [主栏布局与外观调查](exit-bar-layout-investigation.md)

本轮产品源码保持ab79dcb9状态。保留本报告及任务范围更新供后续定向修复，未为新问题创建commit或push。

## 独立只读复核

Trellis check 已对汇总及三份分报告复核，未发现事实性阻断；确认工具早恢复、Bar晚提交、实际失败阶段与输入归属未知、现有离场顺序及重入/回滚边界均表述准确。未修改产品或执行运行时验证。
