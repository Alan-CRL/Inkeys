# 启动上下文与调用关系

## 主时序

| 顺序 | 代码位置 | 执行线程/阻塞 | 已确认行为与失败出口 |
| --- | --- | --- | --- |
| 1 | `Inkeys/IdtMain.cpp:139` `wWinMain` | 主线程 | 先做 hidden Draw3 test、路径/可写性、文件名、单实例、崩溃处理、架构/OS/更新检查；这些都在合法 Preview 边界之前。 |
| 2 | `Inkeys/IdtMain.cpp:591-743` SuperTop | 主线程，可能启动 helper 并退出 | `ReadSettingMini()` 只为旧 SuperTop 配置；父进程可 `LaunchSurperTop()` 后 `exit(0)`，helper 分支也直接返回。 |
| 3 | `Inkeys/IdtMain.cpp:744` 后 | 主线程 | 本任务唯一合法 T0：最终进程已经越过所有 SuperTop 重启/提权分支。立即启动 Tracker，再做新 mini config、DPI 和可选 Preview 准备。 |
| 4 | `Inkeys/IdtMain.cpp:750-855` | 主线程 | 日志清理和 async logger 初始化可能阻塞；当前 T0 不能放在这里。 |
| 5 | `Inkeys/IdtMain.cpp:856-894` | 主线程 | 当前 DPI awareness 设置过晚；旧 drawpad/tester resize 与 awareness 混在同一块。 |
| 6 | `Inkeys/IdtMain.cpp:895-908` | 主线程 | COM 初始化，随后 `Display::Initialize()` 同步枚举显示器。 |
| 7 | `Inkeys/IdtMain.cpp:910-1167` | 主线程 | 默认配置、`config.ReadAll()`、必要写回和 deploy 兼容读取；UI3 当前固定中文。 |
| 8 | `Inkeys/IdtMain.cpp:1189-1204` | 主线程 | I18n、快捷键和 DesktopDrawpadBlocker 插件。 |
| 9 | `Inkeys/IdtMain.cpp:1206-1226` | 主线程 | 从资源 221 创建 PptCOM activation context 并加载 COM 服务。失败目前属于启动失败。 |
| 10 | `Inkeys/IdtMain.cpp:1227-1323` | 主线程 | 自动更新线程；RenderPipeline 初始化；字体文件/集合初始化。开关开启时只把 RenderPipeline 提前，字体不前移。 |
| 11 | `Inkeys/IdtMain.cpp:1325-1513` | 主线程等待两个 owner promise | 组装 WindowSpec 并启动 Window Service；其 overlay 和 setting owner thread 创建 HWND，`Start()` 等待两个线程的启动结果。 |
| 12 | `Inkeys/IdtMain.cpp:1514-1568` | 主线程 + Draw3 drawing thread handshake | 启动 Draw3，失败后尝试现有 fallback；最终失败当前只日志并 return。 |
| 13 | `Inkeys/IdtMain.cpp:1569-1608` | 主线程 | Setting、Whiteboard、Draw3/Product 首帧同步和初始 topmost refresh；这些失败出口需要纳入显式失败流程。 |
| 14 | `Inkeys/IdtMain.cpp:1616-1633` | 多线程 | 启动 TopWindow、Bar、Freeze、StateMonitoring 和 PPT 线程；阶段完成乱序上报给 Tracker。 |
| 15 | `Bar.Initialization.cpp:115-170` | Bar 初始化线程 | Window/UI/media/preset/components/state/position/mouse/render client/interaction；当前多处 silent return。 |
| 16 | `Bar.RenderLoop.cpp:12357-12619` | 唯一 RenderPipeline 渲染线程 | GetDC、ULW、ReleaseDC、EndDraw 全成功后 `presentCompletion.IsCommitted()`；这才是 100% 和交接门。 |
| 17 | `Inkeys/IdtMain.cpp:1641-1656` | 主线程等待 offSignal | 退出顺序当前为 Whiteboard/Setting、线程 join、Draw3、Window、RenderPipeline、Display；新模块必须更早停止并注销。 |

## 并行阶段

- `Bar::Initialization()`、Freeze、TopWindow、StateMonitoring 和 PPT 在主线程完成核心初始化后并行启动。
- PPT UI 在 `IdtPlug-in.cpp:624-640` 注册 PageControl client；真正连接 Office 的 `GetPptState()` 在后续 detached 工作中，不是现有启动门。
- Freeze 当前在 `IdtFreezeFrame.cpp:93-95` 未检查提交返回值就置 ready，需要改为实际 ULW 成功后报告。
- `TopWindow` 在 `Window.Legacy.cpp:90-171` 最多等待约 20 秒后反复请求 topmost refresh；它必须消费显式 ready/failure 状态，Preview 则通过异步 observer 在自己的 owner thread 重验 z-order。

## 失败边界

- T0 之前：路径、单实例等保留 `ShowStartupMessage()`，没有 Preview。
- T0 之后且 Preview 未建立：复用现有 `Inkeys.UI.MessageBox`/系统 fallback；该弹窗不依赖共享 RenderPipeline。
- T0 之后且 Preview 已显示：Tracker 冻结并请求 red frame；成功 ULW 或 350ms 上限后进入同一错误弹窗。
- Preview、BIN、cache、blur、shimmer 失败均非致命；共享 RenderPipeline、Window Service、Draw3 最终启动、Setting、Whiteboard 和正式 Bar 失败维持致命语义但不再静默退出。
