# R2五窗独立Touch实施计划（用户已批准）

1. 冻结现有dirty基线：已有诊断和文档归本任务；两个已修改CSO与其他任务目录不覆盖/清理/混入输入修复。读当前PRD、touch-fix-design、研究和spec，记录代码边界。
2. 先明确五个独立TouchContext的现有状态与最小路由元数据，增加操作显式context的共享helper及无窗口测试：候选选择/registration identity/Down锁定、Move-Up跨接收窗、dup/旧代次/Cancel/无命中、primary-fallback及有界状态。预留NativeCandidate查询和客户端适配接缝，不重构共享worker业务。
3. 实现Bar完整已呈现probe与Page原CaptureInputSnapshot适配，注册五个native target。通过现有spec.created在正确owner阶段登记；注销/窗口生命周期配对清理。WindowFromPoint严格单候选及TLS TouchQueryScope锁外查询后校验，不搜索后方窗口。
4. 五窗分别拥有WM_TOUCH入口和本地TouchContext，复用同一解码规则，每个原句柄只处理一次；跨窗层按值交给目标自己的Touch适配；目标确认后才申请lease/capture，再送原共享mailbox；源HWND/目标context/本地代次/注册代次在诊断中记录，不新增一份global currentTouch业务状态。保留原Touch原始信息与Bar haptic绑定元数据，不转发原句柄或递归WndProc。
5. Page过滤改为与Bar一致的IsPointerGeneratedMouseMessage，删除Pen兼容业务分支。保留真实Mouse/触摸板/Raw、来源版本和非鼠标租约。不继续按IsPenGeneratedMouseMessage拆Down/Up。
6. 完整接通owner/worker取消、overflow、隐藏/销毁/注销、语义/显示变更、同线程SetCapture重入；旧route/capture/registration代次不能释放新手势。取消不生成Click，lease申请后失败必回滚。
7. 沿现有真实生产探针补Bar及Page适配与共享worker集成回归；独立check核所有权、锁顺序、opaque/locked/popups、取消、真实Mouse与触摸长按，修复任务内问题。更新ui-pointer-session/pen-haptics等相关合同，保留诊断输出。
8. 主会话完整InkeysRepo.sln Debug|ARM64（ARM64原生MSBuild，单一Path、禁节点复用，总超时≥900s）、--ui3-input-worker-test/适用状态Host探针和headless --no-window（900s）。不启动GUI；编译环境问题不改项目/依赖绕过。
9. 实现获批准后，沿既有Solution另构建Release|x64（长LTCG超时1800s），核PE架构/诊断标识后交付同一CMD采集方法，用户只需一次持续感应跨窗、点击/按住/离开范围的真机复核。无窗口通过不冒充真机修复。

## 必须覆盖的回归

- 五套context互不覆盖：本窗Up/Cancel不误清其他窗，新注册/旧context的迟到消息不串用；共用线程/队列保持窗口与接触身份隔离，不新增五线程或五套重复实现。
- 原日志形态：Touch在旧Bar而Pointer目标Page；旧Page而实际Bar/另一Page，同一范围ID多次Down/Up不用OutOfRange也正常。
- Down72d/Up7ad及反向、重复兼容消息均不会产生独立Pen会话或双动作；完整Touch终态释放一次，无26次旧owner滞留。
- Hover跨窗与按住跨窗分别测试，Down外UI后移入不启动；拖动/连续选色保持Down owner，PPT长按无Move持续、Up/移出停止，白板Up一次。
- Bar全部面板/More/圆角/阴影/底栏Rigid-Body-Grip变换和Page菜单/主题遮挡；外部/设置/画布Native候选拒绝，可见locked背景阻挡，alpha0边界不误接纳。
- SetCapture重入取消/隐藏/销毁目标，提交前后generation变化；队列溢出或注册卸载先取消native route和业务，旧Up/Cancel/capture票据不能完成/释放新代次。
- 多点primary/fallback、不同hSource同ID、ID复用、批次旧fallback Up；保留源身份/时间回绕，Mouse/精密触摸板/可选Raw缺失仍工作。
- Bar触觉依旧可选且与UI独立；分页不加振动，Hook/Draw3无变化。保持现有源码BOM/CRLF、中文关键注释和minimal diff。

## 风险与回退

新增/修改预计位于UiPointer领域router、Bar.Interaction及必要的已呈现输入快照接口、PageControl.cpp/.cppm、IdtMain五窗登记、现有纯/生产测试；工程文件仅当新源码确需登记时最小修改。不得扩成全桌面路由/通用UI框架。

只回退本次统一Touch增量，不回退此前用户确认的共享样式或详细诊断，不使用git reset覆盖已有工作。用户已明确批准最新五套独立状态方案，现派发实现；不commit/push。
