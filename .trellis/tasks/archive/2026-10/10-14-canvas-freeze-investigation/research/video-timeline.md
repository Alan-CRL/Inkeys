# 用户视频取证记录

调查日期：2026-10-14 至 2026-10-15（Asia/Shanghai）。原片 `D:/Personal/Downloads/10月11日.mp4`，由用户授权的 `D:/Project/Resource/ffmpeg/ffmpeg.exe` 提取帧；没有修改原片。

## 媒体与环境事实

- MP4 79.95 秒，H.264，1920×1080，30 FPS。容器 creation_time=2026-10-11T15:12:40Z。
- 视频中 Captura 自身计时与 MP4 时间有偏移，本文一律使用 MP4 时间；不根据 Captura 显示推算绝对时间。
- 视频中的 Draw3 日志显示实际窗口/主画布为 1366×768、96 DPI，拓扑为 CloneOrMixed；录制画面存在放大。标题路径含 Inkeys64，不能据此断言准确二进制 hash 或 commit。
- 屏幕左侧一直可见的是 Captura 自己的设置界面，不应把它当作 Inkeys Setting。

## 时间线（秒级近似）

| MP4 时间 | 可见行为及日志 | 可支持的结论 |
| --- | --- | --- |
| 约 14–17 秒 | Inkeys 首帧、选择态、空画布，控制台出现 startup/RTS/Draw3 信息 | 本次运行可启动，非一开始整体崩溃 |
| 约 18–24 秒 | 选择画笔并绘制红色墨迹 | 曾正常接收实际笔画 |
| 约 25 秒 | Inkeys Setting 首次打开 | 首次打开设置并未立即构成画布永久失效证据 |
| 约 30–35 秒 | Setting 关闭后画布仍有后续红色墨迹 | 不能把“任何一次设置show”直接等同故障 |
| 约 40 秒 | 从 Inkeys 打开 Explorer；墨迹仍覆盖外部窗口 | 外部程序窗口真实出现 |
| 44 秒 | Explorer 可见、Inkeys 选择态，点击 More | 准备第二次进入 Inkeys Setting |
| 约 45–46 秒 | Setting 再次打开，Explorer 留在后面，旧墨迹覆盖设置 | 需复测“选择态/有墨迹时重开设置”这条顺序 |
| 约 47–48 秒 | Setting 关闭；鼠标移到并点击 Explorer 关闭按钮 | 紧邻失效前的原生焦点/显隐操作 |
| 49 秒附近 | Explorer 已关闭；日志显示 selection-ulw 输出 revision=5 和真实 present success | 画布输出目标切换至少曾成功；旧墨迹不是损坏截图 |
| 50–53 秒 | UI 软笔高亮；用户拖动画线但旧墨迹无对应变化；mouse accepted=1、frame、空 dirty 日志继续 | 该时间段 Win32 光标与 Draw3 循环仍活；落笔管线失效候选 |
| 后续至约 70 秒 | 工具可切换，橡皮光标可出现，旧墨迹仍在 | 不支持整个进程/全部 UI 已停止；旧内容未被正常擦除 |
| 约 74 秒 | 点击清空后的旧墨迹从画面消失 | 反对全部canvas command始终不推进；无命令处理计数/目标revision时，仍保留隐藏或换场景的有限替代解释 |

## 字段释义与证据边界

1. `DrawingTool::Pen=0`；`owner=2` 是 Mouse 光标归属。`tool=0` 与软笔高亮一致，不代表未选工具。
2. `mouse=valid/contact` 来自 Win32 cursor sample；`mouseLifecycle` 是橡皮视觉 HasPosition/NeedsAnimation。它们不能代替 RTS contact 或 controller active 数量。
3. `present success=0` 在 dirty 为空时只是这一轮没调用 Present。真正 GPU 错误应有 `[Draw3Diag][present] event=failure`、HRESULT、raster/recovery 记录等。
4. Win32 `mouse accepted=1` 只证明光标采样通过过滤；当前产品连鼠标笔画也由唯一 RTS producer 发送 Down/Up。
5. 50秒后采样帧可见 Win32输入/画布空帧推进，但控制台滚动、截图裁切和红色墨迹遮挡会遗漏日志；不能仅因某帧没显示 `rts-input` 就证明整个区间没有 RTS 回调。
6. rts-input 日志在本地 Git 的 21a37239（2026-10-03）加入；视频录制日期晚于此提交，但仍需二进制身份才能严格映射到源码版本。

## 提取产物

帧图统一保存在本地 `Build/CanvasFreezeInvestigation/video/`，作为生成证据不纳入产品源码；下列名称相对此目录。

- `overview-5s.jpg`：每五秒一帧的全片索引。
- `working-16s.jpg`：首帧和环境日志。
- `detail-01.jpg` / `detail-03.jpg`：44、46秒，第二次打开设置前后。
- `transition-09.jpg` / `transition-13.jpg` / `transition-17.jpg`：约49、50、51秒的切换和失效输入。
- `detail-07.jpg` / `detail-10.jpg`：约50、53秒，全分辨率日志。

上述时间点采用fps提取，边界存在不足一帧至半个采样间隔的量化误差；需要精确Down瞬间时应按原片帧时间戳重新提取。
