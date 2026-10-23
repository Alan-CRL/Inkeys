# 正式捕获生命周期扩展独立离线检查（2026-10-22）

本报告只审查扩展的静止采样、20次 Capture Start/Stop、Busy、立即取消、真实Read在途撤销和普通Freeze协调。**本轮没有实际执行这些GUI场景，不能写通过。** 实施者第一次启动已在安全门前退出；checker遵守锁屏停止，仅读源码/日志、运行严格坏argv。上一轮12帧成功及其A80D...产物见 `live-capture-check.md`，不能套用到本轮。

## 范围与结论

当前任务 `.trellis/tasks/10-21-scroll-follow-persistence-regression`，基线HEAD `10abcac3`。读取更新PRD/design/implement/check.jsonl、真实捕获规范、前轮检查、live授权、实际产品整链设计及原Session/Freeze/coordinator实现，按真实调用关系核查。`get_context --mode packages`无packages；本次影响native-desktop的Win32/线程/资源/测试入口，index与跨层准则适用，没有shader、PPT、算法或持久化格式改动。

静态结论：扩展确实复用同一原Mag pair和原producer、真实Session撤销/排空及原Freeze状态观察者与事务，没有用假completion替代。发现并修正一处在途取消验收竞态后，当前源码可以进入下一次串行完整ARM64 build和获准的真实测试；**最新修正尚未编译或实屏验证**，父/本任务不标完成。

本轮没有source写入、build、GUI或Computer Use。唯一源码修复由主会话串行交原实现者完成，再由checker重读验证；没有回退他人变更，没有commit/push/finish/archive。

## Findings（fixed，由实现者按checker证据修正）

文件：`Inkeys/IdtMagnification.cpp`，Read-stop分支。

原扩展先观察一次 `inFlight && attempts>0 && accepted==0` 再调用Stop。两者之间Read可能已经提交，Stop后accepted1、最后accepted1、callback1仍能通过原断言，最终打印 `read_stop_checked=1`。这只能说明“观察到在途后发Stop”，不能证明真实Read被撤销，属于**测试证据缺口**，不是生产取消逻辑错误。

主会话同意后由实现者做局部修复，最终Mag.cpp SHA256为 `512a650ea9007c9cc5080b51cd0d4a9af4f02c04e05c6d60b13d29758f147dd0`，checker已重新逐行核对：

- 每次先确认旧session排空，使用本次callback计数baseline；新session仍检查递增、原pair/owner一致和Busy。
- Stop后accepted>0只打印 `race-missed`，不计实际撤销，最多3次独立session重试；每次先通过原verifyIdle真实排空。
- 只有Stop时accepted0、drain后accepted0、该session callback增量0、lateResults>=1且撤销reason为Requested时，才记 `actual_read_revoke=1`。
- 3次都没命中则抛 `read-stop-uncovered-after-race-retries`，最终失败/未覆盖，不打印矩阵通过。
- Session每次Start重置statistics；`lateResults`是在Read真正返回而发布/窗口访问门已撤销时才增加，不是伪造收据。该条件与accepted0/callback0/reason联合，可以区分Read已先提交的正常竞态。
- 不新增hook、owner RPC、同步阻塞或生产接口修改。源码格式与diff检查通过。

## 真实路径静态审查

### 同pair、Busy、20轮与warm资源基线

每次startSession调用正式 `FreezeCapture::Start`，要求session严格递增、WindowService的host/child不变及ownerTID不变；随后再次Start必须Busy且返回同session。20轮各次绘制phase、等待本session至少2个匹配63点的正式图像，再Stop和排空；不是人工给Session设置accepted或复制别处图像。

资源计数在每轮verifyIdle之后取得。round1已完成一次真实Start/Stop后才作为warmbaseline，round2–20与该GDI/USER数精确比较。它能验证热身后这些轮次的计数稳定，**不能排除初始分配、缓存、单次泄漏或长期趋势**；原始进程前后delta也不能称零泄漏。GetGuiResources和数值趋势尚未真实运行。

### Stop、回调与清理屏障

正式Session::Stop只关闭publishing并唤醒，不阻塞UI；生产Read仍持有其DIB。Pump在结果提交前复查发布门，已准入callback可自然返回；它返回后才撤inFlight。Drain只在非publishing且无inFlight时运行Backend::End，先Conceal/释放DIB，再清occupied和callback。

verifyIdle先轮询真实occupied=false，再记录receive样本数，继续泵150ms确认未增；核对同session、publishing/inFlight=false、ready=true、host/child都隐藏。不是只发Stop或sleep后直接宣称完成。针对cancelled callback的分支另外核对atomic计数；新版Read-stop按session baseline隔离重试。150ms是有限观察窗，不代表无限期安全证明，实际生产排空条件才是寿命屏障。

立即取消实际顺序是成功Start、核对Busy，再立刻Stop，随后排空/150ms观察并要求cancelledCallbacks0。这证明首次800ms准备期间的该次取消，不能把它叫作没有任何中间操作的原子Start/Stop。

最终正常/异常清理仍为Capture::Stop→停止原coordinator→原worker join→WindowService owner StopAndJoin→销毁本次source/class→Display shutdown。借用回调/窗口context到join后才释放。90秒专用进程watchdog只作为无法返回PW时的失败兜底，不强杀工作线程，不提前释放DIB。扩展无UI线程join/PW普通产品路径改变。

### Freeze真正revoke/drain/reveal

两次freezeCycle（重试前一次，以及正在捕获后一次）直接调用真实 `UI::Freeze::Toggle`。Toggle同步发布版本，已注册的PublishFreezeState先调用Session::SetFreezeState，立即撤销publishing并标FreezeActive，再发给coordinator。

原MagnifierThread是唯一串行消费者：先Stop(FreezeActive)和Drain，再执行原UpdateMagWindow事务（PrepareHidden、角色Filter、Source、Invalidate/Redraw、Reveal），特定revision真正CommitApplied后coordinator.visible才为true。扩展等这一revision且occupied/inFlight均false，检查实际host/child visible和alpha255，再要求Freeze期间正式Start返回FreezeActive。额外150ms验证receive样本数不增。

关闭Freeze也用Toggle，等coordinator处理对应新revision、visible=false且两个真实窗口隐藏。之后再新Start，等3个正确phase帧后停止。activeCapture路径核对被撤销的session和FreezeActive reason，避免只因Freeze窗口存在就算互斥。

以上是**真实原Mag协调/状态/窗口属性路径**，没有实际执行前不能叫通过。即使将来通过，也不等于验证Freeze屏幕内容保持、UI按钮交互，或Business/StateMonitoring/Draw3的系统成对ACK。

### 静止采样与性能含义

四phase后保持phase4至少5秒且新增至少20个匹配帧，再Stop/drain。仅使用该session且timestamp>=staticBegan的样本，逐个要求phase4，基于正式Backend::Read耗时和相邻Read完成间隔统计；`(n-1)/(last-first)`给出实际有限采样节奏，打印target_fps20单独作目标。

没有驻留probe或人为等待混入Read；首次800ms和phase切换不进该static窗口。但回调每帧63点GT、第一次crop、GetLayeredWindowAttributes、日志及夹具消息泵仍有开销，这只是Debug专用夹具读回节奏，不能当最终追踪/浏览器性能或硬件帧率。

Read范围保持Session对Backend::Read调用前后，含几何检查、清DIB、PW(2)+GdiFlush、alpha整理，**不是纯PW时间**。新分布p50/p95使用排序索引 `floor((n-1)*p)`（lower empirical sample），与前报告nearest-rank口径不同，后续汇报应明确口径或从原始帧统一重算，不能混用。当前没有真实新static性能数值。

### 删除probe与严格入口

原新增LiveReadProbe、raw read hook、私有owner message0x4B5、owner PW/Flush/Source/Redraw路径已全部移除，正式Backend::Read不再包含该observer。旧 `--freeze-mag-printwindow-test` 原诊断保留，本轮未运行。

新RunFreezeCaptureLiveTest恢复无bool参数，声明/定义/harness-off stub与Main一致。退役 `--freeze-capture-live-probe` 的全argv前缀显式返回2，防旧命令落普通GUI；剩余live-test与既有内部flag仍全argv严格扫描。Release/harness-off常规返回2。无需新配置/工程/资源登记。

## Findings（not fixed / 未执行）

- 最新在途取消断言修正尚未完整ARM64编译，待主会话与后续整链接入串行合并后一次build。本报告不把旧2D5A...产物当作512A...源码已编译。
- 20轮、warm资源、真正Read撤销、Busy、立即取消、两次Freeze和5秒static均未运行；安全门没有通过，没有有效新PNG、首帧、phase或性能分布。
- 这次InvalidArgument只测试空callback，**不是Mag初始化失败测试**；设备/窗口初始化失败、安全层消失重现、旧系统/WOW64和长期资源仍需另列。
- 没有执行真实Business/StateMonitoring双窗ACK、可见墨迹跟随/退出、浏览器/Word/PDF、触控/笔、x64/PE或harness-off构建。前轮有效12帧只保留为其原hash的独立证据。
- 除已修测试竞态外，未发现需要修改生产Mag/Session逻辑的问题；不为缺少实测而调整线程、等待参数、SDK或依赖。

## Verification（实际结果）

### 实施者构建和安全停止

扩展初版完整 `InkeysRepo.sln Debug|ARM64`，实施者复用vswhere定位ARM64原生MSBuild、同PS Path规范/node reuse禁用的原wrapper，**exit0/23.820s**。日志 `Build/ScrollFollow/motion-tracker-build-live-lifecycle-first.log`、同前缀-console/-stderr。checker查阅成功产物和日志；本轮checker没有再build。

初次正常live运行由实施者执行：PID53668，**exit4/1.204s**，stdout0、stderr70B：`blocked=security-interface-before-initialization`，见 `Build/ScrollFollow/live-lifecycle-first-test.json/-stderr.log`。安全检查先于Display、fixture窗口、正式Capture；不是捕获失败或生命周期验收失败。checker没有重试安全门或任何GUI，等待用户手动解锁。

该build与被拒运行EXE SHA256：`2d5a1e20a7dab146d583dd14fd78ad79776f7f138956345575bcc3207cc5ac3b`。此产物在在途断言修复前，不能证明最新源码编译通过。

### checker严格argv与static

- `Build/ScrollFollow/run-live-bad-argv.ps1`：10条全部exit2/stdout0/stderr0，单次98–171ms。结果 `Build/ScrollFollow/live-bad-argv-211468156/result.json`。
- 额外精确退役 `--freeze-capture-live-probe`：exit2/stdout0/stderr0，结果 `Build/ScrollFollow/lifecycle-retired-probe-argv-211469359/result.json`，同2D5A...EXE。共11条无GUI路径；没有运行正常live flag。
- 最后 `git diff --check --ignore-submodules=all -- Inkeys/IdtMagnification.cpp Inkeys/IdtMagnification.h Inkeys/IdtMain.cpp` exit0。三文件UTF-8 BOM/CRLF、bareLF0；Main仍无EOF newline，Mag cpp/h仍有EOF newline。
- lint/static：通过上述范围。C++ type/build：扩展初版通过，最新局部断言修正**待完整build**；GUI lifecycle tests：**未执行，安全环境阻塞**。

| 最新静态源码 | SHA256 |
|---|---|
| Inkeys/IdtMagnification.cpp | 512a650ea9007c9cc5080b51cd0d4a9af4f02c04e05c6d60b13d29758f147dd0 |
| Inkeys/IdtMagnification.h | 400517f5d7274be9e54639f75abdd42f411f4ee82fc524207ab4035a1db82338 |
| Inkeys/IdtMain.cpp | ba4a26c66487843be77533900b45aec9159272be55042aa9d219705d369877a2 |

checker报告完毕后仍不持有源码/build/GUI；主会话安排下一串行阶段。若用户手动解锁后重跑，需先通过完整专用屏幕GT再评判捕获；再次发现安全层立即停，不绕过，不把当前任务/功能标完成。
