# 2026-10-26 真实窗口吸附闪烁复现

## 当前要求和进入修复门

- 延续现有任务，不新建任务；不提交、不推送。
- 目标是用户描述的剧烈抖动/位置闪烁，不能以普通果冻、已证明的上边上移或透明 HWND 外框变化冒充。
- 用户授权原生/命令行可见窗口测试和输入重放，明确不用 Computer Use。
- 未观察并记录符合原报告的症状前，只允许诊断/测试工具，不修改产品行为。
- 用户补充：不录屏最明显；录屏时肉眼有所减少，录像几乎看不到。首轮不录屏、不逐帧截图、不调用 DwmFlush。

## 当前基线与已知证据

- HEAD: a7dc3a08，前一修复 1ba08698。
- 当前头文件的独立 ARM64 探针：既有 bottom-dock 和 window-geometry 测试无失败；固定指针、底边 1820→1824 px 时新映射顶边 1658→1654.465116279 px，旧端点独立映射顶边保持 1658 px。这仅证明几何回归。
- 旧离屏 samples.csv 记录 874×169→874×191 的候选 HWND 尺寸变化，target=2006×946。这仅证明尺寸变化路径仍存在。
- Bar.MultiTarget.Test.cpp::ReceivePresentation 仅检验 HDC/参数，且最终提交 sink 不调用真实 ULW。既有测试不证明源像素或真实桌面正确。
- 普通产品 Bar 标题 Inkeys BarWindow，类名前缀 Inkeys5;，由 IdtMain.cpp 的 AddOverlayWindow 生成。
- 现有 --ui3-presentation-benchmark 使用真实 HWND/ULW，但隐藏且只有点击；外部输入被拒绝，不能作为本轮可见拖动复现。

## 本轮修改边界

只更新本任务记录并新增独立原生重放/观测工具。产品源码、动画参数、窗口/渲染算法保持基线。

- research/live-dock-probe.cpp：限定 PID 的窗口枚举、显式坐标 SendInput 重放、有界 GetWindowRect/GetCursorPos 内存采样、单张桌面截图。
- TestResults/ui3-live-dock-2026-10-26/：编译日志、私有产品副本及配置、采样输出与图片，均不提交。
- 若外部证据不足，再根据已取得的现象决定是否增加默认关闭的产品诊断；不得直接进入预扩或几何修复。

## 执行步骤

1. 使用现有 InkeysRepo.sln Debug|ARM64 和本机 ARM64 MSBuild 确认基线产物。
2. 在私有目录复制基线产物及现有配置，记录 hash/显示环境；只操作本次启动的 PID。
3. 一张静止截图确定主按钮和任务栏位置，之后停止抓屏。用真实 SendInput 按住主按钮，多次跨越吸附区；覆盖慢/快、纵向/斜向、中心附近，并设置有限时长及 Escape 取消。
4. GetWindowRect/WinEvent 异常仅作为线索；核对可见内容或用户现场观察后才记录为原症状复现。SendInput 未必完全等同实体 Raw Mouse，应记录输入来源边界。
5. 必要时对可重复异常做短时像素观察，明确抓屏对复现率的影响。没有捕获到，不记 PASS。
6. 复现后才设计单变量诊断对照或最小修复；修复应在同一轨迹和观察口径下验证。

## 执行记录

- 原生 ARM64 MSBuild 完整 Solution 基线构建退出 0；日志 TestResults/ui3-live-dock-2026-10-26/baseline-build.log。
- 原始剧烈位置闪烁：尚未确认复现。当前不得开始产品修复。

### 首轮可见窗口与重放前置检查

- 私有基线进程 PID 14096；主栏 HWND 0x3d0c96，class=`Inkeys5;Inkeys`，title=`Inkeys BarWindow`。
- 原生 DPI=192；显示器 `[0,0,2880,1920]`；工作区 `[0,48,2880,1856]`；初始 HWND `[876,1676,2004,1874]`。
- 单张静止截图 `TestResults/ui3-live-dock-2026-10-26/baseline-still.png` 用于定位 Logo。此图不能证明动态问题。
- 首次计划命令：`TestResults/live-dock-probe.exe replay 14096 974 1775 974 1615 12 600 150 TestResults/ui3-live-dock-2026-10-26/replay-01-vertical-slow.csv`。
- 退出 2：`start point is not currently owned by the explicit PID's Bar`。前置检查在任何 SendInput 前中止，没有实际拖动，没有生成有效重放结果。
- 原生 WindowFromPoint 网格检查：Logo 区域 x=910..1030、y=1700..1840 均命中 PID 13908 的 `LockScreenBackstopFrame / Backstop Window`。已请求用户解锁桌面，未绕过系统锁屏，尚不能开始真实重放。
- 该环境阻塞不是产品源码失败，也不能算原始闪烁复现。产品源码仍未修改。

### 原始症状已由真实运行和用户目视确认

- 用户已解锁，确认输入为鼠标拖动 Logo；原生 hit 校验重新命中目标 PID 14096。
- 默认执行沙箱内的两次重放在按下前无法确认注入坐标而中止（replay-01/02，无有效拖动），不能算产品异常。对同一已授权、PID 限定的测试使用原生桌面执行后，replay-03 完整通过。
- 有效命令：`live-dock-probe.exe replay 14096 974 1775 974 1615 12 600 150 .../replay-03-vertical-slow-native.csv`。
- 有效运行 result=complete；18.074 秒；8626 个采样；3833 个移动；全程不抓屏。
- 用户明确回复“看到了相同的剧烈位置闪烁”，进一步确认“基本均匀分布；非吸附进入吸附概率高，退出也出现但较少”。这是用户在真实屏幕上的目视确认，不是助手从 HWND 外框推断出来的视觉证据；没有异常画面的截图/录像。
- 同一次按住中实际 HWND 发生 9 次尺寸变化，仅在 cycle 0/1/3；后续 cycle 4..11 保持 1288×440，仍有每次模式切换约 41..45 px 的原生 top 跳变。用户说明后半段仍闪烁，所以“只有 resize 才会闪”不足以解释全部现象；不能因此排除位置/source 交接。
- 实际产品日志 source=0、native 接受数持续增加；raw bound=false、disabled=true。相同症状已在 native 路径出现，Raw 并非复现的必要条件。

### 下一步单变量诊断（不是已确认修复）

- Debug/presentation-harness 构建增加默认关闭 `INKEYS_UI3_DOCK_ULW_ONLY=1`：只跳过 Seek 的 SetWindowPos 直移，让目标位移触发现有 ULW 提交。保留当前动画、viewport、尺寸、source 和 serial 逻辑。
- 在私有副本启用该变量，使用同一真实拖动轨迹，检查症状是否仍出现。若仍出现，则不能把双 API 交接视为已证实根因；该诊断开关不作为最终产品修复。

- ULW-only 首次受限构建退出 1，首错 C1902/PDB manager；仓库已有相同执行环境对照记录。保留原工具链/源码/工程配置，在正常本机构建环境、私有临时目录和单一 Path 下重试完整 Solution，退出 0。仅有未修改 hashlib++ 路径的既有 C4267 警告。
- 私有基线进程通过所属 DrawpadWindow 的 WM_CLOSE 正常请求退出；启动独立 `ulw-only-bin` PID 46652，配置从基线复制，诊断变量只注入该进程。新主栏仍为相同 1128×198、同一初始位置/DPI/显示器。
- 原生顶边阶跃的候选解释应纠正为 directTranslation 的模式切换，不能仅凭 HWND CSV 断言 candidateViewport/source 变化。完整帧中 screenY=monitorY+directTranslationY+zoom×MapY；viewport/capacityOrigin 相消，需要检查内容补偿与实际呈现。

### ULW-only 对照结果与局限

- replay-04：18.078 秒、8564 采样、3834 移动，完整执行；产品日志确认 diagnostic=ulw-only 生效。
- 用户反馈：“基本相同，仍明显闪烁，并且运动十分卡顿”；新增观察“闪烁时脏区红框也位置跳变”。用户要求下一轮前提前通知，以便录屏。
- 同时记录：窗口几何变化从基线2258次降至140次，后半段出现更大的连续位移阶跃。关闭直移后，现有唤醒并不保证纯位移立即提交；此实验引入新的拖动卡顿，不是可交付修复，也不是保持呈现节奏不变的严格对照。
- 结果不支持“只关闭 SetWindowPos 就能修复”。仍不能唯一确定 ULW/DWM 根因。已撤掉12行实验开关，私有实验EXE和日志保留用于追溯，不继续叠加猜测式修复。
- 下一轮恢复原基线、相同轨迹；待用户准备好录屏后再开始。后续诊断应记录同帧位图映射、dirty和实际ULW/直移参数，不用透明HWND外框代替可见形状。

### 录像中的实际像素证据（原基线，用户录屏）

- 用户提供只读源文件 `D:\Personal\Downloads\2026-10-26 14-27-50.mp4`，并指出55–57秒；使用其现有 `D:\Project\Resource\ffmpeg\ffmpeg.exe` 提取，无新增依赖。
- OBS 32.0.1、2880×1920、60fps、H.264；时长65.47秒。用户明确说明录屏使肉眼现象减轻，录像遗漏更多；这段不代表无录屏时的全部严重程度。
- replay-05完整执行18.074秒、8834采样、3832移动；后半程1288×443，16次原生阶跃，范围−46至+41px。
- 从53.8秒起以原帧率提取264张1400×650局部图（桌面crop起点750,1250），保存video-review/frames-54-58。
- root直接查看并确认：frame0161（56.483333秒）正常；frame0162/0163/0164（56.500000/56.516667/56.533333秒）主栏本体下方残留另一截Logo底边及青色选中背景；frame0165（56.550000秒）消失。证据不是仅蓝色HWND外框或红色dirty框移动。
- 原像素并排裁片：`TestResults/ui3-live-dock-2026-10-26/video-review/visible-bottom-residue.png`。保持未注释原帧另存。
- 对frame0162残影选中背景下条带（crop内x335..475,y583..604）与frame0161垂直偏移0..80px逐一比较，最佳匹配为旧像素下移41px，RGB平均绝对差4.3909；零位移差23.2882。这与吸附时HWND约+41px相符，说明旧像素条带与窗口移动存在直接可核查关联，尚不能独凭此区分源位图漏清与系统partial更新留存。
- 代码审阅的新候选：dirty选择性Observe在前，映射原因ForceFullDamage在后；full commit可能清除未Observe对象的历史bounds，下一次LocalDirty只覆盖新bounds。正在构造真实tracker反例并加默认关闭同帧trace，尚不把候选视作已确认实机根因。

### 同帧 API 诊断与用户要求的录屏配对

- 增加默认关闭、仅 Debug presentation harness 生效的 INKEYS_UI3_DOCK_TRACE；固定容量32768条POD，API前后只记录内存数值，手势结束后导出。未改变窗口、dirty、动画或交接行为。完整Solution Debug|ARM64原生构建退出0。
- 第一轮不录屏：PID46656，replay-06-trace-no-recording.csv，8537采样、3832移动。live-trace-01-no-recording.csv共2901条（630 ULW、2271直移），drop=0/inflight=0/complete=1，630 ULW均成功；trace起点相对重放首样本+35.1923ms，终点相对末样本+2.0873ms，持续18.0467911秒，确认对应同一手势。
- 确认触发点不是初始猜测的mapping-full分支：34个ULW具有main_group_observed=0、full_replacement=0、debug_target为空，而businessDirty/presentDirty等于viewport；计算顺序唯一指向ResolveBarDebugDamage之后empty-presentDirty兜底的晚ForceFullDamage（RenderLoop原9617行）。之后仍连续mainObserved=0，说明full pending已由成功Commit消耗，未观察MainGroup旧bounds随之清空。
- cycle1..11每次后续capture都少清24px旧内容；例seq325/frame97旧body sourceBottom=1027，seq442/frame99容量原点仍829、sourceY仍657，presentDirty.bottom=1832只清到sourceY=1003，旧source[1003,1027)映回屏幕[1832,1856)。seq443下一完整覆盖24.238ms后消失。11轮恢复延迟14.470–28.071ms；cycle0没有此前的空dirty晚full且capture完整覆盖，无相同遗漏。
- 这证明实际应用提交的清除矩形漏掉旧body像素，不能把现象直接归咎于Windows/DWM。旧录屏56.500–56.533秒的下方残留与其区域和41px平移机制相符；尚需修复后同轨迹用户目视验证，不宣称所有闪烁已经解释或解决。
- 按用户要求第二轮录屏使用同一EXE hash的PID16856；收到“录屏已开始”后才运行replay-07，完整18.080秒、8063采样、3768移动。但one-shot内部trace此前已被另一43.8397088秒手势占用：起点比replay07早273.8211142秒，终点早248.0615202秒。live-trace-02-user-recording.csv不能和replay07按帧配对；已向用户明确说明这一诊断限制，未隐瞒或当成同步成功。
- 用户两轮反馈：问题一致但效果不同，错误跳变区域在红框/脏区外；不录屏时难辨红框内是否异常，但可确定红框外异常。提供第二录像D:/Personal/Downloads/2026-10-26 15-05-19.mp4，18–20秒明显，其他时段也有。
- 关闭第一轮自有进程通过DrawpadWindow WM_CLOSE。退出触发既有ShutdownSupervisor期限兜底0xE1430016；trace早已完整封口。该退出限制与闪烁调查分别记录，不扩范围修改关闭流程。

### 本轮最小修复与用户指定的停止点

- 用户最新指令：验证完成后进行最小修复，后续验证等下一轮。已通知不再启动拖动、构建或修复后自动回归。
- 唯一产品行为改动：Bar.RenderLoop.cpp空presentDirty且非alphaOnlyDemand兜底删除晚ForceFullDamage，保留businessDirty/presentDirty=candidateViewport；补充中文原因注释。这样该帧仍整窗清绘和ULW，普通成功Commit保留未观察且未变化对象的旧bounds，供下一次移动清旧像素。未修改动画/窗口尺寸/位置/source/串行屏障/重试逻辑。
- 交叉只读审查覆盖初始化、alpha-only、未知业务变化、节点消失与失败重试。初始化/未知变化/失败仍由观察前的既有全脏路径处理；此结论属于源码边界审查，不冒充修复后执行结果。
- 保留默认关闭的同帧诊断（Bar.LiveDockTrace.h、Interaction/RenderLoop接线）用于下轮匹配验证；没有将诊断开关当成修复。
- 已做的修改后检查仅为git diff及空白/编码/CRLF检查；未运行构建、单测或实机验证。现有构建PASS对应修复前诊断基线，不能归给本修复。
- 第二诊断进程PID16856已退出，exit文件=0，原生枚举bar_count=0；无遗留测试窗口。
- 第二录像：OBS 2880x1920、60fps、45.02秒；17–21秒原始帧保存在video-review-02/frames-17-21，18–20秒概览contact-18-20.png。实际观看可见内容跳变及背景文字的异常重复/位移；此录像无同步API trace，不将所有异常归入已定位的旧body漏清。
- 详细数值报告：TestResults/ui3-live-dock-2026-10-26/trace-01-residue-analysis.md，逐cycle数据trace-01-residue-cycles.csv。
- 上边缘上移是独立的anchored映射约束回归，已有确定反例，但本轮未改。Down预扩可以减少扩窗次数，固定尺寸的后11轮仍触发漏清，故它不能替代本修复；也尚未实施预扩。

### 下一轮接力（未经验证不得宣布完成）

1. 先复核当前最小diff，再完成能够在旧代码失败的真实RenderLoop/offscreen回归；新测试不能只复制空dirty分支、也不能用ULW参数合法性代替源像素/旧范围检查。方案见同目录TestResults的offscreen回归报告（若已生成）。
2. 使用现有完整Solution Debug|ARM64原生MSBuild，保留Path规范化与本机PDB环境处理；按变更运行回归。
3. 在用户准备后重跑相同无录屏/录屏轨迹；每轮都核对内部trace与外部replay的QPC范围。one-shot trace只接受进程第一手势，因此应用户准备好后再启动私有诊断进程并紧接重放，或先改诊断明确arm，不能事后把不同手势拼接。
4. 比较capture旧源范围是否被完整清除，并由用户判断原始剧烈位置闪烁是否消失。保留第二影片背景文字跳变是否仍在这一未决项，必要时单独检查DWM/partial更新；不从一个确定bug推断所有现象均已修复。
5. 然后独立修正顶边合同及原先错误的抓点恒等测试，避免同时变更多个因素。顶边方案在TestResults/ui3-live-dock-2026-10-26/top-border-repair-plan.md（若已生成）。
