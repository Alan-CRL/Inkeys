# 有墨迹真实OS夹具：独立离线预审（2026-10-22）

当前任务 `.trellis/tasks/10-21-scroll-follow-persistence-regression`。checker只读新增ink diff、实际现有保存/codec路径与build/CLI日志；**没有source修改、build、CLI/GUI、SendInput或截图**，不派代理，不commit/push。core串行拥有source/build及之后获准GUI。本报告是运行前审查，不预判实际RTS/系统鼠标能生成墨迹。

## 当前放行结论与版本

最终inl SHA256 **c0cbb4ebe3a751091c9441afa6f8f9231175d4fe290cb41c4dd19c1dc80a28b4**，对应core最后两项输入保护：A/B每笔鲜活ScreenSourceGt；Down前再次核physical left。此前69d112...为旧版，不是本报告最终版本。

core报告完整ARM64 product-live-ink-preinput exit0/14.336s，checker已读原MSBuild日志Build succeeded/0 Error，内部计时14.03s，不能混用两种计时。EXE交接SHA256 **b6ebe54edfc32465539ab8aea4daa1752475b0dd548e5bdc2bdfb9fed05a0154**。没有发现阻止这一有限专用OS试验的安全或伪造证据缺口；先给主会话快速结论，完整报告后补。本次实际GUI及其hash关联、图像、文件仍须另一运行记录确认。

已读取当前task/规范和原Product安全check/runtime check，重点依照本轮真实输入/可见墨迹/文件验收边界。无新public产品接口、生产捕获/Tracker/渲染参数、文件格式或依赖修改。

## 新入口与实际生产路径

- whole-argv表第7项 `--scroll-follow-live-ink-test`，只接受精确单参数；harness-off stub返回2。后缀、错位、重复/mixed都早退，普通Main配置/单实例/GUI不落入。原退役probe仍严格拒绝。
- 统一RunCase(false/true)保留空页case；true才启用有限runtimeMetrics128与原RTS trace。这些是观察指标，不提供stroke目标或contact。
- **没有启用或调用HiddenTestContact**，没有Business TestPorts、手工Follow Target/ACK/Handoff，没有改RTS选择或强制鼠标后端。
- A/B只通过tagged OS SendInput。完成条件读取真实input down/terminal/recycled各增加1、runtimeMetrics contactSeen增加1、CompletedStrokeKind Drawing、canUndo和新成功present。若系统注入没有被实际producer消费，5秒内失败，**没有合成墨迹fallback**。RTS ready/input_mode也必须按实际日志描述，不能把鼠标注入叫真实硬件笔。

## 输入安全与寿命

A/B各次先通过未屏蔽的完整source屏幕GT，再确认Primary输出ready和没有自己/用户held。OS移动前核destination精确命中当前进程Drawpad，移动后再核actual cursor命中、取消/失败/安全层；Down紧前再次physical Left检查，防等待期间接管用户按住的键。

Down成功后立即置ownLeftHeld，16段移动每段持续泵消息并验证目标；正常Up前还核实际hit。中途任何取消/安全/目标变化抛出，外层仍按原凭据只归还自己成功插入的Down；独立Up有界最多3次，归还未证不能PASS。没有用批量SendInput计数推定Down/Up全部成功，没有点击外部窗口或以ALT/AttachThreadInput取得权限。

滚轮路径也先ScreenSourceGt、actual source hit/foreground、取消/安全门，再发一次OS wheel；source收到tagged消息并重绘才继续。没有直接SendMessage wheel冒充穿透。

global input发送前条件仍不能提供与外部抢焦点的原子绑定保证；仅限本轮受控专用窗口。检查失败没有向其它窗口重发Down/wheel的补救。

## 像素判定不会用灰底当墨迹

专用世界纹理为灰度，各通道相等。A采用红色、B蓝色：core不只比较亮度，要求主通道>180并比其它通道大60–80以上，**灰度背景无法满足**。每次墨迹图至少256个core，空图不会通过。

A/B ROI在屏幕中部不同纵带；CheckInkRoi确保不与精确source ROI `(64,64)-(320,256)`或99个source采样点重合，没有为了有墨迹而删除/跳过这些GT。每张InkScreen仍先做全部source GT，再对整个墨迹ROI逐像素确认：已知世界背景±3或本笔主色走廊；unknown>0不保存图。主色走廊限制在给定线Y±24、ROI左右至少16px内，避免任意未知区域被保存。该分类是专用灰底/红蓝墨迹判定，不是对任意桌面内容的通用识别证明。

A在真实主窗画好后取baseline。Tracking后实际content=true/pair(0,1)，先比较辅助呈现与baseline零平移；OS前进32px、反向16px分别比较同ROI中A的core位移-32/-16；Off恢复Primary后再比-16。CompareInk对base→now与now→base双向每个core做±2邻域最近匹配，要求最大欧式误差<=2px、core数量保持90%–110%。仅正确viewport数字或单边掩码重叠不能让此断言通过。

它没有逐帧拍摄连续动画，不证明两次采样之间从无闪烁；也没有验证任意形状/荧光笔/擦除。ROI最小尺寸不满足时安全失败，不能推成所有屏幕几何均支持。

## 非空真实ACK和最终位移

A是真实Completed Drawing并且page有内容之后才Toggle，沿原Business F0/Tracker/StateMonitoring/WindowService，明确要求主窗隐藏/辅助窗可见，即pair(0,1)，selection输出ready与真实ACK。没有拿空页pair(0,0)结果替代。

前进wheel=-240使source世界Y+32，反向wheel=+120使其回到总+16；每步等待两个不同生产seq的total和viewport在GT±2以内、已经Applied、clampfalse且Capture至少新增3帧。Off仍沿真实final barrier和reverse ACK，要求Complete、handoff、final.seq Applied、原工具/content版本不变、最终source(0,+16)/viewport(baseX,baseY+16)±2、两次tagged wheel且无外来wheel。

墨迹A/B的工具颜色改变是原公开工具setter；B在Off后才画，文件断言分别核实际Down的client坐标和新Viewport只叠加一次。此时没有扩大双指导航或公开人工校准功能。

## 保存路径与真实身份

B真实完成及蓝像素确认后调用**原StopProduct**，它完成原Exit保存屏障和worker drain；没有另造snapshot或直接写UInk。产品已停但source/Display/WindowService和f仍存活，之后才读本次私有AutoSave。

ReadInkExit沿原ReadLastCommittedDesktopAutoSaveFixture：该函数先持真实非reparse目录/文件leases及date mutex，验证索引和单调唯一dailySequence，再读取末项实际文件；ReadCommittedDesktopPath要求ReadComplete、无recovered/invalid complete block、Import存在、fileGuid匹配，提供真实sourceRevision和loadedSnapshot。

新case另要求：

- 实际Loaded receipt，trigger Exit，sourceRevision非空，session/daily sequence都1、storageSession非空。
- 索引只1项；私有fresh root且全例没有Clear/逐帧保存，只有这次真实Exit。跨午夜时另读当前日期，有效记录必须仍符合这些唯一条件。
- snapshot.fileGuid等于索引fileGuid，Desktop workspaceType0、workspaceGUID等于真实Follow scene、唯一page GUID也相同。
- 正好两条Pen且各至少3点，scale1，文件Viewport等于final。
- A首点等于其原client Down（新画布初始viewport0）；B首点等于实际client Down+文件Viewport，误差0.001。
- 再对该真实路径ReadUInkFile和Import，要求Complete/Success并fileGuid相同。codec的DecodeHeader明确拒绝 `version != kUInkVersion`，而当前kUInkVersion=10，所以ReadComplete确实约束文件为v10，**不是只检查编译常量而漏了文件版本**。

该检查不造Committed completion，也不通过新冷载API安装文档。它验证此案例的真实Exit文件和GUID/viewport/Down锁存；没有保存跟随前A完整数组作逐点基线，**不能宣称本例A所有点/style逐值前后不变或历史Undo/Redo矩阵已测**。原隐藏四场景有这些独立证据，应分别引用。新case也没有跨进程任意冷载或Clear磁盘恢复。

## 清理与已有保护

正常case取完文件后productStarted=false，外层不重复StopProduct；失败仍调用原Shutdown单调升级，state join、Host停止（若尚在运行）、Mag stop/join、WindowService stop/join，之后才释放source/DIB/Display/COM/logger。startupContext和style callback持有者保持到相关owners退出。

所有之前的取消再检查、security即时BeginShutdown、自己的LeftUp归还、原90秒专用进程失败watchdog继续保留。没有把watchdog退出算正常清理；最终result0仍要求state/mag实际返回、失败标志0及所有running/ready/occupied/held/logFailed清零。

## 实际离线验证

checker不运行任何新过程，只读core产物/结果：

- 完整 `InkeysRepo.sln Debug|ARM64`，product-live-ink-first exit0/23.831s；最终product-live-ink-preinput exit0/14.336s（外部core记录）。最终MSBuild日志Build succeeded/0 Error、3条原hashlib++警告；没改工具链/依赖绕环境。
- `Build/ScrollFollow/product-live-stage/offline-214745421/result.json`：16坏argv全exit2/stdout0/stderr0，109–1274ms；protocol exit0/267ms，state exit0/118ms。runner显式拒绝两个精确live flag，不夹带GUI。
- scoped diff --check exit0。新inl UTF-8无BOM/CRLF、有EOF newline；Main和Mag.h保持原BOM/CRLF，Main无EOF newline。只读检查没有格式化别人变更。

Static/lint范围pass；完整C++ build和最小纯CLI有上述实际记录；**ink GUI尚未由本报告验证**。前一空页PASS的840419...不得冒称当前B6EB...已跑ink。

## Findings

本次checker没有修源。core自行补齐的鲜活GT和Down前Left复查已逐行复核，最终C0CBB...与之前预审69D...区分。未发现阻止有限专用真实OS试验的安全或假通过问题，已在GUI之前向主会话放行。

待实际运行：真实OS能否被当前RTS/兼容producer消费、A/B像素、非空辅助窗、32/-16跟随、Off、真实Exit文件及清理。若任一观察不到，就报告该门槛失败/未覆盖，不开启HiddenTestContact或强迫鼠标fallback来冒充通过。任务保持in_progress；有墨迹case成功也不能标全功能完成。


## 首次实际ink运行后的只读字段复核

core随后运行PID29296，原首行root为 `Build/ScrollFollow/product-live-29296-214909218`。外层 `live-product-ink-first-unlocked-test.json` 保存了file-before-StartProcess的B6EB...hash、exit1/6521ms；实际失败在 `real-os-ink-a / ink-a-real-tool-ready`，没有进入DrawOsInk/OS A Down。cleanup returned/failed/running/ready/occupied/held/log均正常。**不能据此说RTS或OS画笔没有被消费，因为尚未发送。**

按主会话分工，checker不重复core的setter/config调查，只核preDown predicate中字段的写入语义：

| 字段 | 实际写入语义 | 是否要求已有stroke |
|---|---|---|
| firstFrameReady | Host启动的空首帧实际present成功后置true，Stop重设false | 否 |
| lastPresentSucceeded | Host Reset置false，ObservePresented按最近真实present成功/失败写；空闲不主动清false | 否 |
| requestedOutputTarget/revision | 真实ObservePresented按该次输出目标/revision发布 | 否，空主窗已有revision2 |
| readyOutputTarget/revision | 真实成功full present后更新；不因没有contact拒绝 | 否 |
| presentedContentRevision | 成功present后更新；空页contentRevision0/presented0合法 | 否 |
| ScrollFollowPresentationAcknowledged | 核projection/dirty、mode revision和bridge revision均对应真实系统窗口提交 | 否，但工具/模式变化可使旧ACK失效 |

原DrawpadPresentationReady也使用first-frame/输出目标/full-ready/content revision这组条件，另查scene/workspace，未要求stroke存在。新DrawOsInk的contact/input/CompletedDrawing增量条件是在Up之后，未放在preDown，不构成输入先决循环。

本次pen-ready facts为output0/2、ready0/2、content0/presented0、ACK1；失败时同样输出/内容数字仍在，**ACK变0**。所以应检查工具setter后的mode/bridge/projection关联，不能先认定需要画一笔才能ready或改成忽略真实ACK。现facts没有打印lastPresentSucceeded/firstFrameReady，checker不把日志缺失当作已逐项证明PrimaryReady为真；可由core加只读字段和setter分别返回值定位，不放宽条件求过。

本节没有修改source、build、GUI或重新调用setter。当前有墨迹路径仍未验收，前面的放行只是安全/代码预审，不预先保证实际运行通过。
