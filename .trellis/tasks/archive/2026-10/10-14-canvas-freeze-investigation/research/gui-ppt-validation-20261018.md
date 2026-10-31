# 2026-10-18 真实产品与 PowerPoint 验收

## 产物和隔离

测试 Windows 11 ARM64，2880×1920，鼠标真实原生输入。独立目录 `Build/CanvasFreezeInvestigation/fixes/run-acceptance-20261018`，EXE SHA256 `a35d9c0adb20a6d251e03b2a00f0c0cf94161c6c8d029ec728e876a43aca6025`，来自当轮成功完整 InkeysRepo.sln Debug|ARM64 构建。ConsoleOutput 下 Cursor/Draw3/PptCOM/ShapeRecognition/TouchArea 均 false；PptGate 为既有 INKEYS_PPT_TIMING 文件日志，不开控制台绘制输出。后续窗口修复仍在迭代，本报告不把本产物的结果冒充尚未构建的最后产物；最后产物补测另记。

## 设置、Explorer 和工具

PID25068，设置 owner 实际挂到 Drawpad 后测试。三轮内容区 RightDown→Alt+F4：先确认 Settings 实际拥有 capture；隐藏后、发送物理 Up 前均 capture=0。Up 后分别画出1257、1258、1280个新红像素。重开首次点击实际切到语言页，普通X正常隐藏。橡皮将第一条测试线的ROI红像素1257→0；再切画笔新增1109像素。

从 Inkeys 打开真实资源管理器，点击选择后再返回画笔，新位置新增1265像素。只关闭此次新增 This PC HWND `0x1c50fb6`；原用户 Inkeys Analytics Explorer 保留。Clear将测试区域红像素6199→0，Undo恢复6199。实际设置“关闭软件”正常退出；最终无该PID的HWND，未强杀。原始 `gui-f4-final.json`、`gui-tools-final.json`、对应 PNG 和日志均在 `Build/CanvasFreezeInvestigation/final-20261018`。

早期自动化受上一轮 PID32368 尚未退出、两套工具栏重叠影响，未将这些操作计入本轮验收。已通过自身设置关闭软件正常退出旧实例，记录 `old-probe-normal-exit.json`，exit0/约1.218秒。之后才在唯一实例重新完成上述有效测试。

## 真实 PowerPoint

通过 Office16 PowerPoint.Application COM 创建本任务自己的三页白底文稿。fixture 位于 `ppt-gui-final.ps1`；不接管用户文稿，既有上轮自建文稿先核对 FullName 后正常Close/Quit。新的文稿/控制/状态在 `Build/CanvasFreezeInvestigation/final-20261018/ppt-final`。Inkeys PID15892，使用同一个A35产物。

通过条件：真实 PowerPoint 页状态、PptGate接受目标、成功文档Present和同session/revision的page_ui_ready；随后以屏幕ROI新增像素核对实际落笔，不能仅用日志ready或指针移动代替。

- 第1/2/3页分别产生1294/1327/1301个新红像素；翻至第2页时第1页ROI为空，返回第2页原1327像素恢复。
- 鼠标仍按住时，从第2页切至第3页，在物理Up之前已取得第3页UI-ready；Up后新的Down继续产生1300像素，不依赖另一次控制命令解除等待。
- 快速1→3→1→2后，稳定目标收敛且新增1303像素。
- 第2页黑屏→恢复后新增1302像素。这次Office黑屏没有触发Host suspend/targetRevision变化（前后都是session1/revision9），所以这项证明书写连续性，不冒充“同页暂停必须产生新revision”的完整GUI证据；后者由生产共享组件回归验证。
- 第3页→结束页：真实target pageKind=EndScreen并得到新UI回执；返回第3页后新增1311像素。
- 退出放映再重启，场次从session1变为session2；显式切回画笔后新增1299像素。
- 点击真实 Inkeys 右侧分页按钮翻至第2页，匹配session2/revision16的新UI回执，随后新增1301像素。

原始 `ppt-gui-results.json` 为最终pass。`ppt-gui-results-initial-layout.json` 保留首次脚本在放映重入后没有切回画笔造成的0像素失败；此时生产日志明确selection-output，点击实际让PPT翻页，是产品选择模式行为，不算画布拒收故障或修复红态。测试未改任何产品门禁或强设ready。

## 证明边界

这些结果覆盖真实PowerPoint与鼠标；未运行WPS、物理触摸/触控笔、Windows7、多屏或故障用户的原始设备。未在GUI强制耗尽内存/损坏GPU；对应completion实际分配失败和持锁回归在无窗口测试中执行。用户原始现场没有dump/同场景终态证据，不能宣称本轮已证明所有反馈共享同一根因。


## 最终B495产物补测及退出

后续清理探针与可见Setting周期刷新修补完成后，完整solution重建exit0。最终Inkeys SHA256 `b49504f9995638f8c0dfae07acb7917db36f2f36bed1d401731b674822f14318`，隔离run-clean-acceptance-20261018，ConsoleOutput全false，PID41588。Settings打开后额外等3.5秒，实际owner=Drawpad、位于两画布上方；RightDown自身捕获成功，Alt+F4隐藏后物理Up前capture=0；新线1298像素，橡皮移动→idle→Move擦除1298→0。真实PPT持笔1→2页在物理Up前取得新UI-ready，Up后新线1303；返回页1新revision、新线1305；Explorer→选择→画笔新线1304。设置重开首次点击和普通X通过，按住内容点击真实关闭软件exit0/1.688秒、无残留HWND、forced=false。原始gui-clean-results.json为pass。

先前51FE产物的真实GUI失败及PID41604正常退出0/1.547秒保留；错误是本轮NOOWNER后排序把画布推到可见设置之前，最终移除该步骤NOOWNER并加严格Show后RootRefresh断言。不是用延长等待掩盖失败：最终同时读取实际Z序与capture，并验证真正新像素。最终B495关键集成链与此前A35较广PPT矩阵各按自身SHA记录；不冒充全部矩阵都在最终SHA重复运行。统一验收见final-validation-20261018.md。
