# 实际 Tracker 输入有限录制实施（待独立审查与实际运行）

日期：2026-10-23。本次仅增加显式 external-empty harness 诊断，不调整 Tracker、Mag、ROI、目标、门槛、wheel 或正常输入/保存链。root 独占构建与GUI；实现者没有执行构建、GUI、输入或截图。

## 当前实际问题

原 `external-46cc7129b8cd4621b8a47d1d8842a840` 在真实200px滚动后，frames9–11空间一致性比 .611565 小于 .65，3拒绝后暂停。点数比134/178与存活门通过，尚未进入后续覆盖和锚点检查。以前750ddd…一次140+60采样成功仍仅为该次限定PASS。新增录制只收集证据，不宣称修复。

## 最小接线与寿命

- runner新增 `-RecordMotionFrames`，与SetupDiagnostic互斥；给严格external child设置 `INKEYS_SCROLL_RECORD_MOTION_FRAMES=<本次32位nonce>`，未选时移除继承标记。C++在创建任何资源前拒绝坏标记/非external组合，再核nonce与已有私有runner身份一致。
- 宏内私有 `LiveTrace::Observer` 通过atomic shared_ptr安装。原Frame真实分支明确记录 Initialize / Process / AwaitTracking，真实Tracker、Publish、Paused和日志顺序不改；最后仅同步只读观察const input/result。AwaitTracking保存默认NotInitialized结果，离线不能送Process。
- collector在RequestToggle/F0之前安装；从seq1连续录制，gap、会话改变、超限、转换/保存异常明确失败。独立复制原句柄/身份/显示数据，不保留Fixture指针；迟到shared引用只能访问collector自有资源。
- 回调仅有界内存复制和同版OpenCV COLOR_BGRA2GRAY，保留完整分辨率Gray8，未提前resize。最多64帧，像素vector总计≤384MiB；不压缩/写盘，不新增生产FIFO。复制耗时包含安全核验与转换，只作诊断开销，不代表正常捕获FPS。
- 原Stop/State/Mag/WindowService join完成并核drain后，卸载observer；在DisplayShutdown前以专用只读门保存CreateNew PGM/JSON。保存门不读取offSignal、不复用会因正常shutdown失败的CheckFixture，不重置原停止信号。未完成录制使显式诊断结果失败；productExitBeforeTraceSave独立保留原结果。

## 已知区域与隔离

每次复制前后及保存前核实际安全桌面/已知安全层、持有的Edge/runner进程存活、Edge精确FILETIME/image、原main/content HWND/物理内容几何、原显示generation/DPI/方向/尺寸、主窗window及client完整包含ROI。只读Guest凭据为本次自建main有界标题同时包含nonce及本机实际后缀 ` - [Guest]`；缺失/本地化明确unsupported，不伪造Guest属性或读取profile。

ROI的5×5命中只允许精确owned Edge main后代；复制阶段额外允许本次精确Drawpad/DrawpadPresentation HWND且核其已在原Mag排除名单，不接受其它Inkeys窗口。安全/身份/区域门失败会禁止全部图像保存并给incomplete原因，不保存未知屏幕截图。元数据仅含本次数据/统计/原因，不含窗口标题或账户信息。

runner另有已批准的独立两行 `--disable-sync` 请求及日志，保留Guest/private-profile；不改策略/偏好/注册表或账户。它只证明本次进程请求该flag，不保证Edge/MSA或OS全局同步零副作用。

## 固定数据格式（schema1）

输出在已有product-live私有root：`motion-trace.json`与`motion-1.pgm`等。PGM为P5、完整ROI宽高、255、tight Gray8。JSON precision17、≤512KiB；全部文件CREATE_NEW且拒输出root祖先reparse。

顶层：kind=`actual-tracker-input-fullres-gray8`、run、complete/failure、recordedCount(integer)、observedCount/skippedInputs(decimal string)、frameLimit64、pixelByteLimit/pixelBytes(decimal string)、originalThreadsDrained、productExitBeforeTraceSave、frames。complete表示原观察前缀完整并可回放，不代表原产品case成功。

每帧：sequence/session/captureSession/displayGeneration十进制字符串，timestampSeconds，operation，原sourceRect/workArea四边对象（left/top/right/bottom均int）、sourceWidth/sourceHeight（实际BGRA buffer尺寸）、roiLeft/roiTop（桌面绝对坐标）、width/height（实际MotionImageView ROI尺寸）、sourcePixelsPerPixelX/Y。sourceRect.right来自原Capture几何，不由buffer宽推断，保留现有right=width-1等差异。首版审查指出原buffer高为primary高减1；已仅修录制器，调用原IsValidGeometry验证完整geometry，再按实际buffer重算clamp/stride/span并输出原identity尺寸（本机1919），未改Mag。

originalStrideBytes为完整BGRA行stride；originalSizeBytes为原MotionImageView从ROI起点开始的可用字节（full buffer减offset），两者均字符串；originalFormat=Bgra32、recordedFormat=Gray8-full-resolution、captureValid、observerCopyMilliseconds。结果accepted/state/reason/deltaX/Y/totalX/Y/confidence及所有原MotionStatistics字段完整复制，64位统计identity为字符串，其余int/double；file/fileComplete用于逐文件校验。AwaitTracking的input.sequence真实且可非零，默认result.statistics.inputSequence可0。

## 静态收据与边界

PowerShell AST通过；未执行C# native方法。保留五个原文件BOM/CRLF/EOF，核心Tracker源码仍BF3409F88F606C82B31AE4BEB30B592C105E945028C115FF06109EDB843B1978。原native从`void CheckDesktop(`至`static int RunScrollFollowProductLiveCase`前一CRLF之前的片段34890字节逐字节不变（72907FD9BCC58EEEE8B18EA75EC967AF985D18AFB8AEFFC2A409038CEEA29E69）。精确delta与格式收据见ignored `Build/ScrollFollow/external-entry-stage/frame-trace-v1.patch` / `frame-trace-v1.manifest.json`，隔离flag delta为disable-sync.patch/json。

尚待checker审查、root完整solution构建/最小CLI及唯一实际录制；当前不存在本轮实际PGM或回放通过证据。actualROI图像并不因固定marker就证明每像素正确，后续由独立离线原Tracker回放与200px像素对应关系判断。默认native用例没有启用collector，原生完整long和旧外部结果不能替代本轮未运行项。

## 文件冻结

- `Inkeys/Inkeys/Business/ScrollFollow.cpp`：`67E1844BFD4647475D99371720EFA19830474CEEBB28F68F9A4EA81A6AED6459` → `12DDD33184786E77703F06CC4C01914BBDD08753E9A8511810EE25B183A64ED8`。
- `Inkeys/Inkeys/Business/ScrollFollow.h`：`C5013C4DC971970DD8DDC65CD539D165C48E8A57D57131A1C2165F0D3EADE7BC` → `18BAD924A78268D6998877B9642CAE8D6472B13811D09C1236B76A85F8FFC559`。
- `Inkeys/IdtMagnification.cpp`：`C8B1651708F2EF9ECE15B577912195BF4C39ED2CDC6E768F1E1490E774C2F16A` → `5421ED52F0F238326FEFF4E099A2ABC54562ACF1378E5A5C65FE34414C308513`。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`：`D3F997D21363C7B43F3DA1EAEAF5C4BD5467B72BE5537216DA90DE3BDF0A44AB` → `1EDCD7E013587B47DB88EF9B4B815759F6659ECC9B38A9188E34FCAAF248F1E9`。
- `Build/ScrollFollow/run-live-external.ps1`：`62BE7D9ED75ABEB2D2F3A6C974EE82B2F31F11AD2A1C383814016AC627DB0244` → `139EBB5F5306BAFD00D4EBE5C68368CDE4FC352EE0070C0A8604D36405E1002F`。

## 实际录制首轮准备失败与只读就绪诊断

root构建/纯CLI已通过（ARM64 B7B0E655、x64 A4D40BE7）。先前默认短PID33604在初始化前安全门exit4/135ms，无窗口/输入；人工解锁后默认短PID33048/B7B0 exit0/6264ms，实际A/B、Exit与清理通过。两次均未启用录制，不冒称PGM或回放通过。

首个 `-RecordMotionFrames` 尝试 `external-8b9c8d4996d44759b649e60be67d1bf1`，Edge812、Inkeys尚未启动，2165ms在Stable初次Observe因合并的identity/readiness/visibility检查立即失败；receipt exit1/5126ms、私有进程已退出。不是8秒稳定超时，未进入Mag/Tracker，未产生PGM。fixture.ready只在首个requestAnimationFrame置true；是否初始未就绪或其它条件失败尚无细分证据，不先归因或改变等待。

本次仅改ignored runner：原一次只读表达式额外返回document.readyState；原合并失败分支在同一个已返回$o上构造固定白名单日志，含oracleNull、urlExact/runExact/fixtureIdExact三个bool、ready(bool或nonboolean)、白名单visibility和documentReadyState。null oracle的不可得字段明确unavailable；不发第二次查询替换失败瞬间，不输出URL原文、任意页面内容或账户数据。原throw、Stable等待、全部安全/身份/真实输入门不改。

runner139EBB5F→62B422F820CFD24E4596D0EF51199BB207BAEC72C033F263470A36FC0253343D；无BOM/CRLF/EOF保持、AST0，精确delta与收据在external-entry-stage/observe-diagnostic.patch/json。实施者仅静态检查，没有build/CLI/GUI/截图/输入；等待checker和root唯一下一次运行取得真实失败字段后，再决定是否需要有限的初始准备等待。

## 初始就绪等待候选（尚未实测）

第二个准备诊断 `external-fe3b8064939a4d798fcc773ad1583445` / Edge19896 / Inkeys0：1703ms原观察oracleNull=true，3292ms原throw，exit1/6504ms并正常清理。原targetURL/targetID仍精确，fixture状态尚未发布；未到Mag/录制，不能称PGM或回放通过。

按root批准，仅首个 `$null=Stable 8000 -InitialReady` 可在无Inkeys、尚无任何有效Observe时等待：同次Runtime envelope必须给出确切本次URL、visible及document.readyState；fixture缺失仅loading/interactive可待，或者fixture全身份/原数字DPR检查通过且bool ready=false可待。原8秒单次循环预算不重置；第一份有效Observe即永久关闭豁免，第二个及以后Stable严格。complete+缺fixture、hidden、错误URL/target/身份/数值仍立即失败；不追加输入/激活/重载或JS写操作。

候选627AD3825CDCB741E9970E4EE68BA75F6FD156ABFD51B8BB230EF867570C62D8与initial-ready.patch/json已交checker，AST0/CRLF/BOM/EOF保持；本实施者未执行CLI/GUI/build。初版候选误把flag加至第二个Stable，审查已指出并更正，仅当前候选可继续审查。

checker已独立核627AD382并放行，root随后授权应用。写前核当前runner62B422F8与候选全SHA，写后逐字节等于候选627AD382；仅ignored runner应用，无源码/CS修改或本实施者GUI执行。实际录制与回放仍待root唯一执行。
