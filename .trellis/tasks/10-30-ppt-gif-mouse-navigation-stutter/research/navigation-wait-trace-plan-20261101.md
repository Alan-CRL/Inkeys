# 2026-11-01 等待标记不可见的最小边界诊断

## 已授权目标与证据

用户要求持续准确定位并执行修复，人工测试交用户；最新样本在1.637秒导航中三点仍不可见。等待模型、状态比较、RequestAll和DrawButton均有代码，但现有PptNav业务入队记录不能证明UI接受、配置、绘制或最终提交。因此先补区分这些分支的证据，不猜测性更改等待/页码/调度。正常退出本轮code0，旧退出超时未定位，不盲改清理。

## 最小行为差距与边界

需要证明同一localSession/epoch/方向的等待状态在哪一边界消失。归属为Ppt等待发布→PageControl状态合并→目标surface配置/绘制→真实呈现事务；不是PptCOM访问或页码ready策略。

预期文件边界：

- 新窄内部数值helper（优先Inkeys/PptWaitingDiagnostics.h）：固定容量多producer POD记录与停产后输出；不建通用遥测框架。
- Inkeys/Inkeys/UI/Ppt/Ppt.cpp：仅记录Publish/Complete/Clear接受结果及原模型/发布revision等纯值。
- Inkeys/Inkeys/UI/PageControl/PageControl.cpp：原合并、配置、Draw/真实提交边界；只记录原有事实，不发新渲染请求。
- Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp：必要窄数值TLS作用域记录实际三点分支/原brush结果与既有绘制调用数；不新增API读取，不改绘制/颜色/脏区，不把FillEllipse调用当成像素已呈现。诊断TLS严格RAII恢复且与导航TLS分离。
- Inkeys/IdtMain.cpp：早期特殊模式返回后/生产线程前prepare，在PageControl/render producers真实停止后输出。
- 既有headless定向测试文件：disabled零读钟/并发固定槽/overflow/输出异常/LastError，必要实际生产helper状态关联回归。
- 必要窄public测试hook/模块接口若现有fixture不能复用，实施前先向主会话说明；不改产品PptState字段或公共COM ABI。

不改PptCOM.cs、dynamic/释放、超时/公平预算、旧页码保留、ready/画布输入、动画/脏区/颜色；不启动GUI、不提交。

## 数据与生命周期合同

- 默认关闭，复用INKEYS_PPT_PERF=1；固定容量明确，记录pid/tid/QPC/frequency及stage、surface、session/epoch/direction、layout/publication/model revision、waiting/accept/reject/commit结果等必要纯值。禁止文稿路径/标题/墨迹/指针/HWND地址。
- 原锁内只复制数值/必要短采样，不做格式化或输出；render callback只写有上限POD槽，不能WriteFile、同步IDTLogger、分配或等待。禁用不读新时钟、不格式化、不写诊断槽。
- 不新增日志线程。正常退出所有producer真实join之后才格式化输出原stderr，独立[PptWait]前缀，避免与PptPerf span冲突；launcher原采集无需改环境/配置。保留capacity/seen/retained/dropped/invalid，满不覆盖。强退/崩溃前未flush会丢记录，必须明确限制，不能当作业务阶段没发生。
- 若利用已有异步数值capture可满足更小diff，可替代专用helper，但不得更改正常诊断/渲染策略。
- 输出错误不影响退出，保留GetLastError；采样不依赖PptPerf导航TLS，不增加COM读取。单个marker不代表光学可见；软件成功提交必须来自真实GetDC/ULW/ReleaseDC/EndDraw/receipt链，不能用callback结束代替。
- 记录Publish被拒原因或足以唯一判别的模型快照；记录PageControl被旧revision合并拒绝、应用后值；surface配置/绘制与原呈现结果严格关联同一快照/attempt。不能用后来model snapshot为之前像素背书。
- 去重只压缩相同diagnostic状态，不能改变产品去重/调度；失败/Retry/未绘制/未提交不能被成功标记覆盖。若容量掉样则对应链路unknown。

## 验证与交接

无窗口helper验证与原PPT定向测试；适用的既有真实离屏等待像素/呈现fixture，不启动主窗口。完整InkeysRepo.sln Debug|ARM64、ARM64原生MSBuild、PATH workaround、至少5分钟预算，核对DLL/TLB/EXE内嵌与入口PrepareOnly。独立review检查无热路径I/O/新wait/COM或UI行为变化、真实提交判定与并发槽协议。

准备通过后用户只需原入口操作到一两次明显等待，再正常菜单/设置退出以flush；无需重复大矩阵。证据到后只修实际消失边界。
