# 2026-10-22 实际画面测试授权

用户最新指示：“你现在被允许进行测试，可以创建窗口和操作电脑和截屏（但是不使用 computer use）”。此前完全不可见窗口的限制已被本轮授权覆盖；历史报告中的禁显/停止点仅描述当时权限，不再阻止当前必要测试。

- 基线feature/move / 10abcac3。任务继续in_progress，不finish/archive；本轮没有新的commit/push授权。
- 允许通过项目测试入口、专用Win32窗口、原生消息与CLI驱动测试和截图；不使用Computer Use工具/skill/cua。先仅操作新建的专用测试内容及私有数据，不操作用户个人窗口、配置、文档或系统显示/安全设置。
- 所有测试窗口自动关闭，一次一套产品/Mag实例；保存图像尽量限专用内容区域。锁屏或交互桌面不可用时记录真实环境阻塞，不解锁或更改安全设置。
- 第一门槛：正式FreezeCapture Start/Stop、原MagnifierThread真实Read；无callback首帧、连续变化/静止、原角色排除、允许旧帧但持续前进、20次启停、Freeze互斥、取消和资源/时间分布。旧owner诊断不可替代新正式worker。
- 第二门槛：真实Business/IdtState/WindowService成对ACK和Draw3可见墨迹/底层滚动整链，不能以原hidden ACK seam或手动绝对目标发布冒充。先专用内容，再在可用且可隔离的应用中测试。
- 捕获若失败先定位原Mag方案；不另建Mag、不改变用户指定原初始化/窗口创建/readyPromise线程设计，不换WGC或捕获排除属性。代码修复需根据证据最小化，并走implement/check。
- 主会话维护规范/任务/验收矩阵；实现者串行持有build/test/专用窗口。独立check后记录实际成功、失败及硬件/旧系统/缺应用未测项，不把授权当作测试通过。


## 用户已手动解锁，恢复测试

用户通过异步提问明确回复“已手动解锁，继续测试”。此前安全停止解除，仍禁止Computer Use；解锁前结果作为独立历史保留，不将17NearBlack归因于PW/Mag，也不追溯认定全由锁屏导致。独立checker统一接管source/build/test，先安全静态核对，再无probe正常正式路径，以完整source/overlay屏幕GT为前提；需要时再分步对照。若安全层重现立即停止，不尝试绕过。


## 用户再次手动解锁，立即执行已构建矩阵（2026-10-22）

用户对第二次安全停止明确回复“已解锁，继续测试”。恢复同轮专用GUI/原生操作/截图授权，仍禁止Computer Use；不尝试阻止再次锁屏。完整ARM64 `product-live-include-order` exit0/24.609s已包含read-stop严格修正和最小product夹具，源码/build冻结；先串行运行正常capture20轮/Freeze矩阵，独立checker只读审查后续产品夹具。实际结果另记，不由本授权预先标通过。


## 第三次手动解锁，运行已准备的真实墨迹（2026-10-22）

用户最新明确回复“已解锁，继续测试”。此前D6二进制的PID79380在创建窗口前安全退出4；未产生A输入，不代表样式修复运行失败。离线完整无窗口及ARM64/x64构建完成后，已将GUI独占交给implement，直接运行冻结D6的短墨迹入口，不重建、不防锁、不使用Computer Use。后续实际结果仍以原始日志与独立check为准。


PID59040同一D6直接重试在初始化前退出4/174ms。随后仅一次同权限原生只读命中元数据：LockScreenBackstopFrame、explorer.exe、visible1/iconic0/cloaked0，全主屏逻辑rect(0,0,1440,960)，各desktop名Default。此查询为DPI-unaware逻辑坐标，不宣称与DPI-aware harness的物理采样点逐像素相同；不靠Default或未cloak判定已解锁。没有截图、输入、枚举或操作安全窗，未再次运行GUI；已请求用户确认当前是否正常桌面。


## 用户确认正常桌面后续跑（2026-10-22）

用户最新同时回复“已解锁，继续测试”和“现在已显示正常桌面，继续测试”。主会话立即再次交implement独占GUI/test，不重建，直接运行冻结D6的真实短墨迹入口；此前安全阻断保留为历史，不由最新解锁推断通过。原生输入/截图及私有窗口授权沿用，仍不使用Computer Use、改变锁屏设置或操作用户内容。checker只读审随后原始证据，不并发构建/GUI。


## 2026-10-23 用户要求继续

用户明确“继续”，延续当前任务及此前专用可见native窗口/输入/截图授权，仍禁止Computer Use、不更改锁屏设置、不commit/push/finish/archive。恢复前确认没有Inkeys或MSBuild进程；KnownInkEdge夹具分类修正只完成源编辑和离线模型验证，native Down量化oracle尚未实现/编译，不能将旧F568二进制当作这两项修正版。先完成夹具校正并顺序implement/check，再一次原安全门下短ink，之后才进入长滚动。
