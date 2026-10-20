# 整合审阅记录（2026-10-19）

## 审阅范围与材料

本轮由最终 Trellis check 审阅父任务及两个子任务的全部改动。已加载父/子 PRD、design、implement、check manifests、父共享研究与两个子任务实施记录；显式补读 native-desktop 的 rendering-and-ui（主栏贴底/输入呈现、Whiteboard、窗口服务）、draw3-integration（单 Host、固定穿透、region/尺寸）、startup-preview，以及 native/runtime-and-rendering 的 resize/输入所有权，native 与 PPT package index/Quality Check。hook 原始完整输出已从保存文件读取，超长材料按相关章节补读。

Display 与 Bar/Startup/IdtMain 的局部修复由本 reviewer 负责；Window 由另一 canvas checker 负责，同目录共享工作树下未交叉写入。全量 Solution 与统一 Headless 由主会话执行，避免并行争用工程缓存。主会话维护三份 spec 增量，审阅确认与实现合同相符。

## 本轮已修复

1. **回弹阶段提前跳至新任务栏线**（Bar.RenderLoop.cpp）：新动画 gate 已拒绝非 Stable phase，但后续传统稳定 SetDirect 仍使用新线，绕过了延期动画。现仅在 Desktop 任务栏 retarget pending 且 gate 尚未开放时暂停该直写，保持旧 Y；真正显示器/DPI/场景路径保留。阶段刚收敛时保留一次续帧，确保零形变收敛也会消费待处理目标，而非直接 idle。
2. **动画中途起拖使用终点导致零移动脱离**（Bar.Interaction.cpp、Bar.RenderLoop.cpp）：Desktop 已吸附手势的 Begin 与已成功呈现环境 Rebase 使用实际 mainCenterScreenY，加上原 grabOffset，保留原 rigid/elastic 含义；Floating、PPT/Whiteboard 保留目标线算法。取消任务栏动画时冻结最后成功呈现中心，减去直移与 monitor origin，不采用未提交候选。按住期间同几何/DPI/总倍率的 Desktop 任务栏通知只更新目标环境，不挪动当前 Y 基线；松手并恢复 Stable 后消费最新目标。
3. **双画布第二次故障重新揭开旧区域**（与 canvas checker 协调修复）：几何失败会清 applied key，下一次重试不能据 key 判定旧 region 是否适用。现在 recoveryVisible 仍存在就保持不安全状态，失败回滚不揭开，直到新策略实际成功。另由 canvas checker 修复后到 Hidden 意图优先、查询失败成对隐藏；详细证据见子任务 research/check-validation.md。

4. **兼容副栏 Top/Left/Right 厚度缩小无法确认**（Display.cpp）：完整对齐连续两次采样的确认原本仅识别 Bottom，autoHide 的其他方向会永久保留旧完整厚度。现按每个 native edge 对称判断对应边界（Top→top，Left→left，Right→right，Bottom→bottom），沿用同一两次确认，不增加 API、schema 或快速采样。
5. **条件表达式双向转换类型歧义**（Bar.RenderLoop.cpp）：第一轮完整编译报告 C2445 与后续 C2737；新 presentedY fallback 的 IdtAtomic<double> 同时允许转为/由 double 构造。现显式 static_cast<double>，其余新条件分支均以 double 算术或已有类型明确返回。此为源码类型错误，不是本机环境故障，未改项目/工具链。

6. **底行测试误把已安全矩形当作待裁剪矩形**（window_geometry_tests.cpp）：统一 Headless 第一轮仅2条失败；原 fixture.bottom=1100、origin.y=-200、translation.y=-31 时实际 bottom=869，已低于保护上界879，生产 min clamp 正确保留原边界，测试错误要求扩展至879并把+1当越界。现仅将待裁剪 fixture.bottom 改为1200，使四种 translation 都实际穿过保护行；独立 already-safe 不扩展用例保留。没有为测试改生产裁剪算法。

## 已核对生产路径

- Display 逐屏 taskbar edge/autoHide/exposure/lastKnown/dockTop 与独立完整厚度进入语义去重；瞬态矩形不逐像素发布业务目标，快路径仅查询缓存 HWND/PID/rect/style，无 EDID/枚举。
- WinEvent OUTOFCONTEXT owner 回调只过滤/排队；25ms 合并、条件200ms兜底、2秒结构恢复；安装/卸载同线程、epoch拒绝旧回调、Shutdown先失效再异步清理，不持 refreshMutex 等 owner。订阅通知在内部锁外。
- pending/presented 的 desktopDockLine/protectBottomEdge 与几何/DPI/UI基准保持同一偶数 serial。完整读、成功呈现写和直移 Rebase 的 release/acquire fences 已在原路径存在；此次 Pending 增量对称补齐，无需重复加 fence。
- 主栏 final viewport 在 source/size mapping tuple 形成前裁剪，包含 direct translation；mapping 改变强制全窗 ULW 替换，稳定恢复 partial dirty。最终提交锁内重新核对最新位移，越界则配对 EndDraw、不发布成功快照并保留重试。安全 SetWindowPos 可用，越界直接移动改为 ULW；空 viewport 等待新唤醒避免零尺寸热循环。
- Mini 单次任务栏/真实 DPI 采样冻结预览线；CenteredBottomBar 和正式 Bar 使用相同桌面线，角落浮动启动保留原语义。PPT/Whiteboard scene inset 5真实屏幕DIP优先。
- Bar 底行保护仅 Desktop 底部 autoHide；双画布的底行/NonRude 则是任何 Desktop/PPT，任务栏通知不触发 Draw3 resize、资源重建或笔迹裁写。白板双窗恢复原 region/属性并继续使用 Freeze/root 原有 best-effort 全屏锚点。

## 已实际验证

- `git diff --check --ignore-submodules=all`：退出0。
- 修改的21个既有产品/测试源码 BOM 与既有观察记录一致，工作树均为 CRLF，无 bare LF；Git blob 内统一 LF 是工作树转换，不据此重写文件。
- 独立编译直接 include 当前完整 `InkeysHeadlessTests/bar_bottom_dock_tests.cpp`，只增加 console main，调用原 `RunBarBottomDockTests()`，未复制算法或删减原用例。vswhere 定位当前 VS ARM64 原生 cl，`/std:c++20 /permissive- /EHsc /W4 /WX /utf-8 /MTd`，独立产物位于忽略的 `.trellis/.runtime/taskbar-check`；编译退出0、无 warning，运行退出0，无 HWND。
- 新增生产 Tracker 用例覆盖动画中间位置与终点相差超过脱离阈值、非零 direct translation、负屏幕原点、UI倍率0.5/1/3；Begin→零移动 Update→小量移动→新任务栏环境 Rebase→零移动 Update，保留既有4DIP/后续6DIP弹性而不凭终点距离脱离。Y动画真实 BarUiValue、方向/显隐/场景、viewport与安全直移原增量用例亦由统一 Headless 接线。
- canvas checker 独立 ARM64 `/W4 /WX` 执行真实 `RunCanvasPolicyTests()`，输出 failures=0；该测试无条件注册于 `--no-window`，不创建 HWND。详细 Win32故障注入和 GDI 预热后等量批次证据见子任务记录。
- 全量 `InkeysRepo.sln Debug|ARM64`、统一 Headless `--no-window` 的结果由主会话填入父任务最终 validation.md。本 reviewer 未重复运行全量工程或旧测试产物。

## 全量验证交接

主会话第一轮完整 build 在86.41秒退出1，首个有意义错误是上述 C2445，后续 C2737为同源；编译日志位于忽略目录 `.trellis/.runtime/ui3-taskbar-dock-build`。等待该 build 完成后才应用类型修复与批准的四方向兼容补丁，没有在编译中修改源。完成后再次 `git diff --check` 退出0，已交回主会话按最终树增量重建并统一执行 Headless；最终结果见父任务 validation.md。

最终产品源码完整 Solution 重建已退出0（23.89秒，0 errors、3条既有 hashlib++ warnings），Inkeys/Headless/PptCOM/Tests 经同一 Solution 完成。该产物的统一 Headless 第一轮98.30秒退出1，唯一2条错误是上述待裁剪测试矩形不满足前提；待该进程完成后才修改测试 fixture，主会话将按最终测试树增量重建并再次统一执行。未因错误断言改生产算法。

本 reviewer 已完成全部代码范围的静态审阅及安全局部修复，没有保留需要产品决定的具体代码缺陷。GUI/Shell兼容性/视觉验收仍保留以下真实验证边界。

## 验证边界

无窗口 helper/状态机与编译不能证明真实 USER32/Explorer 消息、DComp/ULW 可见裁剪、Shell全屏识别或任务栏触发已实测。Windows7、第三方Shell、UIAccess、多屏实际动画/输入/性能仍需另获 GUI 授权后验收；未启动应用/设置窗口，未操作实际任务栏，未执行 commit/push。

## 最终集成结果（主会话实际执行，2026-10-19）

- 修正最后测试 fixture 后，完整 `InkeysRepo.sln Debug|ARM64` 按既有 Solution 增量构建退出0，耗时5.54秒，0 errors、0 warnings；此前最终产品源码完整重建23.89秒退出0、0 errors、3条既有 hashlib++ warnings。
- 构建产物相对本轮全部生产/测试源码的时间戳已由主会话核验；最终 fresh `InkeysHeadlessTests.exe --no-window` 退出0，耗时104.12秒，stderr 0字节，包含 `[CanvasPolicy] failures=0` 和 `PASS animation correctness`。
- 最终树相对上次成功测试后无产品/测试代码改动；之后仅维护验证/任务文档。全范围检查无未修复具体问题，类型/编译、统一无窗口回归及 diff 检查均通过。GUI/Shell/跨系统实机限制仍如上，不将自动验证当作视觉验收。
