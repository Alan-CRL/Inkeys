# Research: PPT 页码偶发内容跳变动画

- Query: 当前页/总页数只变数字时应直接更新，为何偶发缩小或淡出再出现？
- Scope: internal；只读代码/规范/历史任务，未修改产品、测试或规范，未构建/GUI/提交。
- Date: 2026-11-01

## Findings

### 1. 约定确实存在，当前数字直更分支也仍存在

`.trellis/spec/native-desktop/rendering-and-ui.md:445`规定：旧、新内容均Numeric时主/次槽SetStringImmediate；任一Status则主/次TransitionToString。初次Configure直接初始化；工作区变化的槽位/字号/按钮几何仍连续动画。`.trellis/spec/guides/code-reuse-thinking-guide.md:38`重申不能用surface级关闭动画一并关闭Arrow/Add语义转换。

归档`.trellis/tasks/archive/2026-10/10-30-page-status-transition-interaction/prd.md`的Goal/Requirements/AC明确普通数字翻页即时，PPT recovery及白板switching才显示加载/等待文字并做中点过渡；design.md明确旧新类型判别，而不是按字符串猜类型。

### 2. 生产路径核对

| 边界 | 当前行为 |
| --- | --- |
| `UI/Ppt/Ppt.cpp:138–160` PublishSnapshot | 当前/总页、recovery及pagePreparing同锁拷贝；准备bool不改页码kind |
| `UI/Ppt/Ppt.cpp:323–335` PublishPageState | 更新原pageState并发布Snapshot，不驱动文字动画 |
| `UI/PageControl/PageControl.cpp:920–976` BuildPageContent | 默认Numeric；generic准备/command waiting仅映射两个箭头三点。只有PPT recovery.kind!=None转Status并用原失败/恢复/重试文案；白板switching转Status Loading/Wait |
| `PageControl.cpp:1185–1205` ConfigureSurface | observedRevision变化可配置；duration>0仅针对真实workspace modeChanged；same-mode传0并不等于直接重建Scene |
| `UI/Bar/Bar.Scene.cpp:395–397` Configure | 已initialized、同surface、同whiteboard则进入UpdateContent，不重提交布局目标 |
| `Bar.Scene.cpp:333–350` UpdateContent | Numeric→Numeric两槽SetStringImmediate；其余类型转换两槽TransitionToString。只数字变化不重做布局。旧32.5DIP紧凑页宽分支受BarPptCompactPaginationEnabled=false禁用 |
| `Bar.Scene.cpp:443–464` Full Configure | 两个字符串即时初始化；字号/槽位仅在真实配置重建/模式变化依direct/duration决定几何动画 |
| `Bar.Scene.cpp:379–380` waiting bool | 仅IncludeButton(Previous/Next)脏区，不修改Page槽类型或字符串 |

因此刚提交的pagePreparing不会直接把数字改成Status。纯Numeric→Numeric的当前/总页数直接路径没有静态发现丢失取消或误调用TransitionToString的错误。

### 3. 最匹配现象的可达候选：短暂Status未显示，就恢复数字

用户明确“跳变”是数字缩小或淡出再出现，不是整按钮位置跳动。这与共享文字contentScale/contentPct动画一致。

`Bar.UI.cpp:814–839` TransitionToString的幂等条件是：active且transitionContent.target等于新target，或inactive且content.val/target均等于新target。active状态下并不因为当前可见content.val等于新数字就取消动画。

可达序列：

1. 稳定Numeric，主槽3、次槽/56。
2. 一次真实短暂recovery把kind改Status，目标为Recovering/Wait（或其它恢复/错误文案），两槽启动TransitionToString；尚未到中点，屏幕仍主要显示旧数字。
3. recovery很快消失，返回Numeric，数字4、总数/56。Scene只看到旧kind=Status、新kind=Numeric，仍调用TransitionToString。
4. active旧target还是状态文案，不等于4或/56，故两槽重新Start；总页数即使仍/56也会缩小/淡出再出现。甚至返回3、/56（可见数字完全未变）也会Start，因为inactive同值分支不适用。
5. 若之后没有新的数字变化，UpdateContent的currentChanged/totalChanged/kindChanged均false，不再触发Numeric→Numeric即时setter；状态退出动画可以完成。

`Bar.UI.cpp:896–930`明确把scale从当前值移向middleScale（默认0.8）、pct降至0，然后回到1，吻合用户描述。它不是已证实的现场根因，但这是无需假设数字直接分支失效就能产生相同观感的具体代码序列。

### 4. recovery不能与普通Pending混为一谈

`UI/Ppt/Ppt.Recovery.h:35–64` Project要求非白板、CanvasRecoveryStatus非None、非零revision、存在同session presentationTarget，并且RecoveryMatchesDesired。Retrying/Recovering映射Recovering；TopologyConflict映射Conflict；LoadIoError/Rejected映射ReadFailed；PresentFailure映射PresentFailed，其余Failed。

普通raw/descriptor失配Pending并不会自动满足上述条件。`BuildPageContent`的pagePreparing只显示三点，仍是Numeric。所以不能因页码等待就断言出现了Recovery/Status。若现场没有recovery.kind轨迹，应保持候选而非判定。

用户明确偏好当前三点；本调查不恢复原加载/等待文字、不改产品。三点表达正常命令/页面准备，原文字表达recovery及白板switching，是当前两类不同状态来源；是否以后统一属于另一个设计决定。

### 5. 现有测试与证据缺口

`PageControl.cpp:3892–3970`现有真实离屏测试包含普通PPT/WB数字即时、初次Status无动画、Status进入中点前/后、同目标幂等、Status→Status打断、Status→Numeric最终稳定、Numeric再次更新取消旧中点，以及全局动画禁用。

现有测试对“离开Status后再来一次Numeric更新”有明确取消断言，但没有把“Numeric→Status未到中点→唯一一次Numeric（主数字变或不变、总数不变）→仅相同heartbeat”的用户可见过程独立定性；这种路径目前按历史设计会有动画，不是现有测试必然失败。

最新PptWait仅记录准备/导航/软件事务，不含BarPageContentKind、recovery.kind、实际content.val/transition.target、timeline代次/进度及选用Immediate/Animated策略。因此即使有成功三点提交，也无法据其证明上述Status候选在现场发生。现有UI3Diag是聚合，`recovery(...)`字段是渲染设备恢复计数，不等于PPT recovery.kind，不能混读。

### 6. 最小后续验证

优先复用现有无GUI离屏入口，增加一个纯测试场景（本轮不写）：稳定3/56→Status目标，仅推进小于中点→Numeric4/56；同时覆盖回3/56和总数/56不变，随后只发同target heartbeat。记录每帧实际val、target、scale、pct、timeline active和content kind，用真实BGRA对照判断是否重现用户观感。并与始终Numeric3→4/56控制组比较，不关闭整个surface动画。

若该场景可重现，只证明机制可达；现场还需窄证据确认是否经过Status。可在现有默认关闭数字capture内采“旧kind/新kind、recovery kind、选用Immediate/Transition、主/次内容仅数字分类、timeline活动/中点状态”，不记录标题/用户文本，不在render写日志。也可先从有明确恢复状态事件的现有运行材料核对，不立即要求用户重复大矩阵。主会话只读最新152235材料没有Recovery专用行，但旧采样未覆盖该事实，因此不能由缺行排除短Status。窄取证还应携带同session/target、旧/新数字是否变化和是否已到中点，不采具体文字/路径/截图。

未来若现场确认这个候选，最小修复评估应只针对“Status尚未实际显现就恢复Numeric”的过渡取消；保留真正已显示状态的必要动画及Arrow/Add语义动画，不全局关闭，不把原文字提示恢复为普通Pending的显示方式。该策略仍需要明确实际显现的软件判据与并发/中点回写回归，本轮未实施。

如果现场始终Numeric，则应转查谁在改文字contentScale/pct或页按钮press反馈；Full Configure/工作区字体槽动画属于不同现象。当前未找到纯数字直接路径的确定违规，不能泛化为所有页码动画应关闭。

## Files Found

- `Inkeys/Inkeys/UI/Ppt/Ppt.cpp`：pageState/准备/recovery发布。
- `Inkeys/Inkeys/UI/Ppt/Ppt.Recovery.h`：真实恢复状态到UI kind投影。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp(m)`：内容kind、workspace配置、真实离屏回归。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp(m)`：旧新kind策略与内容/几何分离。
- `Inkeys/Inkeys/UI/Bar/Bar.UI.cpp`：共享文字即时更新、中点过渡与scale/pct。
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`：共享文字绘制和按钮pressScale，非新分页动画实现。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md:445`
- `.trellis/spec/guides/code-reuse-thinking-guide.md:38`
- `.trellis/tasks/archive/2026-10/10-30-page-status-transition-interaction/prd.md`、`design.md`

## External References

无外部资料或SDK/版本结论；完全依据当前仓库静态代码与历史文档。

## Caveats / Not Found

未运行构建、测试或GUI，未修改产品。没有这次偶发跳变的现场kind/timeline轨迹，不能把短暂Recovery判定为已发生。行锚来自本轮静态快照，后续编辑应按方法定位。只写本研究文件，保留三点偏好，不恢复文字提示。
