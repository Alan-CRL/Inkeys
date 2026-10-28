# 最终三工作区状态机设计复审

## 范围与最终结论

本轮只审设计，不实施。已读取 PRD、design、implement、state-machine-workspace-contracts.md、state-machine-fullscreen-contracts.md，并对照 native-desktop 的 Whiteboard、Draw3、窗口/输入/恢复合同与 ppt-interop/native-session-ui3.md。

已对最新 design §3、§4B/C/D、§7 和 implement 批次1/3定向复核。初稿 D1–D3 已解决，未发现剩余设计阻断，当前设计可以交用户审核。不需要增加第四个工作区、第二套业务 epoch、线程或通用状态机框架。

这是设计审查通过，不是用户批准实施，也不表示冻结补丁已验证或产品问题已修复。用户审核前继续保持产品、测试、构建、GUI/采样及提交门禁。

## Findings (fixed)

主文由主会话修订，本复审者只更新本报告。

### 1. 准备呈现与稳定提交的等待环已解除

design:56 明确同一协调快照同时包含 Pending 和 Committed。背景/画布/PageControl 可以先按 Pending 准备并实际呈现，ACK 仅确认当前 Pending；稳定 Bar、工具外观和 Desktop 基线仍消费 Committed。

W→P 的页码呈现不等 Committed 先切换，UI-ready 本身也不能打开场景输入。准备呈现没有变成第二个工作区权威。

### 2. 有界等待与不可丢失的恢复责任已分清

design:161、164–174 明确自动尝试/等待预算不保证 Win32 可抢占或补偿必在期限内完成。恢复失败保留 Pending/owner Recovery，输入继续关闭。

安全 UI、状态显示、不相交的 Bar/PPT 几何、按 generation 锁存的 Hide/穿透、取消/新目标接纳，与普通 Show/关闭穿透/新场景侵入性政策/owner attach 分开。该能力以真实写集验证为前提，没有假设现有全 overlay 屏障已经自动支持它。

### 3. D1：请求/视图版本及 PPT 权限映射已明确

design:59–72 新增迁移规则：

- 沿用 Whiteboard 奇数、非 Whiteboard 偶数模型，工作区及白板视图语义变化退休旧请求；同语义重复请求幂等。
- Whiteboard 显示/最小化、Hide→Show 也推进请求版本，旧背景/窗口 ACK 不能确认新视图。
- 普通 PPT 页、工具、output/device 更新不因此推进工作区业务 epoch，保留会话内合法长按和原加载预算。
- Input/Scene helper 从同一协调快照派生；返回准备可发布场景，过渡中相关墨迹业务仍关闭。
- desired==committed 不能丢在途责任；物理补偿不要求已取消的旧业务 guard 再次为 true。

业务请求版本没有替代 session/slot/tool/frame/device/HWND 的底层身份。

### 4. D2：准备窗口、实际呈现、最终窗口确认的依赖已明确

design:88、95–108 将一份场景政策请求拆成有依赖的短 owner 回执：原生准备 ACK→Render/Host/PageControl 的真实 ACK→最终 owner ACK。

Window owner 返回消息循环，不持锁等待 renderer。最终几何/region/device 变化若影响输出身份，必须重取匹配帧。捕获 HWND/generation 与实际写集限制补偿，不能让新一代 role 承接旧 CanvasPolicy。

design:117 明确协调锁只保护快照，不跨 Window/Host/Render 等待；Pending fence 只 OR 到现有新 Down 准入，不阻断 ControlWake、目标准备、帧/UI ACK 或终态 drain，没有另建 Host 协议。

### 5. D3：正常、ControlOnly、Protected/fatal 与无效源已分流

design:90、113、157–174 明确四种操作结果，它们不是第四个 Workspace：

- 正常墨迹提交需匹配目标、真实 Frame/工具/必要背景/Native，PPT 另需真实 UI-ready。
- Presentation BasicControl 在缺可信墨迹 target/HWND 时，安全隐藏/穿透画布并完成必要窗口交接后，可提供原有对象定向控制；不发 InkReady/假 UI-ready，也不无限等不存在的 target。取得可信 target 后沿原链开放墨迹，不推进工作区 epoch。Unknown 仍按既有会话事实处理。
- 取消或失败只能回到仍有效且真实恢复的源；旧 PPT 已结束/换会话时不得复活旧 Presentation。
- Protected/fatal 在当前故障 token 与真实 Hide/穿透等安全 ACK 匹配后退场，不等待已停止消费者的普通工具/Frame/UI-ready，不清 fatal，也不消灭剩余 owner Recovery。

### 6. 分批中输入 fence 的缺口已消除

implement 批次1现明确必须真实接入 Host 新 Down fence，批次3只复核并补 Selection/最小化/UI收尾。不会将“目标已迁移、实际 fence 尚未接入”的中间版本作为完成实现。

## Findings (not fixed)

没有剩余需要先修改主设计的阻断项。本轮没有实施，以下属于后续实现/验证义务：

- 恢复屏障缩小必须由真实写集及原生身份核验支撑，不能只按 Workspace 枚举放行控制；仍在 USER32 调用中的 owner 不能靠等待期限抢占。
- 失败结果须对当前请求保持终态，同语义重复请求不能自动重启整套 Show/Hide；有效显式重试或新请求才发起新的尝试，原 owner 必要补偿继续负责。
- Input fence 关闭不能停止准备与完成 wake；清 Pending 前仍需真实提交或安全退休，fatal/加载/恢复门继续独立为 OR。
- BasicControl 不能恢复旧白板期间的过期 PPT 操作；正常 InkReady、pageAckEpoch、当前 session/target 和原业务资格继续严格核验。
- 真实接缝测试应串起协调快照、Window 政策、背景/UI ACK 和 Host 准入。纯 helper 不能冒充完整私有 Service/USER32 动态覆盖。
- 冻结的三份 Window/测试补丁是候选素材，未通过完整最终验证，不能直接当作新状态机已经实现。

## 其他合同核对

- 稳定业务状态仅 Desktop/Presentation/Whiteboard；最小化、焦点、Pending、错误不扩成业务状态。
- Whiteboard 背景本来取完整 rcMonitor；设计没有虚构删除已有 1px 的修复。Desktop/PPT 底行 region、NonRude 与旧 Freeze/Magnifier 尺寸保持隔离。
- 保留单 Host、原设备、固定 Mag→Freeze→DrawpadPresentation→Drawpad→Bar/PPT owner 链、唯一 APPWINDOW 及 Setting 独立线程，不 reparent/新建 runtime。
- 保留撤 fullscreen→离开白板样式→返回场景 band/穿透的离场顺序；全局 TOP/continuous、Shell COM 和 foreground 不进完成门。
- 三槽工具/文档、PPT lane/target、Load/IoError 四次预算、失败 warm/dirty、白板同进程多页及 Bar Desktop 基线/PPT Switch 均保留。
- 实际 Selection 不初始化新墨迹与过渡 fence 分开处理，保留 DiscardUntilTerminal、终态排空和非 Selection 临时擦除。
- 最小化 close 的 Hide/inactive ACK 优先于绘制暂停；恢复同页不重建文档，不清 Desktop 基线。
- 只尽力一次异步激活，不承诺完整尺寸或 Shell 标记赋予 foreground、后台必盖住任务栏。
- 范围较原三文件补丁大，但符合此次三工作区统一需求。继续限制为一份协调快照及原 owner 适配，不改 PptCOM ABI、文件格式、shader/device 或其他设置 UI。

## Verification

- 最新 D1–D3 定向复核已完成；PRD/design/implement 与两份合同研究的核心边界一致。
- 前次冻结核验：state-machine-planning-source-hashes.json 的 10 份产品/测试/资产均一致，MismatchCount=0；随后仅重读设计/计划并更新本人报告。
- 本复审者仅新增/更新本报告，没有改产品、测试、spec、规划主文或其他研究文件。
- 未编译、运行 CLI/测试、GUI、Computer Use 或进程/窗口/PDB 采样，未 commit/push。
- 设计可交用户审核；实施仍等待用户明确批准，离屏/源码结论不等于各 Windows 系统实测。
