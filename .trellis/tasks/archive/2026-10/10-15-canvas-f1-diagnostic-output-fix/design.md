# F1：标准流背压修复设计

## Change Boundary

修复已审计Draw3/RTS生产stdout/stderr同步sink对画布、完成通知和正常关闭的背压。不接管全进程CRT，不改变IDTLogger文件日志，不修改UInk持久化写入。默认关闭的专用尾轨迹文件及独立debugger-only路径暂缓。

## Selected Architecture

新增Draw3.DiagnosticOutput.h/.cpp窄实现：固定1024条、单条至多4096字节的有界文本记录（约4MiB payload），包含目的流和必要mirror标记；一个独立native输出worker。实时生产者经原开关检查、有界格式化和try-only路由/队列发布，不等待队列、输出端或worker；失败可丢诊断，无同步fallback。保留前缀、关键字段、Cursor seq/tick/producer TID及stdout/stderr目的地。

输出worker只拥有独立状态、复制缓冲、自有event和DuplicateHandle得到的标准句柄副本；console走原窄字节语义，普通file/pipe走WriteFile。所有I/O在队列锁外；处理部分写、0进度、broken pipe和无效句柄，不自旋。移入标准流的原配对debugger mirror一起迁移。

生命周期由wWinMain进程级局部owner管理，在InitializeDebugConsole之后、Win7/Cursor诊断启用前接线，覆盖early return；Host设备恢复不新建worker。相关开关关闭且无可用继承输出端时不分配大队列/线程；仍有继承输出端时保留普通错误诊断。

## Stop and Resource Ownership

规划停止总预算1000ms，其中至多100ms尝试drain，全过程共用首次计算的绝对deadline。先停止接纳，在路由保护下解除发布，唤醒worker，再发布取消并在剩余预算内重复CancelSynchronousIo + 短Wait；ERROR_NOT_FOUND和取消请求成功都不等于线程结束。

路由门闩保证生产者在SetEvent前仍保护上下文/event生命周期；生产者只try shared，Stop在期限内取exclusive解除发布。worker每次外部输出前后检查取消，重复取消覆盖“最后检查后尚未WriteFile”的竞态。

只有worker已signaled且路由无读者才正常释放队列、event、上下文；句柄副本由worker在最后I/O返回后关闭。取消无法完成或路由无法及时取得时返回明确IsolatedPending，保持接纳关闭并保留最多一份独立state至进程结束，不新建第二worker，不无限join，不TerminateThread，不访问已销毁画布/HWND/COM/CRT日志对象。正常成功不能依赖15秒退出监督强杀。

## Expected Files and Necessary Breadth

- 新增Inkeys/Inkeys/Drawing/Draw3/Draw3.DiagnosticOutput.h/.cpp；Inkeys.vcxproj/.filters仅登记必要文件，不更改SDK/编译器/依赖/工具链。
- Draw3.Diagnostics.cpp/.cppm：Cursor、FrameTiming、RTS trace、CanvasPan及HRESULT/窗口/adapter输出和旧flush合同。
- Draw3.DrawingController.cpp、Draw3.Host.cpp、Draw3.RealtimeStylus.cpp、Draw3.WindowControl.cpp、Draw3.PenCursor.cpp：实际绘制/输入/状态/窗口诊断与标准流配对mirror。
- Draw3.GraphicsInitialization.cpp、Draw3.TransparentPresentation.cpp、Draw3.Renderer.cpp、Draw3.RendererPrimitives.cpp、Draw3.HapticFeedback.cpp：运行期恢复、呈现、光标及触觉诊断。
- Draw3.AutoSave.cpp、Draw3.PresentationAutoSave.cpp、Draw3.Product.cpp、Draw3.PptTiming.h：完成通知前输出和错误兜底；只迁移文字日志，不改变保存/加载/事件协议。
- Inkeys/IdtMain.cpp：窄生命周期接线及现有早期自检模式中的新参数分发。

逐调用点清单、行号和测试专用输出边界见父research/f1-fix-plan.md。文件较多是消除真实旁路所需，只替换输出接线，不进行格式化或业务重构。共享Controller/PresentationAutoSave/IdtMain文件由主会话和F2/F3实现者按顺序整合。

## Tests

计划新增 --draw3-diagnostic-output-test，调用真实生产队列/writer/Stop；严格argv、在配置/单实例/HWND前退出，Release harness-off明确拒绝。覆盖正常file/pipe和目的流、stdout/stderr分别停读、满队列、多生产者/Stop、读端恢复/断开、初始化失败、部分写/0写、重复Stop。

在真实worker最后stop检查后/真实WriteFile前加仅harness有限屏障，验证首轮ERROR_NOT_FOUND后仍能取消。独立不可取消等待接缝只验证deadline与state保留/UAF，不作为产品自然触发。正常pipe取消须实测worker已退出；超期最多一份状态、不重启worker。

完整产品用原停读管道条件保持reader活着并证实积压，实际新笔/Clear/设置和工具继续，停读时正常关闭在监督前完成；另覆盖TouchArea/stderr、关闭诊断和正常console输出。QuickEdit、物理触摸/笔、Win7未实际验证则单列，不拿pipe/简化RTS模型替代。

## Dependencies and Rollback

无其他子任务的功能依赖；开启诊断的全项GUI前先合入本项。标准流迁移与writer生命周期一起回退，不能只恢复同步旁路或只删Stop；保留其他工作者变更。不开新日志框架、不修改专用轨迹文件、不commit/push。
