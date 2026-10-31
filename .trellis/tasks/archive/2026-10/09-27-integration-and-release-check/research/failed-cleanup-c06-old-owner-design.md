# C06：旧Window owner清理的独立有限扩展（待审）

Active task: .trellis/tasks/09-27-integration-and-release-check

2026-10-01 root只读Main真实span、Window与既有C3契约后形成。当前未新增selector/代码/运行；不能把C07三轮当本项已通过。C3-B保存失败优先处理，本单元不阻塞它。

## 需求与验收

确认DComp失败已完成Host内部清理之后，旧WindowService::StopAndJoin仍有独立scope保护。首失败由原真实style拒绝导致；仅MagnifierHost.destroyed在generation1的真实回调内受控停住。不得暂停任意线程、杀用户进程，所有窗口/data均私有。普通冷启动不设总超时；已知失败episode沿原grace+15秒fatal，不冒普通Close15秒。

hold必须见：真实StartProduct已经返回false；firstStartFalseTick/grace非零；旧Magnifier owner已进入destroyed、Root主线程尚在同span StopAndJoin；generation2尚未创建；fatal按同原grace、无parent强杀。release必须真实callback放行、所有四旧owner/HWND已死、scope取消，唯一新ULW/成功内容帧并超过旧grace+1秒仍活；最后普通Close/Stop自然结束。两组至少三轮。DComp前提不可得为NOT VERIFIED/prerequisite，不用早拒当通过。

## 技术设计 / 所有权

- Root唯一改Main/ShutdownSupervisor/FailedCleanupRealCases.h，复用当前StartDraw3ProductWithFallback，普通observation=null路径不变。
- finite enum仅尾部追加MainOldOwnerHold/Release（18/19），不重编号原17、不改变reader15–17区间；新option C06-owner-hold/release，仍原1024B/POD/argc9/三继承HANDLE授权与相同private树。
- RunMainFailedCleanupUlwCounterexample允许这两新受信case并共用当前C07四owned roles；Heap State强持local entered/proceed事件，回调只generation1/index0时SetEvent→无限wait（hold）或固定250ms释放（release）。不得在callback捕获裸栈/业务锁，new generation不继承gate。
- entered见证填原Trace.gateTick/faultReached/firstStartFalseTick/grace；物理generation和已OpenThread(SYNCHRONIZE)的四old owner HANDLE保活，fatal发布继续现幂等数值publisher。Header已定义字段足够，不加外部句柄/任意selector。
- Parent判断新hold走独立条件，不使用原startupHold的startReturned==0代替已返回的first Host false；核firstStartFalse≤gate、Stop未返、grace原tick、无newGeneration、001A自然死亡及≤2s迟到。新release核原C07全链+faultReached且无fatal。

## 执行与验证门

先独立设计审查，再root最小实现；增加两selector实际diff/safety审，必须在所有writer停写后新完整Debug0/旧C07+对应source回归才运行。release先、hold后三轮；与其它build/性能不并发。最后Release三架构相关编译和门，Win7/driver真实阻塞仍独立人工。若callback/lease/join路径无法安全静止，不退出释放被借用对象；保持原进程fatal接管。

状态：设计待独立审，implementation/tests未开始；无新的动态PASS。
