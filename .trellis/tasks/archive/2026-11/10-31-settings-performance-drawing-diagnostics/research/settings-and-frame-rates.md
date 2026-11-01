# 设置页与帧率接线调研

日期：2026-10-31。只读源码调研；没有构建或 GUI 验证。

## 用户修正后的边界

- 条目为“界面帧率”，仅 30 / 60，默认 60；统一控制设置、主栏、翻页控件所在的 WARP 共享管线。
- Draw3 画布独立使用 30 / 60 / 120 / 240，默认 120。
- 不为主栏单独实现调度、不新增界面硬件加速，也不在性能页增加渲染状态。

## 已确认的源码事实

| 事实 | 证据 |
| --- | --- |
| 设置导航当前没有性能页，页面编号含已退役空洞 | `Inkeys/Inkeys/UI/Setting/Setting.Pages.h:14`、`Setting.cpp:2376`；新页应追加新编号，不复用空洞 |
| 中间导航顺序为常规、绘制、插件、组件、实验室、版本 | `Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp:151` |
| 三项迁移目标目前在实验室，修改只写配置，启动时应用 | `Inkeys/Inkeys/UI/Setting/Setting.cpp:4001`；硬件和兆芯由 `Inkeys/IdtMain.cpp:3495` 捕获，DComp 同时影响 HWND 创建样式 `IdtMain.cpp:3284` |
| 共享调度器节拍为 16,666,667 ns，按批统一执行所有注册客户端 | `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp:52`、`:736`、`:807` |
| 无 pending 时，Scheduler 用唯一事件等待；Continue/Retry 仅续对应客户端 | `RenderPipeline.cpp:709`、`:337`；调帧率不能把 idle 变成轮询 |
| Scheduler 和进程级模块均没有用户帧率 setter | `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm:295` |
| 已有无窗口调度器测试覆盖连续帧节拍和 Settings 独自连续帧 | `InkeysHeadlessTests/render_scheduler_tests.cpp:382`、`:467` |
| Draw3 已有四套完整参数，含建模输出率、预测步长和样本数量 | `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:452` |
| 画布默认档位是编译期 Fps120，创建模型配置时读取 | `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm:78`、`Draw3.InkPrediction.cpp:203` |
| Draw3 活动帧等待和预测计算都消费 timingProfile | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:16845`、`:10994` |
| Host 启动创建模型配置，没有公开的热切换整套模型参数接口 | `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:2036`、`Draw3.DrawingController.cppm:171` |
| 新配置按 schema 声明，并已有离散整数白名单解码模式 | `Inkeys/Inkeys/Other/Other.Config.cppm:336`、`Other.Config.cpp:580` |
| 设置写入使用既有 QueueConfigWrite/FIFO，不应在 render callback 做文件 I/O | `Inkeys/Inkeys/UI/Setting/Setting.cpp:1114` |
| 动画和边缘点光已在常规外观页，帧率显示在调试页 | `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:815`、`Setting.cpp:4067` |
| 软件调测已有显示器与 EDID 文本卡片，可参照排版 | `Inkeys/Inkeys/UI/Setting/Setting.cpp:4232`、`:4284` |

## 最小设计建议

1. 性能页放在“绘制”之后，沿用现有 PageHeader / SectionHeader / ComboRow / ToggleRow。
2. 界面帧率只在共享 Scheduler 增加一个受限帧率偏好；默认仍 60。设置可即时同步，影响下一批调度，保留现有请求位、固定 dispatch 顺序、idle 和恢复协议。无需逐客户端改造。
3. 画布帧率以启动选项传给模型配置创建函数，按整套 profile 构造。首版标“重启软件后生效”，不在一笔中途重建模型。旧单参数调用保留默认 120 语义。
4. 新键建议放在现有 `Inkeys::Config`：`UI.FrameRate`、`Drawing.FrameRate`；30/60 与四档数值分别严格白名单，非法类型、超宽整数及越界值回退 60/120。
5. 迁移三个 ToggleRow，沿用 `Experimental.Inkeys3.Draw3.EnableHardwareAcceleration / EnableDirectComposition / EnableZhaoxinCompatibility`，不迁移或改名磁盘键。
6. 界面 30 FPS 是目标上限，不改变动画 SpeedRate 或输入采样率；画布帧率独立，不等于显示器刷新率。测试以真实生产调度/模型创建入口验证，避免仅对枚举做镜像断言。

## 验证入口

- `Scripts/i18n.ps1 sync` / `check`；生成链保持 zh-CN、zh-TW、en-US、Keys header 和中文快照一致。
- `InkeysHeadlessTests` 中真实 Scheduler 的 30/60、idle、切档和多客户端同步测试；既有设置导航测试覆盖新页编号。
- 实施后完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，使用动态发现的 ARM64 原生 MSBuild，同一 PowerShell invocation 先清理重复 PATH 并设置 MSBUILDDISABLENODEREUSE，允许至少 5 分钟。
- 本轮规划无需编译；实施仍不运行 GUI、hidden HWND fixture、交互式 Console 或 Computer Use。
