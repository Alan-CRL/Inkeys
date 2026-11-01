# Research: 命令完成之后的页面准备缺少等待提示

- Query: 三点实际显示过，命令返回后消失，但画布/页码仍需1秒多才就绪；如何用已有真实Pending状态最小化修复？
- Scope: internal；只读源码、规范及人工日志；仅写本研究文件，不改产品/测试，不构建或运行GUI。
- Date: 2026-11-01

## Findings

### 1. 用户新澄清定位了真正缺口

用户补充：第三页点击Next会出现三点，约0.5秒后消失，此时画布仍不能新绘制；再过1秒多，页码才3→4并开放画布。

因此问题不是整段导航完全没有三点像素。最新`144216-578-e2c353b3`中已密封零掉样的3834条PptWait记录、35次accepted Publish、真实dots/ULW/receipt链与该澄清一致。真正缺口是现有等待只覆盖native命令outstanding，未覆盖随后已经观测到的页面准备Pending。

本轮停止额外PNG导出、点位/颜色、GPU/DWM或“用户看漏”的泛化研究。先前“多阶段视觉意图+soft TTL”的初步建议也不作为优先方案：它试图覆盖无证据区间，引入同页完成的不可判别性和额外状态。主会话提出的已有Pending独立投影更小、更可证，建议采用。

### 2. 原始日志证明命令返回与页面就绪分离

采集`Build/Validation/ppt-navigation-20261101-144216-578-e2c353b3/native/idt1793515337757.log`：

| 请求 | native enqueue | native complete | 新descriptor与旧raw失配 | raw/descriptor一致 | page_ui_ready |
| --- | --- | --- | --- | --- | --- |
| Next epoch1 | 14:42:42.876 | 14:42:42.925 | 14:42:43.361 raw3/desc4 | 14:42:44.073 raw4/desc4 | 14:42:44.110 rev2 |
| Next epoch3 | 14:42:46.781 | 14:42:46.826 | 14:42:47.309 raw3/desc4 | 14:42:48.726 raw4/desc4 | 14:42:48.778 rev4 |
| Next epoch5 | 14:42:51.224 | 14:42:51.265 | 14:42:51.777 raw3/desc4 | 14:42:53.224 raw4/desc4 | 14:42:53.261 rev6 |

epoch5的command约41ms，返回到page_ui_ready约1996ms；返回后约512ms才出现记录中的新页失配证据。窄修复只承诺真实PageStateMode::Pending期间有提示，不用猜测把这512ms也伪造成页面忙，不声称严格连续覆盖整个命令到输入开放区间。

这些是同一原生日志的毫秒时间戳，证明先后与约秒级缺口，不代替QPC精确软件时长或光学可见时刻，也不声称某一行就是用户口述的具体操作。

### 3. 当前清除点与真正准备链

| 边界 | 代码锚 | 实际含义 |
| --- | --- | --- |
| 命令清等待 | `IdtPlug-in.cpp:1574-1587` CompleteRequest析构 | 释放native command outstanding并CompleteNavigationWaiting；没有等待新页/Draw3/UI ACK |
| void COM返回 | `IdtPlug-in.cpp:559-605` | S_OK只证明调用已返回，Managed超时/拒绝结果未进入native |
| 等待终态 | `Ppt.cpp:358-370`、`PageControl.cppm:607-615` | 推进完成水位并清current，三点随snapshot消失 |
| raw/descriptor可信 | `IdtPlug-in.cpp:1115-1123,1257-1261` | 新descriptor先于raw时不可信，不开放旧页为新页 |
| Draw3目标/ready | `IdtPlug-in.cpp:1328-1389` | 发布真实目标，成功Draw3 Present后才更新PptInfoStateBuffer和非零targetRevision |
| 页面模式 | `IdtPlug-in.cpp:1405-1453` | 同场未准备好时保留旧数字但模式Pending；真实ready后InkReady；BasicControl独立 |
| UI回执 | `PageControl.cpp:2054-2076,2105-2248`、`PageControl.cppm:747-784` | 真实候选/像素和当前session/target/pageAckEpoch通过后才回执 |
| 业务到Draw3 | `Ppt.cpp:189-199` → `IdtPlug-in.cpp:262-275` | PagePresented检查当前pageState，AcknowledgePptPageUi检查expectedPptUiTarget后发布UI-ready |
| 最终输入门 | `Draw3.Host.cpp:3054-3065,817-864` | UI-ready可先锁存；所有目标/Present/UI及原独立条件满足后才开放 |

修复不应把UI等待清除变成新ready信号。也不能只在PagePresented回调调用处宣称画布已开放：业务callback是void，AcknowledgePptPageUi发布的Draw3 ACK仍可能被拒。

### 4. 优先方案：页面准备等待是已有事实的独立纯视觉投影

保留现有导航命令tracker、Complete水位和native outstanding完全不变；新增PptState纯值`pagePreparing`（或等义最小字段），直接由Ppt模块现有PagePresentationState投影：

- 正式positions仍Active；
- 当前PPT可见；
- `pageState.mode == PageStateMode::Pending`；
- 当前同场recovery为None（已有失败/重试文案优先，不用通用准备提示覆盖它）。

`Ppt.cpp::PublishSnapshot`已在stateMutex内取得positions/pageState/topmostRefresh/pageRecovery（`:138-159`）；应复用同一份快照，尤其recovery先按`pageRecovery.session == positions.session`过滤，再判断None，不能让上一场遗留错误抑制新场准备提示。

PageControl的BuildPageContent在真实PPT模式下，把这个generic preparation OR进两个箭头的既有等待bool：命令阶段仍仅显示请求方向，真实Pending阶段两个方向都显示。它表达“本页正在准备”，不沿用旧请求方向，不要求导航epoch或COM回执证明。白板内容必须显式隔离。

`ArePptStatesEquivalent`必须加入新bool，否则同数字/target的模式变化可能被去重，丢掉RequestAll。当前等待字段已有比较（`PageControl.cppm:828-831`）；复用原changed→publishedRevision→RequestAll→ConfigurePagination→IncludeButton路径即可，不新增调度/窗口/动画系统。

`PagePresentationState::Publish`本来因mode变化推进pageAckEpoch（`Ppt.cppm:168-177`），CanAcknowledge本来只允许InkReady（`:180-188`）。新视觉bool不能额外改pageAckEpoch、mode、targetRevision、页码或ack语义。Pending期间显示旧数字和三点，不产生ready。

当原PptInfo发布InkReady或BasicControl时，pagePreparing和新页码进入同一次UI快照；原成功提交事务会在显示新数字的同帧去掉准备三点。失败提交保留旧像素/旧ready，不能为了去点提前提交新页。

边界：InkReady是Draw3已成功呈现、可提交分页新页的状态，早于最终PageControl ACK/输入门；纯值bool变false不是输入已开放，不能把此修复描述为严格追踪每一项input gate。现有真实帧/ACK链仍原样决定开放时间。

### 5. 这比跨命令意图tracker更小且避免不可判别问题

`NextSlideShow/PreviousSlideShow`为void；同页动画后内容相同，`SlideShowSessionCache.Publish`的fingerprint相同而不递增stateRevision（`SlideShowSession.cs:195-214`）。不改PptCOM/ABI时，无法精确区分同页动画完成与延迟出现新页证据。

基于真实Pending的generic indicator不需要猜这个事实：同页动画未触发Pending就只保留原命令阶段三点并按原Complete结束；若真实数据使页面Pending，则按真实模式显示。无需soft TTL、额外timer、方向记忆、PrepareIntent、回执水位重构或新COM协议。原f1267e12的Complete-before-Publish保护完全保留，旧发布不能复活命令等待。

纯键盘/其他来源造成页面Pending时也显示两个方向的准备点，这是准确的页面事实，不是给旧按钮请求冒充成功。即使Pending长期存在，提示对应真实未就绪状态；恢复失败另由现有recovery状态承担，不加任意超时去伪造已准备好。

### 6. 场景与风险矩阵

| 场景 | 必须行为 |
| --- | --- |
| 命令仍outstanding、页面InkReady | 原session/epoch方向三点，Complete水位/重复合并不变 |
| Complete后页面Pending，旧数字保留 | 两箭头显示generic准备点，覆盖当前确认的1秒多阻塞段 |
| Complete后尚无Pending证据 | 不虚构忙；如epoch5返回到失配前约512ms可没有三点，明确是本窄修复的边界 |
| InkReady发布新页 | 新数字和generic三点清除在同一UI提交；原Draw3/UI ACK仍决定写入 |
| BasicControl / HWND0 | generic准备点随BasicControl清除，不等待不存在的非零target/Draw3 ACK，不伪造InkReady |
| 同页动画/第一页Previous/未执行/managed超时 | 原命令Complete结束方向等待；只有真实Pending才出现generic点，不因页码没变而永久保留请求标记 |
| EndScreen准备 | Pending可显示generic点；真实EndScreen InkReady后清除，不把current=-1一概当失败 |
| EndScreen Next→EndShow、Focus/View/Activate/保存等 | 不创建新导航视觉协议；页面模式真Pending时只表达实际准备状态；退出Active/Visible消失则清除generic投影 |
| 键盘换页 | 如果引起Pending，两个箭头显示准备点；无导航epoch也可正确工作 |
| recovery失败/冲突/恢复中 | 同场recovery原文案、按钮和重试令牌保持；generic点不覆盖它。原outstanding方向等待不由新投影强行修改 |
| 白板、白板往返 | BuildPageContent白板分支不消费PPT generic点；PPT不可见/退场时投影为false，不触碰场次、保存或PPT进入/退出逻辑 |
| 换场/退出/shutdown | positions Active/Visible及pageState原重置使generic为false；不引入异步旧令牌，因此不新增迟到复活路径 |
| 同数字但mode翻转 | 新bool参与状态比较，RequestAll必须发生；不得只依赖页码变化 |

### 7. 最小文件边界与验证

预期只需Ppt模块的纯值投影、PageControl的PptState/比较/BuildPageContent以及相应既有测试；原Bar.Scene已能同时绘制两个等待方向且包含脏区，不必修改绘制、点位、颜色或窗口策略。无需修改IdtPlug-in命令gate/包装、PptCOM.cs、dynamic/手动释放、COM ABI、Draw3 gate或加载顺序。若需要新增窄投影helper用于生产/测试复用，可放Ppt.cppm现有PagePresentationState旁，不建立通用架构。

推荐无窗口覆盖：

1. 生产投影：Active/Visible/同场recovery/Pending矩阵；InkReady与BasicControl都不Preparing；上场recovery不能影响新场。
2. 同页数字仅Pending bool变化使ArePptStatesEquivalent不等价，原Request/配置链可观察到；pageAckEpoch与ready规则不被新视觉修改。
3. CommandNext→Complete→Pending→InkReady，以及Pending中有新Next/Previous、旧Complete→Publish、跨场/退出，验证原水位保护未改。
4. BuildPageContent：generic使两箭头true，命令等待仍只有对应方向；页码/布局不变；白板、不可见、recovery隔离。
5. 复用现有真实离屏24组合：Pending准备像素在两个按钮区域确有差异，partial=full，InkReady/BasicControl清除与新页数字同帧，关闭诊断BGRA相同。
6. 有失败提交/旧ACK时，原ready和旧像素保持，不因generic bool变更提前开放输入。完整Debug|ARM64与适用headless/离屏回归，真实Office最终仍由用户短测一次3→4。

诊断若补字段，记录pagePreparing/page mode及PptWait的原字段即可，仍固定POD与停产后输出。不能用“画过三点”替代Pending期间覆盖验收，也不能借此宣称owner/descriptor长尾已经缩短。

## Files Found

- `Inkeys/IdtPlug-in.cpp`：命令Complete、void包装与PptInfo原Pending/InkReady/BasicControl发布，作为只读事实来源。
- `Inkeys/Inkeys/UI/Ppt/Ppt.cpp` / `Ppt.cppm`：正式场次、PagePresentationState与等待快照投影。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp(m)`：PptState纯值、等价比较、BuildPageContent、实际帧/ACK及既有离屏测试。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp`：已有等待方向OR后可直接消费，不需改绘制。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：原输入门与UI-ready锁存，只读边界，不是本修复修改目标。
- `PptCOM/SlideShowSession.cs` / `PptCOM.cs`：只读确认same-page/void不可判别性，不改COM产品行为。

## Related Specs

- `.trellis/spec/ppt-interop/native-session-ui3.md`：旧页码、命令epoch、Pending/BasicControl/InkReady、Present/UI ACK与墨迹门。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：真实候选/像素提交及失败回执。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：默认关闭数值采样、真实join后输出，无热路径I/O。

## External References

无外部查询；全部来自本地代码与同一人工样本，不扩展Office/WPS支持范围。

## Caveats / Not Found

- 优先确认并修复的是已有Pending准备阶段缺少提示，不是像素/颜色/位置错误。额外PNG研究已停止。
- Complete到首次Pending之间的无证据区间不强行补忙；新投影也不是最终input-open确认。
- 本修复只改善真实等待反馈，不消除实际owner/Observe/descriptor延迟，性能问题继续独立保留。
- 本次未改产品/测试、未build、未启动GUI/Office；行锚是当前快照，后续编辑按函数名定位。
