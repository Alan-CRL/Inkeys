# 键盘快捷键、往返与清空事务

## 1. Scope / Trigger

修改 KeyboardShortcuts 输入服务、Bar 快捷键 owner、快捷键条件画布命令、独立开关或设置说明时应用本合同。旧 IdtDrawpad.cpp 仍为未编译历史路径，不恢复 PPT 键盘翻页接管；鼠标/PPT 滚轮、私有 Raw 拖动与 WM_TOUCH 沿各自既有合同。

## 2. Signatures

- `Input/KeyboardShortcuts.h`：`Start/Stop/SetEnabled/GetStatus/PublishRoute/TryPop/GetGestureState`。
- `KeyboardShortcuts.Transport.h`：生产 Matcher、RoutePublisher、Bridge、固定 SPSC；输入回调只调用这条纯传输路径。
- `Bar.KeyboardShortcuts.h`：交互线程唯一业务 owner；`Poll(false)` 收包并促使嵌套手势退栈，`Poll(true)` 顶层安全分发。
- `ChangeStateModeForShortcutIfRevision`、`ToggleFreezeForShortcutIfRevision`：普通用户快捷键状态事务；不调用带真实 PPT 退出标识的 SelectionIfRevision。
- `Draw3.Shortcut.h`：Scope/Context、固定64个具名回执槽、租约、只读 Fence；Product/Host 条件 API 将身份传到实际 Controller。
- `config.Input.Shortcuts.{ToggleAnnotation,Undo,RedoY,RedoShiftZ,Clear,Freeze}`：六个独立 NoUpload bool，默认 true；只存 Inkeys3 main.json，不迁移/绑定旧配置。

## 3. Contracts

### 输入与归属

- 专用 jthread 同线程安装/卸载 WH_KEYBOARD_LL、注册/注销 Ctrl+Alt+X，并泵消息。系统注册与 Hook 各自报告成功/失败；一个失败不拆掉另一个，不偷偷回退抢键或模拟补键。
- X 只有 WM_HOTKEY 生成业务意图，LL 只观察链传。注册消息按配置代次和严格晚于配置切换时间验证，关闭再开启不重放旧消息。关闭请求和实际注销状态分开，注销失败不能假称系统已归还组合。
- 回调固定键位状态、一次全原子快照读取及有界提交；禁止业务锁、分配、UI/State/Host/HiMsg、配置/磁盘、格式化日志和任意业务 callback。SPSC256槽有效255；Bar mailbox有界128，有限batch消费。
- 首次 Down 固定整次按压 Forward/Owned。不能先放行后在 repeat 中途接管；Owned repeat/Up 不因松Ctrl、模式/配置/前台变化泄漏。注入事件不建立物理动作或清除物理票据；注入修饰键另作阻止精确匹配的状态。
- 精确区分左右修饰、额外修饰和 Shift+Z；Undo/Redo 可跟随系统 repeat，Delete/Q/X 每次物理按压仅一次。队列满时已确认属于批注的组合保持消费但不执行，避免撤回/删除下层文档。普通 owner 汇总饱和计数，限频日志，不在回调记录。
- 路由心跳250ms；过期/不稳时不提交新业务。只在同前台且已有可信批注归属时延续已知按压所有权；无可信身份不能用旧路由执行业务。

### 启动基线

- LL 事件没有可靠的首次Down/repeat位，不能把所有首次按压永久当未知而让初次 Ctrl+Z 落到外部应用，也不能无条件用 GetAsyncKeyState 的零值假造 Up。
- 安装 Hook 后仅在回调之外校准：证明活动输入桌面与线程桌面同名、成功取得桌面 HookControl 访问、前台 HWND/PID 在采样前后保持一致且前台完整性级别不高于本进程，再采样所需修饰/终键。只初始化尚未观察到任何边沿的条目，已按住的键保持 Forward/未知直到真实 Up。
- 校准失败保留保守状态，至多每500ms在普通服务循环重试，设置报告 synchronizationPending；不能显示成普通快捷键已就绪。自然 Up 可逐项建立可信状态。此 API 不用于运行期按键匹配，不能宣称初始化管理员前台、安全桌面或所有布局都已验证。

### 行为与优先级

| 组合 | 合同 |
| --- | --- |
| Ctrl+Alt+X | 合成式批注→Selection保留墨迹；分离式先清空当前页再Selection；Selection/已有ClickThrough→最近批注工具，无有效记忆用Pen |
| Ctrl+Z | 当前页Undo，无SuperRecovery |
| Ctrl+Y / Ctrl+Shift+Z | 同一Redo，两个独立开关 |
| Ctrl+Delete | 一击Clear保留工具，即使空页也不进入Selection；同一持续Ctrl会话内第二独立Delete进入Selection |
| Ctrl+Q | 批注且Freeze可用时切换；PPT/白板不可用不执行 |

- 普通五组合按真实 Pen/Shape/Eraser 批注状态归属，空页/栏收起不改变；Selection/ClickThrough放行。设置实际获得键盘焦点与模态输入优先，不能仅因设置后台可见禁用快捷键。
- 专用全局X在设置普通编辑仍生效；模态/退出期间暂停业务但系统组合仍保留。AltGr可能与Ctrl+Alt重叠，不能后验模拟重放；提供独立关闭及准确说明。
- 返回保留工具子型、Laser、宽色；退出只按捕获Freeze版本收尾，返回不恢复定格。全局X连续按压按owner合法顺序串行，不把同一旧快照重复解释为清空；前一失败退休其依赖切换。

### 条件命令和两击Delete

- 稳定页作用域包含Host生命周期、页epoch、工作区/PPT完整身份以及Bridge页面意图epoch；内容修订单独记录。Clear→Undo→Redo同页FIFO不应因Clear自身修订而失效，A→B→A仍拒绝旧事件。
- 条件与入队共锁验证，Controller实际消费再次核验；快捷键不能借普通 captured-scene 重放切回旧页。Accepted仅为入队，不证明Clear成功。
- 回执预约固定slot后接纳，requestId关联 Applied/NoContent/Stale/Blocked/Failed。只有有效画布实际空页可回NoContent；退休/停止不回调已销毁owner。磁盘durable完成不作为选择切换前提。
- 分离式退出租约→真实Clear回执→条件Selection；第二Delete则使用第一次Clear的具名后置内容版本，租约→Controller只读Fence确认空态→条件Selection，不再Clear或覆盖恢复点。
- 租约仅短期阻止新contact；已接纳contact须由Controller active收尾后再验证Fence。Selection已发布但绘制线程尚未应用时保留短期交接门。快捷键门禁与PPT/load/fatal基础门独立组合，释放自己的租约不能打开别人的门。
- 复合租约期间普通Clear/Undo/Redo在同一发布同步边界拒绝新入队，防止Fence确认空态之后被别的编辑恢复墨迹；先前已接纳命令保持FIFO。外部新工具/页面等意图退休旧快捷键事务。
- 第一次单Clear不长期持租约。它之后的新笔迹/Undo等内容变化必须在匹配下一次Delete之前退休候选，下一Del成为新第一击；已进入Fence后的晚到内容变化按Stale拒绝Selection。
- 两个Delete须有中间Up，Ctrl从未全部松开；无额外500ms时间窗。第二Down之前CtrlUp取消候选；两个Down已捕获后CtrlUp不撤销已形成的第二步。其它终键/额外修饰及上下文变化另作中断代次。

## 4. Validation & Error Matrix

- Hook安装/注册/注销失败、初始同步失败分别显示真实状态，不冒充成功或透传。
- 回执/输入/mailbox饱和、owner暂停、重复Start/Stop、配置切换、DWORD回绕和注入交错均须有纯生产路径覆盖。
- 清空失败、真实空页、晚到新页/工具、CtrlUp前后、同轮新笔迹与第二Delete、临时交接不可用与后继X均须生产owner回归。
- 跨C++ module global fragment的自由函数不必然随接口类型可达；实现单元调用头内函数时显式include该头，不删除IFC缓存或改工具链掩盖源码依赖。

## 5. Tests Required

- `InkeysHeadlessTests.exe --no-window`：生产Matcher/Bridge/Scope/回执/门禁、六bool严格读写/ReadMini/冻结快照/NoUpload。
- `Inkeys.exe --state-mode-production-test`：初始化前真实IdtState和同一Bar owner Dispatch/Drain，延迟后端测试真实回执策略；不创建窗口、Host或安装Hook。
- `Scripts/i18n.ps1 sync/check`、完整InkeysRepo.sln Debug|ARM64原生MSBuild、编码/BOM/CRLF及最小diff检查。
- 真正系统钩子、实际按键手感、PPT/WPS、管理员焦点/UIPI、Win7、布局/AltGr、设置三语缩放另列人工验收；纯策略或桥时延不能代替实机结论。
