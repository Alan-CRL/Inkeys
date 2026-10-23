# Research: 最小真实 Edge 浏览器适配器

- Query: 复用当前原生 product live harness 验证真实浏览器专用页面，保持真实 Mag/Tracker/Business/系统窗口 ACK，不普通启动 InkeysMain、不用 Computer Use。
- Scope: mixed
- Date: 2026-10-22

## Findings

### 当前可用程序与授权边界

只读检查：`C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe` 存在；真实用户标准 `C:/Users/alan-/AppData/Local/Google/Chrome/Application/chrome.exe` 与 `.../Microsoft/Edge/Application/msedge.exe` 均不存在。没有读取用户profile、历史、标签或浏览资料。runner的 `$env:LOCALAPPDATA` 指向sandbox影子目录，不能据它说标准用户安装缺失；上述显式路径才是本次检查范围。

当前锁屏，只准备设计，没有启动浏览器、运行GUI/输入/截图。Chrome仍是常见安装路径未发现，不等于搜索整盘证明未安装。Word/PDF不展开。

### 最小落地边界

保持已通过原生live夹具的原 Mag callbacks/线程、ProductHost、Business RequestToggle/Stop、真实 StateMonitoring、成对WindowService ACK、真实OS墨迹输入、Exit私有保存与安全层取消逻辑不变。

只替换其“底层source”适配：

- 原`SourceProc/CreateWindow/TexturePixel/source paint seq`换成新建专用Edge进程/页面与只读CDP oracle。
- 原`WindowFromPoint==f.source`换成已核实的本次browser root或其真实内容子HWND命中；目标不是Inkeys，也不是任意名为Edge的窗口。
- 原`f.taggedWheels/f.paintedY`换成专用页面 `wheelCount/scrollCount/scrollY/stable/paint.seq` 的独立观察。浏览器事件拿不到原生dwExtraInfo tag，因此不能声称网页wheel事件带本run tag；通过发送前后counter、单一专用page、时间窗口和实际hit归属关联。
- 原`ScreenSourceGt/ReadInkScreen`依赖精确TexturePixel公式，不能原样用于网页；替换为网页截图oracle/独立DOM坐标加真实屏幕差分。不是替换捕获或Tracker输入。

建议单独明确 `--scroll-follow-browser-live-test` 的有限harness场景，或现有有限入口的明确browser adapter；不得把老hidden CLI去掉invisible门当可见浏览器测试。不复制普通InkeysMain启动，不修改用户配置，不拉起普通PPT/自启动/全局hook链。

### 私有浏览器启动与单页面

专用run下新建绝对 `browser-profile`，不复制任何用户profile。启动真实Edge，传非默认 `--user-data-dir=<new private root>`、loopback调试端口、`--no-first-run`、`--no-default-browser-check`、仅本次专用页面URL。官方Microsoft支持独立profile与remote-debugging端口；不要先关闭已有Edge，利用独立profile分离。

最小首个场景可 `--app=file:///.../browser-fixture/index.html?run=<unique>`，去除地址栏固定内容并令专用主要内容占画面；仍真实Edge，但必须报告是app窗口，不冒充普通tab工具栏场景。后续普通tab验证可单独保留固定chrome区。不要使用headless模式（没有真实桌面画面/命中），不加disable-gpu、不修改deviceScaleFactor、禁用安全沙箱或改渲染参数来追求通过。

端口绑定loopback并给每run独立值；可用调试端口0与该私有profile的DevToolsActivePort发现机制，但需按实际Edge版本验证支持，不猜存在就连。CDP HTTP `/json/version`与`/json/list`只访问本次拥有的端口，选择URL精确等于专用fixture file URL+run token的唯一page target；若target不是唯一匹配，停止，不枚举/附着用户正常Edge端口。记录版本/协议/targetId/browser websocket；端点不可用/策略阻止时记真实阻塞，不修改企业策略或转而附着用户浏览器。

连接可用已有bundled Node WebSocket或.NET ClientWebSocket脚本侧oracle，不为此添加产品依赖/包安装。原生fixture与脚本oracle之间只需有限私有文件/命名pipe交换就绪、只读scroll/geometry、关闭请求；控制数据与production Tracker完全分离。不要引入通用远程浏览器控制框架。

### 所有权与 Win32 HWND

CDP Browser.getWindowForTarget返回的windowId是CDP逻辑ID，**不是Win32 HWND**。不能reinterpret_cast。

最小可信关联：

1. 父进程保留自己CreateProcess的浏览器根进程handle、PID、创建时间、实际image path、精确私有profile参数。独立profile启动应产生自己的browser owner，不应交给正常现存profile；若启动器返回后owner关系不明，停止。
2. EnumWindows只找这些已验证owned browser进程的顶层窗，标题含唯一专用fixture标识/运行token并与CDP唯一target一致。class如Chrome_WidgetWin_1仅作辅助，不是所有权证明。
3. WindowFromPoint在专用内容中心可能返回Chrome_RenderWidgetHostHWND等子窗。核GetAncestor(hit,GA_ROOT)==ownedTop、IsWindow、root/child PID归属；Chromium是多进程，child可能不同renderer PID，需真实父子谱系/本profile命令行归属。不能因child非主PID就随便接受任何Chrome子窗，也不要假设所有content HWND相同PID。
4. 前台必须是这个owned root或已核实owner树，鼠标位置、真实命中、窗口bounds与页面geometry都稳定；目标被用户别的窗口覆盖或安全层出现立即撤销本case，不强制夺回输入。

外进程HWND仅走Win32查询、普通位置/前台操作和真实SendInput；不SetWindowLongPtr替换WndProc，不hook浏览器或注入DLL，不把窗口改成自己WindowService角色。浏览器本来不在Inkeys排除表，墨迹窗口仍按原排除。

### 独立滚动真值与CSS/原图像素

现有ignored `browser-fixture/index.html` 自包含28块非周期SVG+文本，seed=0x51c0ffee、无网络/固定浮层/自动运动，暴露只读状态对象。对象有scrollX/Y、wheel/scrollCount、bounds、resize/DPR、rAF paint seq、stable；CDP Runtime.evaluate仅JSON.stringify这些字段，避免函数序列化，不调用scrollTo助手来替代真实wheel。

先等ready、visibility=visible、stable.frames>=6且duration>=150ms、实际content viewport尺寸/resize/DPR不变。发送一次真实OS wheel前后读取oracle，记录deltaMode，最终稳定后以实际scroll差为GT；不能用一次WHEEL_DELTA假设固定16/120px。边界wheelCount增加而scroll差0是合法零位移。

GT在CSS像素，Tracker输出是物理捕获像素。需要明确映射：由CDP layoutMetrics/cssVisualViewport、页面devicePixelRatio与Win32实际content bounds/稳定物理位置交叉校验当前scale；标准桌面无缩放/无pinch时通常deltaCSS×DPR，但不能只凭96/DPI重复乘除。浏览器zoom/系统DPI未校准或resize改变时停止该会话。可用固定DOM锚点getBoundingClientRect前后位移，再与实际屏幕内容移位独立校验；不向产品额外传倍率或GT改变其算法。

rAF字段表示网页JS观察到的帧，不等于DWM物理显示完成。移动后等稳定并核真实屏幕像素/锚点，不把rAF当Mag更新完成信号。CDP不改应用ROI；生产仍固定workArea。普通浏览器工具栏在ROI内是固定干扰，须据实际追踪结果报告，不能为了通过偷偷裁掉不同ROI。

### 独立画面oracle与墨迹差分

页面截图可通过CDP Page.captureScreenshot仅作为oracle/基准，绝不喂给Capture/Tracker、不生成Follow Target。最终墨迹可见性必须由原生屏幕DC截图包含真实DComp/ULW证明。若使用browser截图，对齐CSS viewport与物理屏幕内容rect、处理DPR和截图scale，并确保截图没有触发resize/scroll；截图尺寸必须实测，不假定其像素含义。

更小首次案例可在绘A前取得无墨迹真实屏幕baseline，在文档稳定/无滚动时绘A后作同rect差分定位墨迹；跟随滚动后，以独立browser-only oracle提供该时刻无墨迹底图，再与实际合成屏幕差分分离墨迹。A ROI选择页面可识别局部边缘且远离浏览器chrome/滚动条/抗锯齿文字高噪声；保存前后原始PNG与定位规则。不能把Inkeys隐藏后截底图当“全过程墨迹持续可见”证明，也不设置任何防捕获属性。

先只做可逆一小步真实wheel→稳定→墨迹位置/viewport与实际browser scroll差对比，再反向/边界/水平/长滚动。页面白色/较稀疏内容导致Tracker暂停应如实记录，本V1不自动重选ROI，不借CDP GT补位移。真实Edge通过与否是新实测项，不能由自建图案PASS推导。

### 自动关闭与残留

本run的单独profile/CDPBrowser websocket归属已验证后可Browser.close关闭本browser；或只向已验证owned HWND发WM_CLOSE并等待其根进程handle。不能taskkill /IM msedge.exe、不能关用户所有Edge，也不能照Microsoft文档示例“关闭全部实例”去终止用户会话。

Watchdog只管本次创建进程及确认过的子进程。浏览器派生进程应按父子关系+创建时间+本profile身份保留证据，不能靠常见进程名推定。退出后日志/PNG/profile留在私有run目录，先保留证据；若清理只删除已解析、核实位于run根的自建profile，必须等其进程结束，Windows操作同shell完成，不扩大目录。

夹具正常Off、真实逆窗口ACK和退出保存完成后再关闭浏览器；安全层出现时原流程立即隐藏/停止Inkeys显示，不为了CDP结束强行回桌面。浏览器失联/重启/目标导航到其它页面即失败或退休旧会话。

## External References

本次只查官方primary文档，未运行CDP：

- [Microsoft Edge DevTools Protocol](https://learn.microsoft.com/en-us/microsoft-edge/devtools/protocol/)：独立user-data-dir、调试HTTP列表/版本与target websocket。采用独立profile，避免文档普通默认实例示例要求关闭用户浏览器。
- [Chrome remote debugging switch changes](https://developer.chrome.com/blog/remote-debugging-port)：Chrome136起要求非默认user-data-dir；本方案始终新私有profile，不能用于用户默认资料。
- [CDP Browser](https://chromedevtools.github.io/devtools-protocol/tot/Browser/)：getWindowForTarget、getWindowBounds、close；windowId是协议身份，Win32映射另核。
- [CDP Page](https://chromedevtools.github.io/devtools-protocol/tot/Page/) 与 [Runtime](https://chromedevtools.github.io/devtools-protocol/tot/Runtime/)：layoutMetrics、截图oracle及evaluate。实际API以本次Edge `/json/protocol` 返回为准，tot文档不是已安装版本保证。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`：现有source、MoveOnOwnedWindow、ScreenSourceGt、ReadInkScreen、InkWheel需要适配，核心owners无需替换。
- `Build/ScrollFollow/browser-fixture/index.html`：已准备的专用长页与独立scroll/paint oracle，未执行。
- `C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe`：已确认程序文件存在，未启动。

## Related Specs

`.trellis/spec/native-desktop/scroll-follow-product.md`、`scroll-follow-capture.md`：真实底图、单一ROI、原输出/窗口ACK、像素和可见墨迹证据边界。

## Caveats / Not Found

- 当前锁屏，所有浏览器路径/协议/命中/像素均只有设计和文件检查，没有运行证据。
- 未确认Edge安装版本/策略是否允许新私有CDP、端口0、app窗口或file页面访问；不能宣称必然可运行。
- 普通tab固定工具栏、水平wheel/Shift-wheel差异、十屏特征接力、缩放拒绝、硬件笔/触控仍分别实测；app窗口单步不替代这些。
- Chrome常见用户安装路径缺失，未下载/安装或搜索其它磁盘。Word/PDF未研究。

## Chrome 注册入口补查

2026-10-22 只读通过 Microsoft.Win32.RegistryKey.OpenBaseKey 分别检查 HKCU/HKLM 的 Registry64/Registry32 四个视图，仅路径 Software/Microsoft/Windows/CurrentVersion/App Paths/chrome.exe，四项均返回不存在。没有枚举用户目录、其它应用或修改注册表。结合此前常见ProgramFiles与标准用户LocalAppData路径检查，结论仅为‘常见路径及这四个App Paths入口未发现Chrome’，不证明全机未安装。现runner可能有沙箱环境重定向，结果是本次调用所见的这些入口，未借此搜索用户资料。

当前不实施外部browser adapter，先等待原生ink与长滚动真实门槛；本报告为之后复用准备。
