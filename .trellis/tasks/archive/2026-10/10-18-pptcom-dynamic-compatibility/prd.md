# PptCOM dynamic 优先、损坏接口兼容与确定性释放修复

## Goal

修复用户所用 20261014a 中的 PPT 联动兼容回归。首要目标是：当 PowerPoint/WPP 的类型库、PIA 类型接口或事件接口异常，但 IDispatch/dynamic 仍可访问时，用户仍可使用大部分既有功能；使用结束后不因 Inkeys 遗留 COM 引用而让 PowerPoint/wpp 长期驻留、卡住或影响下一份文稿打开。

## Authorization and phase

- 2026-10-18：用户明确授权创建 Trellis 任务、继续调查并提交修复规划。
- 用户已于2026-10-18审阅并批准方案，要求开始实现；允许执行task.py start和计划内产品/规范/产物修复。
- 实施可写范围为已批准设计所列产品、测试、规范及配套DLL/TLB；保持最小diff，不执行commit/push。
- GUI / Computer Use / 启动 Office 主窗口的测试仍须另行获得用户明确授权；批准代码方案不自动覆盖 GUI 测试。

## User requirements

- R1. 常规 Office/WPP 属性、集合和操作调用优先使用 C# dynamic。不得把反射统一 accessor 当作 dynamic 的等价实现。
- R2. 只有有具体证据证明 dynamic 办不到的成员/能力，才可采用明确列出的类型包装或反射例外；事件绑定属于允许的已知例外。例外失败不得阻断其他仍可访问的功能。
- R3. 类型包装不可用、事件不能绑定或注册部分失败时，自动采用不依赖该事件接口的轮询保底；启动前已在放映、换页、结束、重开及切换文稿均能恢复。
- R4. 每笔 COM acquisition 必须有明确的所有者、转移或 finally 释放。禁止链式调用遗失临时对象，禁止用 GC/延迟终结器代替确定性释放，也禁止对共享借用对象过度释放。
- R5. 排查和处理会导致 Office/wpp 后台驻留的同 identity 跳过释放、ROT/去重重复释放、临时 View/Navigation、事件连接以及清理与业务调用交错；既存问题也纳入本次 COM 生命周期边界，禁止扩大为无关重构。
- R6. 同一已验证场次的短暂 busy 不应造成不必要的整场断联；恢复后应有有界重试。保留旧快照不能冒充新页、新文稿或结束页的可写 ready。
- R7. dynamic 页码可用、类型 HWND 不可用时，恢复可靠的窗口发现回退；必须确认目标放映窗口及其进程，不得用任意前台窗口、仅相同标题或不唯一结果冒充身份。
- R8. 单个可选能力不可用只降级该能力。SlideID 不可得时允许显式页序号模式；不伪造 SlideID，不把可选工具/页面总览等失败升级成整场联动失败。
- R9. 核查 PowerPoint 2010（MSO14）与 WPP/WPS Presentation 2013 的实际访问和释放兼容性；区分微软 MSO/PIA 版本与 WPS 产品版本。当前产品还标称 PowerPoint 2007，保留既有声明与分支，不擅自提高最低版本。
- R10. 对 COM 激活、ROT 可见性/权限、IDispatch 核心访问均彻底失败的设备明确记录边界，不能承诺 dynamic 能修复完全不可达的 COM。
- R11. 保持现有 COM GUID、旧接口方法顺序、类型库 ABI、.NET Framework 4.0、依赖版本、页面/会话/墨迹隔离和用户配置兼容。
- R12. 保持最小 diff、原文件编码/换行，在关键边界加适量中文注释；构建和验证遵守用户给定的 solution Debug|ARM64 与 ARM64 MSBuild 规则。

## Expected feature behavior

| 能力 | 类型/事件接口异常但 dynamic 可用时的目标 |
| --- | --- |
| 识别放映、总页数/当前页、进入/退出场次 | 不依赖 PIA cast 成功；无事件轮询可用 |
| 上一页/下一页、结束、激活放映窗口 | 核心 dynamic 调用可用，临时对象及时释放 |
| 页级画布、墨迹恢复/保存 | 有可靠文稿/页/窗口身份时可用；缺 SlideID 显式退化，不串文稿 |
| 原生笔/激光笔接管、幻灯片总览 | 提供方支持且成员可用时可用；缺失时仅该能力不可用 |
| 即时事件通知 | 允许因事件接口损坏不可用，由轮询补足主要联动 |
| Office/WPP 用户关闭及下一文稿打开 | 不因 Inkeys 可控引用或事件连接造成延迟退出/锁住下一文稿 |

## Acceptance criteria

- [x] AC1. 常规 COM 访问通过生产 dynamic 适配器测试；动态对象无可反射 CLR 属性时仍可读取。typed/反射例外有逐成员证据和失败策略清单。
- [x] AC2. 模拟 PIA/QI/事件失败而 dynamic 正常，能够识别放映并通过现有控件完成核心命令、基本页码、结束及重开；仅窗口身份缺失不阻断对象定向的基本控制，不能由 HWND0 误判场次退出。
- [x] AC3. 事件部分注册失败能够回滚已建立订阅；无事件轮询持续运行，不因 bindingEvents/forcePolling 状态遗留丢失恢复。
- [x] AC4. 单次及连续 busy 的新旧公开缓存语义一致；同场同页安全保留、跨页暂停、新场隔离，恢复不受无关 3 秒稳定复核定时器阻挡。
- [x] AC5. descriptor HWND 缺失时按受验证回退尝试确定唯一目标；多实例/同名文件/窗口销毁复用/非目标前台均不会误绑定。无可靠窗口时只保留基础控制并隔离页级墨迹，基础页码不产生Draw3 ready或污染墨迹缓冲。
- [x] AC6. 每个取得点、每个异常点、重复扫描和命令路径的 COM release ledger 配平；借用 alias 零额外 release，无 double release、无共享 RCW FinalRelease 误用。
- [x] AC7. 结束/切文稿/重绑/退出时先停止使用并解绑，再释放对象；并发在途调用不被释放击穿，不死锁。
- [x] AC8. 不以 GC.Collect、WaitForPendingFinalizers、sleep 等待后台自然退出或杀 Office 进程作为释放正确性或验收通过依据。
- [x] AC9. 记录 PowerPoint 2010、WPP 2013、PowerPoint 2007 保留项的逐功能矩阵、版本/位数和证据等级；无法取得的旧版本明确 NOT VERIFIED，不能用现代 Office 或 mock 冒充通过。
- [ ] AC10. 授权后的真实提供方验收覆盖健康/类型接口损坏或等价隔离夹具、事件/轮询、关文稿/退出、再开下一文稿；不改用户系统注册，不接管/终止已有 Office。
- [x] AC11. COM ABI 与依赖未改变；DLL/TLB 显式配套交接，native 实际嵌入产物与本次托管构建相符。
- [x] AC12. 相关 managed/native 无窗口测试、完整 InkeysRepo.sln Debug|ARM64、diff/编码换行检查通过；源码错误与本机环境错误分开记录。
- [x] AC13. 更新与用户 dynamic 优先要求冲突的 COM/spec 条款及所有权/回退规范；产品兼容文案不无证扩大或缩小。
- [x] AC14. 用户已批准设计和实施计划后才开始实现；任务完成不自动授权 commit/push。

## Non-goals

- 修复系统 Office/WPS 注册、安装/卸载 Office、全局注册表或权限调整。
- 承诺 COM 完全不可激活或核心 IDispatch 不可用时仍提供同等联动。
- 重做 Draw3/UI3、存储格式、所有线程模型或完整发布流水线。
- 仅替换旧 DLL 或放开身份门来绕过新 native 合同。

## Evidence and planning outputs

- design.md：技术边界、最小修复、降级决策与释放合同。
- implement.md：用户批准后的执行顺序和验证门。
- research/：前轮调查证据、版本矩阵、所有权审计及状态/轮询方案。


实施/自动化验证记录：research/verification.md。AC10的真实提供方/进程退出/下一文稿验收保持待办；本机夹具及静态兼容矩阵不代替实机结果。
