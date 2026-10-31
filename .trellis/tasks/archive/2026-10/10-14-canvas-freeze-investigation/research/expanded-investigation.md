# 扩大调查：已验证的画布停滞触发器

日期：2026-10-15（Asia/Shanghai）。源码 HEAD：5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb。任务保持 planning，本轮不实施修复。

## 结论与证据等级

按用户追加要求，本轮不以视频中的 frame 日志排除其它用户的死锁/永久等待，审计整个输入、绘制、窗口、状态与持久化链。新截图“画布卡死，但设置仍可操作并重启”作为独立症状约束。

已找到并验证三条具体缺陷，其中第一条在未经修改的当前 HEAD 完整程序中复现“画布停住、Settings 仍可交互”；第二条使用真实产品 worker/文件/Bridge 加原码 Controller/Host 片段复现持续拒绝输入；第三条证明活动接触的等待协议缺口。**仍未确认哪一条造成原始视频或所有用户报告，未证明一个共同根因，也未发现可确定闭合的应用内双锁死锁。**

| ID | 触发与结果 | 运行证据 | 恢复边界 |
| --- | --- | --- | --- |
| F1 | 开启 Cursor 诊断，stdout 为存活但停止读取的管道；缓冲耗尽后 Draw3 线程同步写入一直不返回 | 完整 HEAD GUI；两次触发、三次线程采样、设置页切换、恢复读端后同进程恢复绘制 | 输出端恢复读取/关闭或进程终止；程序没有写入超时。未实测传统控制台 QuickEdit |
| F2 | PPT 已保存冷槽恢复时合法 UInk 被 FileShare.None 短暂占用；IoError 被归成 SourceChanged，进入 manualOnly，文件解锁后仍抑制输入 | 真实产品服务、文件编码/读取、Bridge + 当前原码 gate/Host 条件；最终 build/run 均 0，无 warning | 同 target/工具切换不重试；明确新 load 成功。新场景/目标 revision 可触发重读，非全工作区永久失效 |
| F3 | Speed 橡皮接触静止收敛后进入无限重复等待，Touch Move 成功但没有 wake | 原 ContactInput/SpeedEraser + 摘录 Controller while/Drain，退出 0，所有断言通过 | 真 Up/Cancel/其它 control wake 可解除；不能解释有效抬手后所有后续笔画永久失效 |

第三轮新增窗口输入缺陷F4与真实窗口边界实验见 [窗口关联调查](window-interaction-investigation.md)：F4可使主栏点击持续失效，但同场新线成功，不合并成第四条已证画布永久停顿。

## F1：同步诊断输出卡住唯一画布消费者

### 生产调用链与普通配置前提

- `DrawingController::Run` 在 `Draw3.DrawingController.cpp:12607`、`:14454` 执行 `FlushCursorDiagnostics()`。
- `Draw3.Diagnostics.cpp:574–617` 的 flush 先复制有界队列并释放 mutex，随后仍在**绘制线程**执行输出。
- `Draw3.Diagnostics.cpp:48–55`：`WriteConsoleA` 失败后直接同步 `WriteFile(..., nullptr)`。没有超时、取消或与绘制隔离的输出 worker。
- `IdtMain.cpp:295–325` 保留启动器的 stdout/stderr 文件/管道；两路已重定向时不创建新控制台。这是正式支持的启动路径。
- `Other.Config.cppm:360` 的 Cursor 默认 false；Settings 实验室中的“光标调试信息”说明下次启动生效。不能声称所有默认配置用户都会触发。
- [Microsoft 匿名管道文档](https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations) 明确：缓冲已满时同步 WriteFile 等待读端释放空间。这里只暂停读端，不暂停/修改 Inkeys 线程或注入产品故障。

### 完整程序受控对照

运行的是上轮从当前 HEAD 构建的 ARM64 程序，SHA256：`9316b86f5d08f0135b8e583fe490211fbcfb59f1c5c0fa055a3abea8865347fd`。副本在 `Build/CanvasFreezeInvestigation/run-head/`，配置与用户安装隔离。使用原生 Win32 输入和窗口采集，没有使用 Computer Use。

1. `pipe_backpressure_probe.py` 启动唯一子进程 PID 16544；stdout 接匿名管道并持续读取到文件，stderr 直接到文件。
2. 正常读取时，实际鼠标 Down/Up 经 RTS published=1，屏幕出现红线；已打开的 Settings 正常显示。
3. 把探针控制文件改为 pause。尝试第二笔，画布不显示新笔迹；Settings 从主页成功切换到“绘制”页。读出字节保持 60205，管道仍有 4461 字节待读。
4. 三份线程栈中的 Draw3 TID 5192（由实际 `frame` 日志匹配）均停在 `ntdll!ZwWriteFile → KERNELBASE!WriteFile`。第一次暂停期间相隔数分钟的两份样本具有同一 PC/SP；第二次暂停的第三份样本也命中同一位置。Host 没退出，Settings 仍活。
5. 恢复 read，无需重启；同一进程出现先前积压笔迹和新画笔笔迹，读出字节推进到 145487。
6. 再次 pause 并尝试新笔，第三份栈再次命中同一 WriteFile 调用；恢复 read 后读出字节继续到 227775。
7. 结束时探针仅终止自己持有的隔离子进程；没有留下测试 GUI。

取栈工具先恢复被短暂采样的线程，再打印/符号化。原 `StackWalk64` 在 ARM64 PAC 返回地址处停止；第三份记录保留原始 PC，并仅在低48位候选落入已加载模块时作显示解析。它落在 `WriteFastConsoleLine`，源码 `Draw3.Diagnostics.cpp:55`。**完整 Flush→Run 栈没有展开；该上游调用链由源码和 TID/frame 日志共同支持，不能把候选地址冒充额外采样栈帧。** 第二份样本有一个非目标线程在枚举后退出，open_error=87；第一/三份全量采样退出0，Draw3三份均成功恢复线程。

证据（均在忽略的 `Build/CanvasFreezeInvestigation/`）：

- `pipe-experiment/stdout.log`、`stderr.log`、`before-status.json`、`blocked-status.json`、`resumed-status.json`、`exit.json`。
- `pipe-experiment/blocked-stacks-1.txt`、`blocked-stacks-2.txt`、`blocked-stacks-3.txt`。
- `pipe-before.png`、`pipe-paused.png`、`pipe-resumed.png`。
- 可复用源码：本任务 `research/pipe_backpressure_probe.py`、`thread_stack_probe.cpp`、`native_probe.py`。

### 最小修复方案（未实施）

让唯一画布线程只做有界、不会等待输出端的入队/丢弃，把同步 console/pipe/file 输出移出画布线程。既有有界 ring 只能限制内存，不保证 consumer 的 I/O 有界。应统一检查 Cursor/Draw3/TouchArea 等实际同步输出入口，避免只迁移一个 flush 后仍在其它绘制路径调用阻塞 sink。

诊断消费者退出也必须有明确取消/超时与资源生命周期，不能把无限 join 留给退出流程。不是把 WriteFile 换个 API 或单纯增大缓冲。回归必须保留读端存活且停读、缓冲耗尽的条件，并验证新笔/Clear/设置及关闭仍前进；另测诊断关闭与正常读端。

### 归因限制

实验确立代码活性缺陷，不能直接证明用户控制台发生过停读/选择暂停。QuickEdit/Mark、终端是否阻塞仍需实际模式与现场栈。若用户未开启相关诊断、没有阻塞输出 sink，此条不能单独解释其故障。

## F2：PPT 临时读取错误变成无自动恢复的准入状态

`Draw3.PresentationAutoSave.cpp:1568–1575` 将 `ReadUInkFile` 非 Complete 与真正身份/内容变化合并返回 SourceChanged。`Draw3.DrawingController.cpp:12061–12077` 仅对 IoError 安排重试；SourceChanged 调 `OnTerminalFailure()`（`:870`），置 manualOnly，未初始化的当前槽继续阻止 Down/Clear（`:9850`、`:12262`）。`Draw3.Bridge.cpp:75–78` 对同 target 去重，`Draw3.Host.cpp:1294–1304` 只在目标版本变化时再排目标命令，工具切换不会提交新 load。

独立 probe 复用了本次完整产品构建的 ARM64 IFC/obj、真实 `PresentationAutoSaveService`、UInk 严格编码/读取和 Bridge；Controller retry/失败分支、Host目标条件逐段取自当前源码，哈希保存在 `presentation-load-probe-source.json`。**未运行完整 Controller/Host GUI，本项不是第二份完整 GUI 卡死复现。**

最终结果：

1. 保存一个合法快照，baseline Current load 返回 Loaded。
2. 以共享模式0打开该已保存 UInk；真实 ReadUInkFile 返回 IoError=6，真实 worker completion 返回 SourceChanged=4。
3. 原码失败分支保持 persistenceInitialized=false，实际输入谓词持续抑制绘制。
4. 释放句柄后读取立即 Complete，sourceRevision/SHA256未改变。
5. 原 retry gate 即使传入24小时后的时间也不重提；这验证状态机，没有实际等待24小时。
6. 真实 Bridge 同 target 发布不推进目标revision；仅改工具不满足原 Host 重载条件。
7. 显式提交新 load 返回 Loaded，验证 worker 和文件已恢复；新 binding 身份可推进目标revision。

`Build/CanvasFreezeInvestigation/presentation-load-probe/build.exitcode` 与 `run.exitcode` 均为0，`RESULT failures=0`。最终编译与产品匹配 MSVC14.44、/MT、/fp:precise；初轮探针的 C5050 环境差异已通过匹配探针编译参数消除，未修改产品工程或依赖。

最小修复应把暂时读取 IoError 保持为 IoError，进入已有有界退避；SourceChanged保留给有证据的内容/身份变化。重试耗尽/确实需人工处理时应提供明确提示与能绕过“相同目标去重”的受控重试入口，但不能把未读出的旧墨迹当空白、放开输入覆盖数据。此项只在 PPT 已保存槽重新从磁盘恢复时可达，warm槽、普通Desktop不等同。

## F3：速率橡皮的接触移动不唤醒已睡眠的画布线程

真实 `PublishMove`（`Draw3.ContactInput.cpp:771–801`）只更新 mailbox 不 signal；controller 在无动画/无面积deadline的速率橡皮 idle 状态执行 `Draw3.DrawingController.cpp:14551` 的 while，1000ms超时后继续用同一 generation 等待，不回外层读mailbox。

Touch RTS 没有 Pen cursor wake；Pen接触中的cursor更新也有不唤醒策略。当前完整隐藏注入 `Draw3.Host.cpp:2111` 每次成功阶段另加 ControlWake，会掩盖该路径。**实际动态 probe 覆盖 Touch coordinator/SpeedEraser 和摘录等待循环；Pen 的影响是调用链结论，尚未用真实笔复现。**

`make-input-wake-probe.py` 直接从产品提取 ContactInput、原 Drain 和等待 while，并包含实际 SpeedEraser；只提供两个 WindowController 查询标记。实测 Move发布成功、snapshot可读、generation不变，超过1200ms仍未返回；真实Up立即解除，存在command boundary时Up仍解除。还验证event虽被消费，旧generation仍正确识别真正wake。

`Build/CanvasFreezeInvestigation/input-wake-probe.log`：`probe_failures=0`，退出0。修复方向是在进入休眠与生产者发布Move之间建立不会漏唤醒的握手，或在可控超时后真正回外层读取状态；不能只修改隐藏测试的额外wake。需覆盖Idle→Move竞争、Touch/Pen/Mouse、command boundary、Up与CPU空闲开销。

## 后续反馈：无控制台输出，报告同时涉及触摸和鼠标

用户再次描述“画几笔→设置开关→Inkeys打开Explorer→选择→画笔和橡皮失效”，明确使用时没有开启任何控制台输出。具体这位用户的输入设备未知；总体反馈多为触摸，也有鼠标，尚不能推断同一故障现场两种设备均已试过。

### 对已有三个缺陷的影响

- F1：Cursor关闭时FlushCursorDiagnostics在Diagnostics.cpp:576直接返回，因此已验证的Cursor管道实验不能直接解释该反馈。普通Release且诊断关闭应大幅降低其优先级。仅“没看到窗口”与“没有输出sink”并非同一技术条件：Debug LogFrameTiming还有独立输出、启动器可重定向；这些只是边界，当前没有证据，不能自行添加隐藏管道前提来解释用户。
- F2：仍需PPT已保存冷槽恢复。用户提供的是桌面设置/Explorer/选择流程，没有PPT恢复证据；不能据症状一致直接认定。
- F3：有效Up/Cancel、成功到达Host的工具状态发布会唤醒消费者。该缺陷可以解释特定Touch移动阶段停住，不能独立解释结束接触并切换工具后的持续全失效，也不能统包鼠标报告。

### 修复信心与验收分开

对F1隔离阻塞sink、F2正确分类暂时IoError、F3补全休眠/Move唤醒协议，已有具体代码位置和可重跑触发实验，可分别进行有针对性的修复；F3还需验证真实触摸/笔路径及空闲CPU开销。当前没有修复补丁，信心不是已经通过修复后验证。

“消除这三个已验证机制”和“解决用户这组设置/Explorer后失效反馈”必须分别验收。现有证据不足以保证前者完成后后者也消失。继续优先判别故障时共用输入链：真实输入是否到达RTS、是否成功发布、是否被准入拒绝、画布消费者是否仍前进，并同时保存窗口owner/透明样式与线程栈；采集不能依赖阻塞stdout或把所有快照放在可能已卡住的锁后。

本次定向核查没有新增一个设置/Explorer操作必然触发的硬故障，也没有重新运行GUI矩阵；此前正常GUI基线启用了诊断，不能称作无诊断条件下的排除性实验。专属研究见no-console-input-followup.md、no-console-settings-followup.md。

## 当前未成立的强断言与剩余现场证据

- 所有 Run 异常返回都可让UI永久留下：不成立；HEAD主线程100ms检查ProductRunning并受控退出。要成立还需要后置清理先无限等候等额外证据。
- 设置涉及两个线程，所以一定有死锁：未证明。旧Sync/ImGui闭环有defer guard；本轮对多条lock/window重入链只找到外部等待边，没有证明普通操作必然闭环。
- 漏一个Up就能让所有鼠标以后永久坏：不成立；同identity重复Down、RTS lifecycle/admission换代都能收尾。不同identity的孤立contact+command boundary是条件候选，真实丢终态触发仍未证。
- 普通打开/关闭设置必现：当前HEAD基线三次实际笔画均有RTS终态并显示。已通过真实设置关闭按钮以及Explorer/选择/绘制往返；第二次Settings开关未逐帧记录，不把它当完整脚本高频矩阵。

若用户现场与F1/F2前提都不匹配，最有价值的下一份证据是**卡住期间的画布/RTS/窗口线程栈和无锁阶段快照**，不能仅继续读取同一诊断stdout或拿WCT未发现cycle排除所有锁。使用本地已匹配的PDB及同版本二进制；不通过超时重启RTS/GPU把故障先抹掉。

## 变更与清理

本轮仅新增/更新任务研究、独立诊断程序和规划文档。产品C++、项目文件、依赖、用户安装配置均未修改；没有commit/push。独立probe使用现有MSVC/SDK和已构建产品obj，未引入构建系统。隔离GUI PID45048、16544以及本轮新开Explorer窗口已关闭，原有用户窗口保留。
