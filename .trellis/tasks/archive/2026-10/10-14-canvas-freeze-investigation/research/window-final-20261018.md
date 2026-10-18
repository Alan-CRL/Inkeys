# Window 最终运行验证（2026-10-18）

## 初次完整运行

本轮用户已重新允许电脑操控和窗口验证。主会话串行运行完整 InkeysHeadlessTests，产物 SHA256 为 1b687d9a0689e28bf325bb73a9ebc28e95f1b49f75d6332f5b88a2c10433b152；退出 1，运行约 69.094 秒。原先列出的 11 条断言均未失败，唯一失败为新增 `topmost restore raises the complete owner tree again`。

原始输出：`Build/CanvasFreezeInvestigation/final-20261018/window-initial.{stdout.log,stderr.log,json}`。旧 11 条此次未失败不能自动证明此前三个根组的具体原因已找到；必须保留运行环境和原始证据差异。

## 剩余失败的窄边界

失败点位于：已 owned 的 Setting 以非置顶策略显示 → 恢复根置顶策略 → Setting detach 两次并成为普通窗口 → 检查九个基础 overlay 成员全部位于独立 topmost 竞争窗之上。其后的 PromotePptWindow（Bar 使用 HWND_TOP，目标 PPT 位于 Bar 正下方）与另一条完整树相对竞争窗检查均未失败。

仅凭初次日志不能确定是所有成员失去 topmost、部分成员相对顺序未上升，还是 detach/Shell 的后续调整。已在原断言前与恢复策略后增加失败才输出的 owner/style/Z 序采样，不删除、放宽或重写断言。尚未在证据不足时修改生产层级行为。

## 后续结果

第一次带失败快照的复跑日志为 `window-sampled.stderr.log`。根刷新后根位于 Z=131、TOPMOST=0，而八个基础 owned 成员位于 Z=7–14、TOPMOST=1；恢复根策略后根为 TOPMOST=1/Z=21，但 Bar 为 TOPMOST=1/Z=130，低于自己的 Drawpad owner/Z=17；Setting detach 后仍存在 Bar 顺序缺口。这些是当前 HWND 快照事实，不能以 root API 返回 true 代替后置状态验证。该次完整套件未保留完整退出元数据，不能当作最终测试结果。

新增临时、环境开关控制的 `INKEYS_WINDOW_ORDER_PROBE`：RefreshTopmost 的 root SetWindowPos 前、后和 ITaskbarList2 MarkFullscreenWindow 后分别采样根/Bar样式；测试 MagnifierHost/Bar 的 WndProc 输出 WM_WINDOWPOSCHANGING/CHANGED 的 insert-after 和 flags。失败快照另输出所有同进程非 overlay HWND 的 class/title/owner/TID/Z/style，当前 foreground PID 和各线程 HKL。调查结束后移除临时生产 probe，保留必要失败事实。

主会话的产品预检全 HWND JSON 包含 Default IME、MSCTFIME UI、Sogou_TSF_UI、SoPY_UI、HintWnd 等辅助窗，部分由 root、Drawpad、Setting 所拥有。该观察不等于测试故障已归因输入法。它使“只计九个基础成员必须完全连续”的断言存在待核实前提；Win32 只承诺 owned 在 owner 之上，不保证特定九窗之间没有合法辅助窗。官方依据：[Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)。具体插入者和是否主动改变根样式，等待受控快照和英文布局 A/B。

静态复核 F4：Setting.cpp 和 Setting.SessionState.h 相对已验收 commit 68cd297 无差异；所有 SC_CLOSE/WM_CLOSE 和业务 Hide 均经过 Setting 线程 HideSettingWindow，只在捕获仍属自身时先 ReleaseCapture 再 SW_HIDE，原生隐藏使会话失效，WM_CAPTURECHANGED 在 ImGui 锁前早返。旧未知来源产物的捕获残留预检不能作为当前源码退化证据，最终重建产品仍须同触发 GUI 复测。

新的采样构建/运行由主会话串行管理。


## 三阶段 probe 与最小修补候选

`window-probed.json`：真实 `RunWindowTests` 的显式 `--window-contract-only` 入口，产物 SHA256 `6347d4c54d119eb28a5f4bbe3fee36b71cd35dd1c74a429497553d62517a0391`，6.609秒、退出2。失败为 Bar 恢复层级，以及“隐藏 Setting 保留其他窗口捕获”。原三个根断言本轮没有失败，不能据此抹除前次红态。

本轮所有 RootOrderProbe 的 `after-swp` 与 `after-shell` 样式一致，否定了本轮 Shell 标记直接导致 demote 的假设。恢复置顶时 Bar 仍 TOPMOST=1/Z=130，Drawpad owner 为 Z=17；WindowPosProbe 显示它在根 band 转换中被放到旧的 insert-after 位置。之后生产 `PromotePptWindow` 使用 Bar HWND_TOP 后，完整树相对竞争窗的最终断言通过。由此实施最小候选：根 band 刷新后明确执行既有 Bar HWND_TOP 带内顺序步骤；只修改根的 topmost band，不在 Bar 执行独立 HWND_TOPMOST，不激活窗口，不改变白板 NOTOPMOST策略。新增直接断言确保 root policy restore 后 Bar 高于两块画布，全部旧断言保留；实际红绿结果等待集中增量编译。

辅助窗快照直接证实 root/Freeze 之间存在 root-owned Default IME；Setting/IME/TSF/Sogou链在 attached/detached 时传播 band。仅九个基础成员的连续性不是独立于这些辅助窗的 Win32保证。本轮未因这一事实改弱断言，也未给产品禁用输入法。首次 root样式红态仍须更多受控复跑。

捕获的第二条失败新增精确阶段采样，区分 HideSettingWindow 的显式自有 ReleaseCapture 与 ShowWindow 内部处理。当前未根据这一孤立失败修改捕获行为。


## Bar 修补后的运行与捕获分层

`window-order-fixed.json`：SHA256 `c388cfcd0ebff4e23a9afb4e18627fc533b37f35706a97fae9fbe93ad2d2e87f`，1.578秒、退出2。原 Bar 恢复竞争窗断言和新增 Bar 高于两画布断言均通过；剩余为包含合法辅助窗的“连续”断言、以及捕获保留断言。连续性仅因不计入 Default IME 等同 PID 的 owner 后代而误判，后续测试将按整个 owner 后代集合检查，仍拒绝同 PID 的无owner竞争窗插入，全部基础成员须保留。

捕获三阶段已直接证明：owned=true时 HideSettingWindow 入口 capture=Bar，显式自有清理后仍 capture=Bar，原生 ShowWindow(SW_HIDE) 返回后 capture=0。服务没有主动 ReleaseCapture(Bar)。官方 [ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow) 说明 SW_HIDE 会激活另一个窗口；[WM_CANCELMODE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-cancelmode) 说明 DefWindowProc 会取消捕获。新增 owner线程原生SW_HIDE同条件对照，等待运行确认，不通过强行重获Bar capture来改变系统取消语义。


## 原生捕获对照与最终测试前提

`window-native-control.json`：SHA256 `bdce4436aa64304c11ed2cd4a80dbfdc6998c51393d4d50994ce9d7333526eb5`，1.703秒、退出1。合法辅助窗判定修正及真实 owned/独立窗对照通过，Bar和原窗口11项均未失败，唯一失败保留为尚未修正的可见 Setting 捕获前提。

同条件原生 SW_HIDE（完全不调用 Window Service）输出：owned=0（未附加owner）时Bar保留捕获；owned=1（已附加owner）时Bar捕获变0。与服务Hide的结果一致。结合三阶段日志，证明当前断言将系统隐藏/激活的取消语义误当成服务越权释放，并非 F4 helper 清错捕获。

测试据证修正为两个独立合同：可见 Setting 隐藏与同条件原生 SW_HIDE 的捕获末态一致；已经隐藏的 Setting 再 Hide 必须保留 Bar 后续获得的新捕获。后者在共享输入队列中直接覆盖“不得无条件 ReleaseCapture”的回归。两种owner状态、自有捕获释放、隐藏窗口再次自获捕获、HideAll/Shutdown、接触终态原有断言保留，不强行复获或阻止系统 WM_CANCELMODE。

RootOrderProbe 与测试专用 WndProc 逐条定位诊断已移除；单独 CaptureHideProbe 暂按主会话要求保留，供最终重建产品强触发GUI一次取证后再移除。静态核对UTF8/BOM/CRLF和diff --check通过；最终无临时诊断产物及完整运行等待主会话记录。


## 最终源码清理与真实产品强触发

主会话使用同轮成功重链的产品（二进制仍含临时probe）完成了 F4 强触发：`capture-probe-hide.json` 中按住时 Setting HWND `0xef12cc` 捕获有效；Alt+F4 隐藏后、物理Up之前，Setting已不可见且 capture=0。`gui-capture-probe.stderr.log:529–531` 直接记录 owner线程40996的 helper 入口 capture=自身、显式释放后=0、SW_HIDE后=0。该证据验证生产 HideSettingWindow 确实被调用；未知来源 A60旧产物的失败不代表当前源码。最终无诊断、owner收敛后的完整GUI仍由主会话另行记录。

已删除全部 RootOrderProbe、CaptureHideProbe、INKEYS_WINDOW_ORDER_PROBE 生产定位代码及测试 WndProc逐条probe。Window.cpp 最终新增行为仅为 Setting owner附加后自身线程带内排序、根band刷新后Bar带内前置；所有topmost band调用仍只位于根。SetWindowPos失败继续返回false，shutdown拒绝新RaiseOwnedSetting，Setting owner-thread不等待overlay线程。

最终静态检查：Window.cpp / window_tests.cpp均UTF8无BOM、全CRLF；git diff --check通过。Window.cpp相对68cd297新增35/删3行；测试新增167/删11行，包含失败快照、合法owner辅助窗正反对照、Bar恢复断言、隐藏capture原生对照与有效前提回归。没有修改依赖、工程配置、输入法配置、提交或推送。

最后不含生产probe的完整Solution构建、默认全套/专项多轮、无诊断PPT/GUI交由主会话串行完成；这里不提前把尚未运行的最终产物标为通过。


## 最终重复运行发现的 attach 瞬态（继续定位）

最终无probe Solution构建exit0。相同最终Headless产物SHA256 `ff867279624523d00e713a6bf3c08c5421279823cbc1ec4204b0c9bf4c813a99`，专项首轮exit0，第二轮 `window-final-2.stderr.log` 失败为 `set setting owner` 和随后的 `owned setting sees other window capture in attached input queue`。不得以首轮通过掩盖第二轮，仍待定位。

源码确认不是有限超时或未await：Command.deadline默认nullopt；fixture调用默认INFINITE；WaitForCommandResult直接future.get()；promise在Execute返回后发布；SetWindowPos没有SWP_ASYNCWINDOWPOS。失败可在SetOwner成功后、PrepareSettingZOrder的根刷新/Setting带内排序/最终matches或最终owner读回发生；随后rollback为null会使下一条attached input queue断言级联失败。当前日志未保留这些后置字段，不能以没有命令失败文本直接宣称Win32调用全部成功。

本轮仅补临时失败后置快照 `SettingOrderFailure`，在rollback之前记录root-refresh/setting-raise/postcondition/final-owner阶段和真实owner、root/Setting band、相对画布顺序。未加sleep、盲目Flush、重试或改变断言；集中编译/多轮专项由主会话完成。


## attach 瞬态的后置证据与修补

`window-owner-probe-3.stderr.log` 捕获明确后置：phase=postcondition，Setting owner等于Drawpad，Setting TOPMOST=1且高于两画布，运行中/未退出；唯独同一根实际TOPMOST=0，而本事务策略desired=1。故不是“Setting仍normal而HWND_TOP无法入band”；改Setting insert-after Bar不能恢复normal owner本身的topmost，Win32允许normal owner拥有topmost popup。

最小修补：真实RootRefresh→SettingRaise结束后，只在同一有效根、策略未变且实际band失配时，再应用一次当前根策略；至多一次、无sleep/Flush/定时循环。最后仍执行完整owner/band/上下关系matches，失败保留rollback和现有状态节拍重试。没有改Setting锚点或对Setting独立置顶。

确定性窗口回归仅在测试WndProc中，针对实际RaiseOwnedSetting的WM_WINDOWPOSCHANGED且带SWP_NOOWNERZORDER，根owner线程通过专用测试消息执行NOTOPMOST；Setting自己保持TOPMOST，从而一次建立已观测到的失配。测试分别断言故障确实建立，以及真实SetSettingOwnedByDrawpad最终owner、根/Setting band和上下关系全部正确，而非只检查调用次数。正常attach observer计数限制为基本刷新一次加至多一次有原因补正，已正确owner再次设置仍严格不得增加刷新。

此阶段临时SettingOrderFailure/SettingOrderCorrection只用于定向取证，最终构建前全部移除，不新增产品同步stderr路径。集中编译/专项由主会话执行。


## 再次红态与根 native 调用阶段事实

一次后置补正不能视为根因修复：`window-owner-corrected-1` exit0，`window-owner-corrected-2` exit3，后者补正后的根仍TOPMOST=0、Setting=1。不继续增加重试次数。

`window-root-stages-1.stderr.log:138–160` 对 capture fixture（没有 MagnifierHost，Freeze 为 fallback 根）记录：根 0x4C00E8 在 before-root、after-root、after-shell、after-bar 全部TOPMOST=0，Bar也一直=0，而根原生SetWindowPos返回成功。第二次同策略补正相同。此例已经否定“Shell 或随后 Bar 排序把本次正确的根降回 normal”假设，失配在根API刚返回时就存在；主窗口fixture的MagnifierHost根当轮正常。

下一轮仅补精确调查：生产门控 RootOrderIdentity 记录实际owner/parent/style/WndProc与线程；测试侧仅在INKEYS_WINDOW_ORDER_PROBE启用时用subclass包装capture Freeze原本的默认WndProc，记录WM_WINDOWPOSCHANGING/CHANGED经原过程前后flags/insertAfter，在原owner设置失败后同一owner线程执行一次独立native TOPMOST对照。原失败断言保留，不替换默认过程为DefWindowProc，不追加产品重试。临时同步stderr调查逻辑仍须最终移除。


## 原生对照排除与窄候选边界

`window-root-native-1.json`：SHA256 `460233675374fa28abb68395606342119c72a4ed55758c5be12e7491b2904d55`，2.000秒、exit2。失败Freeze真实owner/parent均null，style=0x86000000，classProc就是默认过程；同owner线程直接native TOPMOST也return1/error0但root仍normal。WINDOWPOSCHANGING进入原过程前insertAfter已经是正常窗口，flags0x13（无NOZORDER），原过程不修改字段，没有CHANGED。故默认WndProc不是改写者，不能靠替换DefWindowProc规避。主fixture actualProc与classProc差异经模块路径证实是COMCTL32，与HiMsg的SetWindowSubclass一致，未归因输入法。

`window-native-controls-2.json`：SHA256 `729fb6a22d1e1c64dc38c2b32ecb9bf4cd1d050366e1e8659897fa8240af6bc6`，1.688秒、exit3。base、NOSENDCHANGING均保留root0；离屏ShowNoActivate后VISIBLE=1，再TOPMOST仍root0。因此“从未Show的隐藏layered窗口”亦不足解释。失败insertAfter实际是本树DrawpadPresentation，normal/hidden，owner=root，并等于root当时的previous。

`window-native-matrix-2.json`：SHA256 `219d1317846db7b7105f708e27401f60f67926b436207ff7483528d90e7f7538`，2.344秒、exit2。base与NOSENDCHANGING再次root0；第一次NOOWNERZORDER使root0→1，但后续Defer(root)/Defer(root+Bar TOP)是在已变更状态上运行，不能单独归功HDWP。组合的Bar读回仍0，提示只恢复root不足以证明完整owner树正确。BOTTOM→TOPMOST也使root1，但不会将有可见副作用的bounce引入产品。

按主会话同意，撤回未解决自然故障的“一次后置补正”及人工降根回归（正常attach observer重新严格要求一次）。临时窄候选仅在RootRefresh原生调用增加NOOWNERZORDER；新增严格全链回归：主九窗/无Magnifier的fallback四窗及owned Setting，每节点band必须等于策略，owner在数组内时child必须处于owner之上。四阶段快照覆盖每个基础role的真实owner/previous/band；门控native原参数→新flag→恢复NOTOPMOST→原参数对照另记录全部owner后代含aux。候选未因root单节点变1而标为通过，等待集中build和真实严格回归。

官方SetWindowPos文档说明根成为topmost时owned也应进入topmost、normal owner允许拥有topmost popup：[SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)。历史Microsoft Support另说明NOOWNERZORDER可能影响owned在NOTOPMOST中的传播，但该文仅适用于Win7/Server2008R2，不能据此直接归因Win11：[历史Z序问题](https://support.microsoft.com/en-us/topic/a-non-topmost-window-can-be-displayed-above-a-window-that-has-the-ws-ex-topmost-extended-window-style-in-windows-7-or-in-windows-server-2008-r2-6a464f1a-ec37-5442-54e6-3a39950d9913)。


## 单根NOOWNER候选的严格否证与owner顺序候选A

`window-noowner-candidate-1.json`：SHA256 `435495a586ee7dbf502f57b779e349fc0aa75b2041eca9e3f5ab1a744ddf6fc3`，2.125秒、exit13。新增strict断言直接抓出退化：after-root为root1、Presentation/Drawpad/四PPT/Bar/Setting0；末端Bar TOP未恢复完整band。仅根NOOWNER改变实际owner组传播，不能保留为最终方案。已撤回。

native对照还表明“样式全部为1”并不保证Z正确：后续原根调用可使owned样式1，却仍在Z121–124而root在Z8；恢复normal再原根TOPMOST也出现root Z121、owned Z122–124（child在owner下方）。因此验证扩展为每个基础成员band和owner相对顺序同时成立，不能以API true或单根style1宣称成功。

`window-owner-sequences-{1,2,3}.json`：同SHA256 `381164a4f3428cfad8078b81cb3f84145def5276e2a38416915449a0e3a02204`，前两轮exit0，第三轮2.078秒exit3并捕获原root0/Setting1后置。第三轮故障后的native A（normal→owner先后带内HWND_TOP|NOOWNER→原根TOPMOST）使8基础成员及root-owned IME全为style1，Bar Z8→Drawpad13→Presentation14→root15，IME Z7；B（先原根、再带内排序，根失配才补原根一次）也最终正确。C（NOOWNER先根校正、再原传播、再排序）则IME落在root下方，不选。

上述native控制最初发生在public失败rollback后，Setting已owner=null，不能把它单独当作完整跨线程attached Setting证明。后续失败诊断可在Setting自身线程临时附回Drawpad跑A/B，并在结束恢复原owner；真实产品候选不依赖该诊断。

主会话批准最小候选A：RootRefresh在原根band提交一次之前，对基础overlay按owner先后执行一次HWND_TOP|NOOWNERZORDER；缺失成员/根跳过，所有调用在overlay owner线程，失败返回false；无normal reset、无根NOOWNER、无追加根补正/循环。Setting仍在自身线程按既有RaiseOwnedSetting排序。四PPT保留当前相对Z序，Bar保持最上方；新增真实Show BottomLeft→RootRefresh后仍直接位于Bar下方且全链band/owner顺序正确的回归。原根之后已证Bar补齐步骤暂保留。

所有原断言及新增全链band/owner严格断言保留，capture fallback fixture扩展为真实完整四PPT+Bar基础链。产品/测试源码现按主会话要求冻结，待集中构建及12轮真实专项；本报告不提前宣称候选通过。临时生产stderr与native诊断在证据确定后统一清理并再次最终构建。


## 真实附加Setting后否证A，与有限根band校正C

`window-owner-before-band-{1,2,3}.json`：同SHA256 `a2a282a17a16adad416d349d80dc77a2f9bc6d5e20c1d857adbbf8b783a17ffd`，前两轮exit0，第三1.469秒exit3，仍是capture原SetOwner/全链band/inputqueue级联失败。after-root仍root0/Bar0，故无normal reset的前置排序A没有解决自然故障。已撤回A。

该轮失败控制已真正把Setting在自身线程附回Drawpad。A最终所有核心style1但整组Z112–121；B核心Z7–14、root Z120，中间仍有无关窗口缺口；C最终Setting7、Bar8、PPT9–12、Drawpad13、Presentation14、root16（MSCTF15是Setting后代），基础core全为style1、owner相对顺序和连续性均满足。C中部分root/Setting-owned IME/TSF窗口仍留在Z121之后，不能把基础核心修复宣称为所有系统辅助窗的任意排序保证。产品不会按厂商class枚举或操控系统aux。

按主会话决定，生产选有限C：原根band调用一次；仅当真实rootband不符时，NOOWNER只校正根band并严格读回，再用原flags传播一次；之后完整基础owner先后HWND_TOP|NOOWNERZORDER（PPT保留本次事务开始前相对Z序，Bar最后）。根最终IsWindow及实际band必须满足策略，否则返回false并走既有错误报告。没有normal reset、次数循环、Prepare事后重试、逐节点TOPMOST、Setting锚点变化或aux操作。原Bar后置步骤并入完整基础顺序。

白板Enter/Leave后各补一次Refresh+全基础band/owner相对顺序严格断言，覆盖desiredTopmost=false/true两向。失败时另有纯测试D（让Win32随基础owner移动aux）、E（同PID全部owner后代深度排序）观察，但不会把这些辅助窗动作加入产品。当前C源码冻结待集中build+12轮真实专项及最终GUI，尚未提前标记通过。


## C首轮与普通/白板策略边界

`window-finite-reconcile-1.json`：SHA256 `5c9f8421711b375e202ab3088d5c978e166cd6c54c1a8ce97b57c88f666d4f63`，2.360秒exit1。原11条、完整fallback与Setting捕获全部通过；唯一失败是本轮新增whiteboard root refresh全链band/owner严格断言，不能标全绿。

现有分段已足够定位，不追加计时或重试：whiteboard desired=false的after-root、after-shell均全基础band0且owner顺序正确；新增after-order使Presentation位于Bar之前、Drawpad低于Presentation owner，根仍band0。退化由本轮完整NOOWNER带内排序引入普通可激活组，非NOTOPMOST传播失败。

最小更正将策略分开：keepTopmost=true维持C有限根band校正及完整基础后排序；keepTopmost=false沿用原根传播后仅Bar HWND_TOP原flags（允许原生owner联动），不新增普通组NOOWNER排序。白板样式与生命周期不变，新增Enter/Leave全链严格断言保留。源码再次冻结，待集中build和重复专项。


## C的传播遗漏后置与同根锚点继承候选F

`window-whiteboard-policy-2.json`：SHA256 `21b655143c84c12b5c21b03ad022b2035d88d38c4dc3eeaefed218f99c821a34`，1.766秒exit1。普通/白板策略更正已使白板断言通过，唯一失败是fallback完整band。SetOwner自身成功且Setting/root均1，但七个基础owned成员仍0。

该次after-root为root0/Setting1、其余基础0；after-correction根1；after-propagation再用原flags后仍只有根和Setting1，其余全0；后置HWND_TOP也不能让normal成员跨band。因此不能以“调用传播一次”代替每节点真实读回。D/E是在reset后的状态上验证，不能替代该自然失配的同条件证明。D（原生owner联动）能使基础正确但Setting的IME仍低于Setting；E（诊断全后代深度排序）可将所有记录到的owner后代排序正确，但产品不采用厂商/aux枚举。

主会话批准F窄候选：完整基础后排序中，只有实际band不符的成员先以同一有效root为真实insert-after锚点（NOOWNER/NOACTIVATE）继承root实际band；API失败或读回仍不符立即返回false/上报，不能假定继承成功；随后既有HWND_TOP恢复owned在owner以上的Z。根仍是唯一显式TOPMOST/NOTOPMOST节点，没有normal bounce或次数循环。Setting同样在自身Raise命令中，仅实际band不符时按有效且符合当前策略的root继承，再TOP；原完整matches与rollback保留。普通/白板原Bar路径不变，系统aux不动。

失败native F放在所有reset控制之前，从自然失配及真实owned Setting开始逐节点记录锚点继承/恢复Z的结果和全链读回。源码现冻结，等待集中构建及12轮严格专项。另有直接相关审查待下一轮统一处理：新增PPT顺序捕获不能用无界GetTopWindow→GetWindow遍历，改为EnumWindows+固定4句柄数组、收齐提前退出、无分配；既有SettingAboveDrawpads不扩展重构。


## F十二轮全绿与最终无probe源码

主会话集中Solution构建exit0。`window-root-inheritance-1..12.json` 同SHA256 `807771268f6d2599b6c7ae37e999a56e7e53e8be1157aeb94b752c90b88791d7`，12轮真实window contract全部exit0，单轮1.453–2.547秒。原窗口11条、完整无Magnifier fallback+owned Setting、全基础band/owner顺序、普通/白板双向、PPT活动兄弟顺序、捕获及原生SW_HIDE对照均通过。该构建仍有临时probe，原始源码/EXE证据由主会话保存到Build；不能用此SHA冒充最终无probe产物。

按主会话要求，统一删除全部生产SettingOrderFailure/RootOrderStages/Identity/ProcedureModule及测试native A–F、Freeze subclass、原始WndProc属性和相关消息；只保留有目的的失败事实快照、原生SW_HIDE正对照和全部严格回归。产品没有新增同步stderr定位路径，没有环境变量探针或辅助窗操作。既有统一ReportCommandResult错误报告保留。

新PPT相对顺序捕获已改为EnumWindows+固定4 targets/ordered数组，无分配，收齐提前退出；只在topmost策略确有PPT时枚举。漏掉配置句柄时返回false并走既有错误报告，不静默省略成员；普通/白板沿用既有Bar路径。没有扩展重构既有SettingAboveDrawpads。

最终两个源码相对当前HEAD：Window.cpp新增147/删除3行，window_tests.cpp新增206/删除15行；均UTF8无BOM，全CRLF，bareLF/bareCR为0，git diff --check通过。Window.cpp新增集中于Setting自身owner线程Raise及有限根策略/实际band继承/owner后排序；测试保留原全部断言并增加完整band/owner、fallback全PPT、白板双向和PPT顺序回归，以及有实测依据的owner辅助窗/捕获原生语义对照。

源码现再次冻结；最终无probe完整Solution构建、同产物12轮专项/默认全套/最终GUI及PPT smoke由主会话串行执行，结果须另记，尚未在本报告提前宣称最终验收通过。无commit/push。


对F十二轮原始phase日志按单次事务、desired策略和真实root分组复核：轮1/4/6共3次自然after-root band失配；这三次after-propagation仍分别有7个基础节点为normal，after-order全部21/21节点已进入topmost，完整严格断言通过。其余9轮未触发该实际修复分支。因此12轮全绿包含真实故障状态被选定修复路径收束，并非仅未复现故障。此统计剔除了desired=false的正常normal样本、MagnifierChild、Setting和DisplayObserver，不把普通策略样式0误计为传播遗漏。


## 最终真实GUI发现的可见Setting排序回归

主会话用最终无probe产品（EXE SHA前缀51fe2，PID41604）发现本轮新排序回归：Settings visible HWND0x631784，owner=Drawpad0x890d44，但实际Z序为Bar/PPT→Drawpad0x890d44→Presentation0xbe025e→Freeze0x19165a→Setting0x631784→root0x29e10b0。Setting虽可见，却被透明Drawpad覆盖，RightDown不能取得Setting捕获。证据截图 `Build/CanvasFreezeInvestigation/final-20261018/clean-gui-failure.png`。不能因无probe十二轮及命令行测试通过便宣称最终GUI验收通过。

现有回归只验证Setting attach/Show/Reshow即时顺序，没有验证可见Setting之后的周期RootRefresh。本轮基础后排序的HWND_TOP|NOOWNERZORDER会把Drawpad推进其owned Setting之前，因此回归由本轮新增排序引入；band继承和一般topmost策略成功并不足以保证持续可操作。

先补严格回归：真实owned Setting Show/Reshow后再RequestTopmostRefresh，必须仍visible、owner仍Drawpad、完整十窗band/owner顺序有效，Setting高于两画布；失败保留owner事实快照。窄候选D只移除Most基础后置HWND_TOP的NOOWNER，使Win32在原生owner联动中随Drawpad带起Setting；同根band继承步仍保留NOOWNER，不增加Overlay向Setting的显式跨线程等待，Setting自身Raise线程/锚点、普通白板策略、系统aux均不改。

相对上一版产品仅更改该flag和中文原因注释，测试新增10行。当前总源码diff为Window.cpp +147/-3、window_tests.cpp +216/-15，仍UTF8无BOM/全CRLF，diff --check通过。源码已冻结交主会话集中构建，待新回归、原全部严格窗口/捕获、默认全套及真实GUI/PPT再次验证；本节不提前宣称通过。


## 最终清理构建与GUI验收

最终solution `final-20261018-owned-setting` exit0；Headless SHA256 `8f68e6e65ff1c8e6e46d07b61d8c51acb9a2b7ea029d192c657f1d99be4961ae`，新增可见Setting周期刷新与原全部窗口专项连续12轮exit0，默认完整suite exit0/82.547秒。产品SHA256 `b49504f9995638f8c0dfae07acb7917db36f2f36bed1d401731b674822f14318`的真实GUI额外保持设置打开3.5秒：设置实际高于Drawpad/Presentation，自身RightDown捕获成功；隐藏后Up前capture0、继续新线/擦除/持笔PPT翻页/Explorer工具往返均通过，真实按住关闭软件exit0/1.688秒。首轮失败截图备份名clean-gui-failure-first-regression.png，历史51FE失败未删除。最终独立源码复核未发现阻断或弱化，统一证据见final-validation-20261018.md。无commit/push。
