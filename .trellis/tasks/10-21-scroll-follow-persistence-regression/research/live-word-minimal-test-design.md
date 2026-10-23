# Research: Word 最小私有实屏验证设计

- Query: 用已安装 Word 做下一项空画布实际滚动验证；研究独立启动、避免用户文档/Normal模板、精确窗口归属、只读滚动GT与现有 external IPC 的最小复用。
- Scope: mixed（仅源码/已有报告/官方文档；没有创建文档、启动进程或COM、执行CLI/GUI/输入/截图）
- Date: 2026-10-23

## Findings

### 可用性和最小首例

主会话已有 `Build/ScrollFollow/word-availability-1023.json`：`C:/Program Files/Microsoft Office/root/Office16/WINWORD.EXE` 存在，文件版本 `16.0.20430.20182`，HKLM App Paths 的32/64位视图均指向该文件。这只证明安装入口，不能证明已激活、首次运行已完成或 NativeOM 可用。

首例限定为**新私有宏自由DOCX + 独占新Word进程 + 一次真实OS滚轮 + 空画布Tracking/实际owner/ACK/Off**。不进入用户文档，不声称Word墨迹可见/保存/长程/硬件笔。文件生成和实际执行由主会话后续单独实施。

### 官方启动参数：不能照搬 Excel /x

[Microsoft Office 命令行参数文档](https://support.microsoft.com/en-us/office/lifecycle/command-line-switches-for-microsoft-office-products) 给出的 Word 语义：`/a` 阻止自动加载全局模板（含Normal）/addins并锁定settings文件；`/n` 新实例且不打开文档；`/w` 新实例且建空文档；Word `/x` 仅限制响应一个DDE请求，不是Excel `/x`的“独立进程”合同。同页说明多switch组合不受支持。因此不把 `/x /a /q` 或 `/n /a` 组合作为隔离保证。

本轮最小可行路线：**启动前只读确认 WINWORD 进程数为0**；若有任何现存Word就停止，不关闭或附着它。直接启动已核路径，单 `/a`，附唯一私有docx路径作为文件操作数；保存精确CreateProcess/Process handle、PID/FILETIME/image/命令参数。在点击/滚轮前必须证实新PID仍活着、窗口属于它、NativeOM返回的文档FullName正是私有路径、Documents集合无其他文档。若启动器退出、Word转交其它PID、出现并发未知Word实例或文档不符，停止，不按进程名继续找。

**文档证据的限制：**官方表明确支持 `/a`，但本次未找到该版本 `/a "file.docx"` 的官方组合示例；文件操作数不是第二个switch，不过它的实际打开行为仍须本机准备步骤验证。不能仅凭启动返回值声称已隔离。若不正确加载，记录阻塞；不自动退到多switch或附着用户实例。另一条只用 `/n` 的路线虽有新实例文档保证，却没有 `/a` 的Normal/addins屏蔽，本首例不自动采用。

`/a` 不是独立用户profile。不能承诺Office的MRU、许可、遥测、缓存或其它偶发文件零写入，也不能重定向整个APPDATA/修改注册表来制造“可携带Word”。不改Normal.dotm、不关闭其保存提示、不保存全局Options、打印机、TrustCenter、宏策略或用户默认视图。私有TEMP/TMP只隔离本次临时文件，不代表Office全部状态隔离。

若需要更严格控制“打开文档时不入最近列表”，官方 [Documents.Open](https://learn.microsoft.com/en-us/office/vba/api/word.documents.open) 有 `ReadOnly=True, AddToRecentFiles=False`；但这属于**准备阶段修改本测试Word实例**的操作，且先要安全取得该实例对象。本首例倾向普通命令行直接打开，允许记录最近列表可能被Office自身更新，不把AddToRecentFiles=False伪称命令行能力。本研究没有调用Open或任何COM。

### 私有文档合同（后续创建，不在本研究生成）

- 全新普通目录 `Build/ScrollFollow/word-<nonce>/`；路径/祖先无reparse，docx唯一文件名含nonce。用现成文件工具生成宏自由OOXML，记录SHA256；不依赖Word生成或读取用户模板。
- ZIP不得有VBA、ActiveX、OLE、外部关系、链接更新、外部attachedTemplate、远程图片、字段自动更新或用户姓名/文档数据。正文为自建非周期文字与简单图表/本地内嵌图片，数页以上可滚动；明确段落样式/字号/页尺寸，避免依赖Normal格式。不要用全屏随机纹理把Word验收等同于native图案。
- 放置3–5个具有唯一文字或书签的短非折叠Range，初始位于正文可视中段；一小步后仍完整可见。正文滚动区域/窗口实际bounds/zoom保持稳定。必要时在自建图文中加入可识别局部画面标记，用屏幕小块证明专用文档在显示，不能只相信窗口caption。
- 可对私有测试文件设只读属性，原始hash在退出后核对；这不等于打开时没有Office临时owner文件，也不等于Office全局设置无写入。

### 只附着精确自建 HWND，禁止全局ActiveObject

已有Microsoft官方 [AccessibleObjectFromWindow](https://learn.microsoft.com/en-us/windows/win32/api/oleacc/nf-oleacc-accessibleobjectfromwindow) 明确列出 Word 最内层 `_WwG` 文档窗口，在 `OBJID_NATIVEOM + IID_IDispatch` 时返回 **Window对象**。可在私有runner的辅助进程内，仅针对已验证新PID的owned顶层窗及其 `_WwG` 子窗调用；不使用 `GetActiveObject("Word.Application")`、ROT遍历或 `new Word.Application` 来猜实例。

顺序：

1. 先持有精确新PID/FILETIME/image；EnumWindows仅接受该PID的唯一目标文档root（常见`OpusApp`仅作辅助）；校验content child PID/GA_ROOT/实际client屏幕bounds及可见性。
2. 对这个 `_WwG` 调NativeOM，取得Window；读其 [Hwnd](https://learn.microsoft.com/en-us/office/vba/api/word.window.hwnd)，将INT32 HWND位模式正确无符号拓展后与Win32持有窗口/根祖先对应，不把负数直接当非法句柄。再核Document.FullName、唯一文档、nonce/预定Range文字、应用窗口身份。失败/歧义即停止；不通过全局ActiveWindow补救。
3. 每次采样前后重新核进程/窗口/文档及不变几何；Word繁忙、对话框、文档被关闭或HWND变动不能当GT零位移。

NativeOM可能同步阻塞或被Office拒绝调用；在拥有COM公寓的独立runner观察helper中做有界调用/进程级监督，不能让Inkeys UI或其原Mag线程等待COM。超时只结束自己helper/撤销此case，不强杀或接管任何未知Word。真实Word调用迟到、RPC_E_CALL_REJECTED和初始化尚未完成须区分；不无限重试。

### 独立滚动真值：固定正文 Range 的屏幕坐标

推荐用官方 [Word.Window.GetPoint](https://learn.microsoft.com/en-us/office/vba/api/word.window.getpoint) 对**同一批固定短Range**返回屏幕矩形。该方法给screen pixel坐标，Range不完全可见会报错。无需选择Range或改变Selection、View/scroll；不要用VerticalPercentScrolled、页号或滚轮delta推算像素GT。

- before记录至少3个相距足够远、完整可见的固定Range（起止字符/书签、文字hash）及left/top/width/height；稳定后才允许真实OS wheel。
- after仍读相同Range。各anchor的屏幕位移须在既定小像素误差内同向相符、尺寸不变；至少3次采样、总时长≥150ms无进一步移动。页面重排、zoom变化、anchor离屏/报错则此例失败，不换新anchor重设原点。
- 定义内容运动 `dContent = afterPosition - beforePosition`；现有owner期望 `viewport = base - dContent`。若复用external正scroll差语义，传 `dScroll = beforePosition - afterPosition`，则 `latest=-dScroll`、`viewport=base+dScroll`。明确命名避免双重负号。
- GetPoint的“screen pixels”仍须与当前runner DPI上下文/原生屏幕坐标实际对齐；先用已知ownedcontent边界/实屏标记验证，不能再次无条件乘DPR或用Word zoom百分比换算。若多个anchor不一致、物理坐标映射不成立，停止，不回填Tracker。
- 真滚轮仅由runner在实际foreground root、WindowFromPoint命中该正文content、左键未按、安全层正常后SendInput一次；COM不得调用SmallScroll/LargeScroll/ScrollIntoView/Selection移动来代替输入。Word没有网页wheelCount/scrollCount，记录真实SendInput返回和anchor移动；不能伪造浏览器事件计数。

该真值独立于Tracker和Mag，能证明所选正文anchor的真实布局坐标移动；不证明Mag某个具体帧逐像素正确，也不能证明所有固定工具栏和页边区域都是纯平移。Word大面积空白/固定Ribbon可能触发Coverage/AmbiguousMotion/Paused，应如实记录，不为这个应用改ROI/门或自动恢复。

### 现有external入口可复用与真实差异

本次源码 `Draw3.ProductLiveTest.inl` 已有 external-empty，不能不加区分直接复用其Edge schema：

- `ExternalState` :25–35、`CheckExternalIdentity` :237–245 固定 `edgePid/edgeImage` 等字段；`:314–315` 明确只允许basename `msedge.exe`。
- `CheckExternalObservation` :260–272 固定browser fixtureId、URL/run、visibility、rAF稳定帧、CSS dpr/innerWidth；`:1090–1121` 还要求web wheelCount/scrollCount并由CSS×dpr求GT。这些都不是Word事实，不能把Word伪装成Edge/编造dpr=1与计数。
- 可原样复用：有界阶段文件/反重放run+PID+FILETIME+session、私有Inkeys earlyCLI/存储root、真实共享原Mag、Business toggle/stop、Channel只Read、ACK/空页pair(0,0)与Off(1,0)、最后可信目标屏障和正常清理。
- 最小必要修改是**明确Word有限kind/严格入口**及Word专属identity/observation验证，保留既有Edge kind合同；不把Edge allowlist泛化为任意可执行文件，不添加产品业务API。可对公共envelope小范围抽取sourcePid/created/image映射，但不要为单Word引入通用多应用插件架构。
- 私有 `run-live-word.ps1` + 有界NativeOM helper负责Word setup/GT/OS input/关闭；C++只读最终物理delta与稳定/anchor证据、查实际owner。现有 `browser-ready / tracking-ready / wheel-result / off-ready / final` 的五阶段节奏可复用，Word数据文件命名/版本/kind要诚实明确，不将CSS观测合同悄悄放宽。
- Inkeys的内存默认配置、私有AutoSave/TEMP、严格单参early exit、唯一DiagnosticOutput::Session不变。无需给Inkeys硬导入oleacc/Office.Interop；相关调用仅测试runner进程，也不需要改PptCOM。

### 自动关闭、实际阻塞和报告范围

正常先等Inkeys真实Off/结束屏障/退出，再关闭只属于本新PID且FullName匹配私有文档的Word窗口；通过该精确NativeOM Window.Document.Close(wdDoNotSaveChanges)及其Application.Quit(wdDoNotSaveChanges)或owned root WM_CLOSE都只能作用本例。使用前再次确认没有额外文档；不按caption或WINWORD进程名批量关闭。应用出现任何Normal保存/恢复/登录/激活/隐私/修复/默认格式提示，不自动点确认或修改Options；记具体本机首次运行阻塞并停止输入。普通WM_CLOSE若出现modal且不结束，不假装cleanup通过；保留证据，必要时交用户处理。不能因测试超时关闭用户并发打开的其它文档。

通过标准：一小步非零正文anchor位移；共享Mag/Tracker实际接受且绝对owner/viewport与独立物理GT相符；正常Off精确ACK和final target无遗漏；全部owned进程/句柄按计划释放。未通过则区分准备/许可证/原生对象模型/追踪/结束清理，不能由native或Edge通过替代Word实测。

## Files found

- `Build/ScrollFollow/word-availability-1023.json`：现有安装入口/版本/注册记录，非运行证明。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`：当前真实external-empty及Edge专属校验。
- `Build/ScrollFollow/run-live-external.ps1`、`external-native.cs`：现有私有阶段/进程/Win32安全检查，Word只复用公共合同。
- `research/live-external-minimal-entry.md`、`live-browser-adapter-design.md`：原external设计及空画布证据边界。

## Related specs

`.trellis/spec/native-desktop/scroll-follow-product.md`、`scroll-follow-capture.md`、`scroll-follow-tracking.md`：实际显示/底图证据、原Mag所有权、真实ACK、GT不能驱动Tracker和暂停/ROI不为应用放宽。

## Caveats / Not Found

- 未创建DOCX、未运行Word/COM/CLI/GUI/输入/截图；所有步骤是后续最小验证设计。
- `/a` 的单switch官方语义与附私有文件实际行为、独立PID/窗口归属是不同层证据；后两者必须实测。
- Office incidental profile/MRU写入没有private profile隔离保证；不修改用户设置、注册表或Normal模板来绕过。
- `_WwG`/NativeOM/GetPoint在本安装版本是否可用、初始Range是否完整可见、DPI映射和激活状态尚未验证。
- 不修改代码/build/test/config/spec，不启动任何进程或调用COM，不执行Git。其他代理修改保持原状。
