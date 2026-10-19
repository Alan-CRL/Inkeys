# 全范围实施审查（trellis-check，2026-10-19）

范围：读取check.jsonl及关联PRD/design/implement、native desktop/指针/鼠标钩子/PPT会话/配置资源规范，检查全部本任务tracked diff和新建生产/测试文件。没有安装系统Hook、运行GUI/Computer Use、提交或推送。完整solution及无窗口测试由root统一执行；本报告不重复构建，不把静态检查称为运行通过。

## 已发现并修复

1. **首次快捷键放行到下层（高）**：原matcher所有键均unknown，只有Up令known，导致第一次Ctrl+Z及每个终键首次使用无条件Forward。新增Hook安装后的受保护启动校准（只在普通服务线程），验证同一活跃桌面及HOOKCONTROL、同一前台HWND/PID、前台token完整性不高于自身，再采样所需12键高位并复验上下文。只初始化未观察键，不覆盖采样期间真实Hook边沿；已按住的键Forward直到真实Up。保护不可证实则不信任零值、保持同步等待，最多500ms尝试，快照成功后停止API采样。回调和运行期匹配仍仅使用事件。UI通过synchronizationPending显示真实等待。
2. **注入修饰键破坏精确组合（中）**：原实现忽略注入Alt/Shift等全部事件，会将真实Ctrl+注入Alt+真实Z误判Undo。增加仅阻止新物理匹配的独立注入修饰位，不产生快捷动作，不覆盖同VK物理Press；注入Up不清理物理所有权。
3. **配置切换同tick旧WM_HOTKEY复活（中）**：原清除最多256条消息并允许message.time==配置tick，旧消息可跨配置被接纳。改为严格晚于配置tick，DWORD回绕按有符号差处理；同tick新消息允许保守舍弃。
4. **新笔迹后同一Poll的Delete丢失（中）**：candidate.ready后出现内容修订，原实现到Drain结尾才退休候选，先走第二击Fence失败并吞掉本应为新清空的Delete。已通知business owner，并确认修复为Dispatch前RefreshCanvas退休旧内容候选及同Poll回归。
5. **清空/选择之间普通编辑突破事务（高）**：新contact门禁不能阻止普通鼠标Undo/Redo/Clear；Controller已回复空态后、Bar选择前可能恢复墨迹。复合lease新增实际生产OrdinaryCommandAllowed策略；Host::PublishCommand在commandPublishMutex+presentationTargetMutex同提交边界拒绝这三个内容命令。Acquire/Release共scene锁，既有已接受FIFO继续在Clear/Fence前完成，其他命令不变。新增固定lease生产测试证明阻断/释放。
6. **快速第二个X在Selection交接空档丢失（中）**：business owner补充的审查修正；Unavailable仅为同一作用域Host选择交接时，保留已接纳后继X到重新开放，避免丢掉退出→返回第二步；具名失败仍退休后继切换。已核源码及生产owner回归。
7. **饱和诊断缺失（低）**：原只保留状态计数。root已补普通Bar owner的聚合限频drop报告及owner邮箱溢出计数，不在Hook格式化/记录，不把正常Up当作忙。

## 静态合同确认

- LL和WM_HOTKEY由同一jthread泵产生新SPSC；Settings仅改原子启用代次并异步唤醒。Hook固定数组、单次原子快照、有限入队和计数，无业务锁/动态分配/业务callback/HiMsg/COM/日志格式化；负code与无关消息链传。
- 物理首次Down归属保留到repeat/Up；关开关、模式变更、先松Ctrl、前台改变不会泄漏已接管Up。最初Forward的按压不被repeat中途接管。仅Undo/Redo随系统repeat产生业务。
- X唯一业务来源为注册热键；普通LL不重复触发X。配置generation及严格时间门退休旧消息，注册和Hook安装独立退化，真实状态文案包含冲突/注销失败/同步等待。
- 普通组合只按批注归属，Selection/ClickThrough/Settings正常编辑/模态优先；专用全局X遵循已说明的保留组合例外。旧IdtDrawpad仍为None，未恢复PPT键盘导航或旧全局面板生产器。
- owner有限收包，嵌套只触发原cancel退栈，顶层分发；输入事件和业务命令分别校验service/config/foreground/mode/page；连续X有受管链，双Delete区分未完成候选和已形成第二步，后者CtrlUp不取消，另终键/配置等仍退休。
- Bridge发布和Host/真实Controller消费同时核稳定页作用域：pageEpoch/intentEpoch/Host/PPT identity均保留，Clear内容修订不替代页面身份。具名64槽回执预留、一次消费、退休后迟到不能占用新slot；没有detached waiter或失败计数冒充成功。
- 双Delete第二步真实Controller Fence验证首Clear后置内容修订/空态，不再Clear；短租约独立于PPT/load/fatal基础contact门禁，释放仅自有门位，等待Selection应用前保持交接门。
- Config六个NoUpload严格bool、默认true、无旧设置迁移；两个重做独立开关，Clear只一开关覆盖两步；正式三语和生成资源、设置实际状态与项目登记均检查。UI卡片标题/描述用与绘制相同宽度测高。
- 旧MouseHook、PPT滚轮、Raw私有拖动、WM_TOUCH来源代码未改；ContactInput必要变更仅增加可组合的shortcut admission位。
- git diff --check使用diff.ignoreSubmodules=all通过。普通git遍历HiMsg metadata会被worktree外sandbox读取限制拒绝，属于工具环境，不修改submodule规避。

## 测试与证据边界

新增纯生产matcher测试覆盖可信全Up启动的第一次CtrlZ、启动已持键、采样期间真实Up/Down保护、注入修饰交错、配置tick/回绕；既有压力/FIFO/原子快照、Config、具名结果、lease、Controller条件/Fence和真实IdtState/业务owner探针继续由root运行。应以root最终build/test退出码及日志为交付证据。

实际Windows7、管理员前台/UIPI、国际布局/AltGr、系统Hook安装/超时/热键冲突和注销失败、Office原生导航、真实焦点/鼠标手感仍需用户人工验收。尤其启动校准在无法证明桌面/权限的前台保持unknown并显示等待，不能将纯策略PASS说成任意前台都能完成一次性物理状态校准。

官方API依据（本次复核）：
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate （高位和失败零值条件；运行期不轮询）
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openinputdesktop （输入桌面handle与访问权）

检查结论：上述可静态定位问题已修正；本报告落盘时未发现其余确定阻塞项。root最终编译与测试尚需完成；若最终运行发现失败，仍须继续定位后更新本报告。
