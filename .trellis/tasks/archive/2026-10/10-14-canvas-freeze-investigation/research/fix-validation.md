# F1–F4 修复验证记录

## 当前结论

用户“批准，并开始修复”后，四项已实施并经独立代码检查。最终主 Solution 与独立存储测试目标均构建退出 0；生产 CLI、原有存储测试和 `--no-window` 通过。用户解锁桌面后，最终同一 EXE 的 F1 背压与 F4 捕获强触发完整产品鼠标 GUI 验收均通过，含实际工具操作与自然退出。默认 HWND 整套复测仍退出 1、11 项失败；原 HEAD A/B 仅证明其中预置顶 owner/Z序条件已有，不能替全部失败归因。独立调用真实 RunSettingCaptureTests 全部通过，整套仍不能记为通过。

本记录只描述修复后二进制，不把此前 HEAD 的调查/组件探针结果当作修复验收，也不宣称原始用户现场已共同归因。

## 实现及检查

- F1：独立固定队列输出器、try-only 生产者、自有句柄和单 worker；stdout/stderr/mirror 迁移；1000ms 同一期限停止与隔离。新增严格早退自检，harness-off 固定 fixture 不参与编译。主程序、Headless、ModelerTests 三个直接消费者均登记同一实现。
- F2：真实 ReadUInkFile IoError 在严格检查前保留分类，使用原四次预算；未放宽文件/目标/会话保护。
- F3：一次 Move 唤醒订阅、arm 后实际 mailbox sequence 复查、帧初 generation 等待、作用域 disarm；有 deadline 分支同样覆盖。
- F4：Setting owner 隐藏仅释放自己的 capture；逻辑和迟到 native Hide 均锁存 session 失效，原渲染 owner 销毁/新建，旧 CommitEpoch 不清失效。

独立检查位于四子任务的 `research/check-review.md`。F1 检查发现的命名空间、格式化前门、隔离后启动快拒绝、harness 编译门和直接消费者登记问题均已修正，并进入最终构建。

## 构建与二进制

均动态定位 ARM64 原生 MSBuild，复用既有 solution/configuration，遵守同 invocation PATH 规范化及禁用 node reuse。未更改 SDK、toolset、依赖版本或产品配置。

| 构建 | 最终日志（Build/CanvasFreezeInvestigation/fixes/） | 退出 |
| --- | --- | --- |
| InkeysRepo.sln / Debug / ARM64 | solution-fixes-verified.log 与 .json/.exitcode | 0 |
| inkStrokeModelerTest.sln / Debug / ARM64，仅既有 inkStrokeModelerTestTests 目标 | storage-tests-fixes.log 与 .json/.exitcode | 0 |

本工作树 provider 缺 Abseil 的历史环境问题仍以本机另一工作树的完整同版 provider 读取解决；10 个 overrides 的版本/port-version 已核对，见 `fixes/build-environment.json`，未升级依赖。

本轮初次构建先出现受限 runner 的 CL code 4，随后普通权限构建 C1041。独占打开原 PDB 成功；最小一行编译对照为 `/Zi` 新 PDB 成功、旧 PDB 副本成功、加 `/FS` 失败、仅为 invocation 指定独立 PDB 服务端点后 `/FS` 成功。采用后者继续原构建，未删除原 PDB、未停止其他工作树的 PDB 服务。清理仅限父 PID/创建时间/命令行均确认属于首轮失败的 20 个残留 MSBuild 子节点。

这里将问题定位到共享 PDB 服务路径，不推定已查明其内部故障。[Microsoft /FS 文档](https://learn.microsoft.com/en-us/cpp/build/reference/fs-force-synchronous-pdb-writes?view=msvc-170) 说明该选项经 MSPDBSRV 串行写入；独立端点参考 [Chromium GYP 原始实现](https://codereview.chromium.org/83803003/diff/220001/pylib/gyp/win_tool.py)，其在本机新/旧 PDB 对照中的实际结果已单独验证。

独立测试构建末尾还有 `pwsh.exe not recognized` 的本机后处理提示，未将其隐藏：MSBuild 退出 0、测试 EXE 已链接且实际执行退出 0。没有为此更改工程或 PATH 全局配置。已有第三方/历史转换、对齐等 warnings 未越界清理。

最终 SHA-256（完整身份清单见 `fixes/binary-identities.json`）：

- Inkeys.exe：`4188e2eb39cca05f40ac68e1730cc85f343c1bfe122c1ee78c920a2dca075c7f`。
- InkeysHeadlessTests.exe：`1f22082e84abf00820f6a973b76f7be528118677158a7d209cab96a4c5dcb955`。
- inkStrokeModelerTestTests.exe：`7c8fc1e089d0019307ece81c30b9b405580040314a100f2fcf566275e5dc9887`。

## 已运行回归

原始 stdout/stderr、命令、实际退出码、耗时与 hash 位于 `Build/CanvasFreezeInvestigation/fixes/regressions/`。

| 命令 | 结果 | 证明范围 |
| --- | --- | --- |
| Inkeys.exe --draw3-diagnostic-output-test | 0，PASS failures=0 | 真实生产队列、writer、满 pipe、恢复、断管、部分写、并发生产/Stop、取消提交间隙、1000ms 隔离；实际 helper 在普通文件核对 |
| 同一参数加 unexpected | 2，符合预期 | 严格 argv 早退，没有进入普通启动 |
| --draw3-ppt-current-load-retry-test | 0，两个 PASS | 真实 worker/不共享文件失败、completion、同目标有限重试、严格 materialize/install 原墨迹及拒绝矩阵 |
| --draw3-pending-topology-load-test | 0 | 原 pending/topology 生产回归 |
| --draw3-control-fence-test | 0，含 EraserIdleWake PASS | 4 设备 × 4 交接窗口 × 有/无 deadline，另有等待生命周期/既有控制边界 |
| InkeysHeadlessTests.exe --no-window | 0，PASS animation correctness | ContactInput、SpeedEraser、SessionState 及其他现有无窗口回归，75.1s |
| 独立入口链接既有 window_tests.obj，直接调用 RunSettingCaptureTests | 0，PASS setting capture ownership | 真实生产 WindowService/owner；owned/unowned、重复隐藏、其他窗口捕获、HideAll 和 shutdown 回退 |
| inkStrokeModelerTestTests.exe --presentation-autosave-only | 0 | 原真实持久化、foreign track 隔离与旧文件保护矩阵，1.5s |

F1 两次背压摘要：stdout `accepted=1025, full=6016, contention=1152, cancel=1, stop_ms=110`；stderr `accepted=1025, full=7168, contention=0, cancel=1, stop_ms=94`。它们是正常 pipe 取消的真实 Stop；自检中有意制造不可取消等待的 `IsolatedPending` 是另一用例，不能代替该结果。自检在普通启动前早退，也没有借主程序 15 秒监督或父进程强杀取得成功。

F3 的设备枚举为逻辑输入，32 组直接调用 Run 共用 idle helper，没有隐藏 Host 额外 wake；有 deadline 用例传人工 2000ms。第四窗口按真实 WaitForWake 调用计数及短时间未返回确定，未做 OS 内核等待采样。这些不等于完整 Run/Host/GPU、真实面积/重连 deadline 计算或触摸/笔硬件验收。

## 默认完整窗口套件仍未通过

首次默认 `InkeysHeadlessTests.exe` 在锁屏时运行，79.5s、退出 1、`FAILED count=8`，保留原始日志。

- 旧窗口用例 5 项：Setting attach owner、unchanged correct owner、joins drawpad owner chain、visible selection setting above canvases、owned reshow。
- 新捕获用例 3 项失败在 owner 前置条件：set setting owner、owned setting sees other capture in attached input queue、attach before show shutdown race。
- Hide、重复 Hide、其他窗口捕获保护、HideAll 和 shutdown 后隐藏断言没有 failure，但前置条件失败仍使整套不通过，不能覆盖为 capture 整套通过。
- 当时 WTS SessionFlags=0/locked、foreground 为 LockApp；fresh-owner 探针仍能成功，因此未直接归咎锁屏。

用户确认解锁后，同一最终 Headless 二进制重跑一次，59.579s、退出 1、`FAILED count=11`，未超时，见 `fixes/regressions/unlocked/results-window.json` 及双流日志。原 8 项全部保留；增加的 3 项为原有 `window_tests.cpp:648–659` continuous owner tree 初始低于 competitor、root 获得 WS_EX_TOPMOST、refresh 后整链高于 competitor 的断言，均发生在首次 Setting attach 之前，不在本次 diff 中。没有新增捕获释放、重复 Hide、保护其他 HWND 捕获、HideAll 或 shutdown 清理断言失败。

补充 A/B 证明预置顶匹配失败在原 HEAD 也存在，并未逐项查明全部 11 项失败的根因；不能称作“解锁后全绿”或“锁屏导致”。本轮未修改 Z序策略或放宽测试断言，完整套件保留失败。

## 解锁后的完整产品 GUI（已授权原生输入）

只操控本轮隔离副本，使用 Win32/Python 原生输入与真实窗口快照，不使用 Computer Use。三次运行均为最终 `Inkeys.exe`，SHA-256 与上述 4188e2eb39cc… 一致；GUI 阶段未改产品源码或重建。

### F1：真实 pipe 停读、满队列、恢复和退出

运行目录 `fixes/run-f1` 的 Cursor/Draw3/TouchArea 开启，stdout/stderr 继承独立 pipe。测试进程保存读端，暂停读取而不关闭句柄。

1. 先真实落笔形成基线，再暂停两端读取并继续画线。`gui-f1/02-backpressure.json` 显示 stdout 已读 54548、stderr 已读 9125，stdout 持续积压 4168 字节。到 `08-paused-ui-status.json`，已读数仍相同，停读期间仍有晚期新线、实际局部擦除、Clear 全清、Undo 恢复、设置语言页响应及关设置后新线。
2. 恢复读取后双流积压归零，生产输出器首条溢出摘要 `full=11874`，后续 dropped=11878（含 contention=4）；随后再次真实绘制新线。不是仅凭“开启诊断”推定已经形成背压。
3. 再次停读、产生积压后打开 Settings；退出前 stdout=4149、stderr=676 字节待读且读端仍暂停。在设置内容区按住右键取得本窗口 capture，再左击实际“关闭软件”，保留右键至进程结束。`11-paused-normal-exit.json` 显示实际进程句柄 signaled、exit 0、点击后 0.9615s；runner `exit.json` 为 forced=false、normal-process-exit。577.57s 是该进程总寿命，不能当退出耗时。

独立审阅已直接查看截图。离线原始 RGB 像素增量补充证明：晚期新线 1271、设置后新线 1279、恢复读取后新线 1274 个新增红像素；原基线擦除减少 1129 个红像素。固定画布 ROI 的 Clear/Undo 为 8106→0→8105；最初尝试的精确红像素计数相等断言差 1 像素而失败，现仅按原图与实际操作证明全清和恢复，不宣称截图逐像素一致。这是截图阈值统计边界，不替代持久化/历史数据回归。

GUI 中 stdout 确有满管背压、生产队列确有溢出；stderr 虽暂停且退出前有积压，没有独立填满的 GUI 证据。stderr 满管与取消仍以此前生产自检的 94ms Stop 为依据。未做真实 console/QuickEdit 验收。

### F4：无诊断、按住隐藏、重开、Explorer 和按住退出

运行目录 `fixes/run-f4` 所有 ConsoleOutput 标志关闭。先真实画线，再开 Settings 并确认其 owner 为 Drawpad。`gui-f4/04-strong-trigger.json` 中，鼠标 Down 后 capture=Settings；按住期间 Alt+F4 后，**尚未发物理 Up，capture 已为 0**，正常 Up 后仍为 0。随后在原工具栏位置切换橡皮实际擦除原线（红像素 1271→138），切回笔形成新线（新增 1272 像素）。

首轮还完成普通 X、重开语言页、额外 3 轮 Down→Alt+F4→Up（合计 4 轮强触发）、快速重开、从 Inkeys 打开 Explorer→选择→回笔绘制、Clear/Undo。Explorer 返回后新增 1268 个红像素；Clear/Undo 固定 ROI 为 2696→0→2696。只关闭该次新建的 This PC 窗口，保留用户原有 Downloads Explorer。

首轮验证器达到预设 600 秒上限，runner 清理子进程（exit 1、forced=true、probe-time-limit）；这不计入产品正常退出通过。保留该记录，再以同一最终 EXE 启动独立短轮：快速关闭重开后，先截图确认 Home，再执行首次 Language 内容点击并截图确认切页。在设置空白区按住右键保持 capture，左击实际“关闭软件”，约 **1.2343s、exit 0、forced=false** 正常退出，见 `gui-f4-exit/03-normal-exit-held.json` 与 `exit.json`。

GUI 证明真实显示/隐藏接线与鼠标操作；Hide→Show 精确合并、迟到 native Hide 和旧 CommitEpoch 的确定性交错仍由直接调用生产 SessionState 的测试证明，不把快速点击等同于已观测特定线程交错。

### 原始证据与覆盖范围

- 三轮日志、PNG、窗口快照和实际退出码：`Build/CanvasFreezeInvestigation/fixes/gui-f1/`、`gui-f4/`、`gui-f4-exit/`。
- 离线截图统计：`fixes/gui-pixel-validation.json`；末尾清理与重新核对 hash：`fixes/gui-cleanup-and-identities.json`。测试 PID 32436/19092/9972 均无残留窗口，原 Downloads Explorer HWND 0x6d0db0 保留，桌面前台已为原 Codex 窗口。
- F4 子任务 `research/check-review.md` 独立交叉核对上述原图、窗口 JSON、暂停计数、溢出摘要、退出与最终 SHA。
- F2 不从这些常规 GUI 操作推定真实 Office/WPS 联动通过；F3 不从普通落笔推定完整 Run/GPU 或真实触摸/笔 idle 触发已通过。物理触摸/笔、Win7、多屏及原始用户冻结现场仍未完成验收。

## 格式、规范与任务状态

- `git diff --check` 通过；37 个本轮源码/工程/规范文件严格 UTF-8、当前纯 CRLF，既有 BOM 由实现/检查双方按字节保持，未全文件格式化。
- 五个任务 implement/check manifest 校验均退出 0；新七节可执行合同已加入清单，原三份规范与 native-desktop index 链接已同步。
- 没有 commit/push；父任务保留原始现场调查，四项实现与本机针对性验收已完成，F1/F4 鼠标 GUI AC 按上述范围勾选。完整默认窗口套件仍失败，任务不归档；真实物理触摸/笔、Win7、多屏、真实 Office/WPS 联动和 console/QuickEdit 分别保留，不由本机 CLI 替代。

### 默认窗口失败的补充 A/B

- 使用当前构建的真实 Window.obj/IFC，在 Default 桌面先 RequestTopmostRefresh，再 SetSettingOwnedByDrawpad：回滚前 owner 确为 Drawpad，root/Setting WS_EX_TOPMOST 均为1，但 Setting 排在 Drawpad/Presentation 之后，matches=false，最终 attached=0。raw SetWindowLongPtr 与 root SetWindowPos 均 result=1/error=0。
- 通过 git show HEAD:Inkeys/Inkeys/Window/Window.cpp 隔离编译原实现（未改的模块接口相同），不写主缓存；相同 prepromoted 流程同样 attached=0。两版非 topmost 对照均成功，HEAD fresh root 对照成功。该触发未执行新增 F4 Hide 路径，不能归为本次捕获修复引入。探针 exit0 仅指采样完成，不代表 attached=0 是正确功能结果。
- 独立入口直接链接实际 window_tests.obj 并调用原 RunSettingCaptureTests，在 Default 桌面退出0并输出 PASS setting capture ownership；没有复制状态机/测试逻辑，也没有向桌面发输入。
- 原始证据位于 Build/CanvasFreezeInvestigation/f4-review：owner-current-prepromoted-ab.stdout.log、owner-head-prepromoted-ab.stdout.log、capture-original-default.stdout.log 与对应 exitcode；完整环境/界限见 F4 check-review.md 附录。
- 全套测试仍保留 exit1，不放宽断言，也不扩大本轮补丁去修改既有 Z 序路径。解锁后已完成上述完整产品及整套窗口复测，整套最新结果仍为 exit1/11 fail。
