# Draw3 运行时全量验证设计

## 1. 测试工程边界

`inkStrokeModelerTestTests` 是原生控制台工程。它通过工程链接引用生产 `draw3` 的 `.cppm/.cpp`，让 MSBuild 重新编译同一份实现，并用 `DRAW3_TESTING` 开启窄化测试钩子。测试入口只负责用例调度、进程驱动、JSON 读取和退出码。

为避免 `main.cpp` 重复入口，应用层可复用逻辑保留在现有模块中；测试驱动独立启动主程序完成呈现基准。测试工程不直接创建第二套 D3D/窗口实现。

## 2. Wake generation

coordinator 持有：

- 一个 auto-reset Win32 event；
- 一个单调递增的 64 位 wake generation；
- 一个合并的 ControlWake pending 标记。

Down、Up/Cancelled 和控制请求先发布其状态，再递增 generation 并 `SetEvent`。Move 只更新 snapshot，不 signal。绘制线程在处理完当前批次后捕获 generation，等待剩余帧预算；等待前后二次核对 generation，避免 reset/set 交错造成丢唤醒。

空闲仍由 queue 的 `wait_dequeue` 负责。活动等待仅用于“距离下一目标帧还有时间”的区间，event 到达后立即回到命令/终态/控制处理。

## 3. 指标会话

`RuntimeMetricsSession` 由主程序按参数可选构造，`DrawingController` 只保存可空非拥有指针。关闭指标时所有热路径先判断空指针，不创建容器、不格式化 JSON、不写文件。

开启时预留固定/有界样本容器，并记录：

- contact 的工具、设备、资格 QPC 和首次成功 Present QPC；
- 帧开始/成功 Present QPC、工作时间、Present 时间；
- coordinator 诊断计数快照；
- 空闲窗口开始/结束时的 frame、Present、wait 和 spin 计数。

软件落笔定义：

- Pen/Eraser：成功 PublishDown 的 QPC 到包含该 contact 的首次成功 Present；
- Highlighter：真实路径首次达到 12px，或 Up 生成 short mark 的资格 QPC，到首次成功 Present。

这样不会把用户完成 12px 手势的物理时间错误计入渲染软件延迟。

## 4. JSON 与基准驱动

主程序支持显式指标输出路径和严格验收开关。退出时以临时内存快照生成单个 JSON 文档；目录由调用者预先创建。原始文件放 `TestResults/` 并忽略。

测试驱动启动同平台/配置的主程序，等待窗口就绪，用 `SendInput` 注入工具选择和鼠标序列，调整窗口、清屏并留出 5 秒空闲区间，发送 `9` 正常退出，最后解析 JSON 并核对分位数与阈值。

## 5. 兼容和失败语义

- event 使用 `CreateEventW/SetEvent/WaitForSingleObject`，保持 Windows 7 兼容。
- 指标文件失败时严格模式返回非零；普通模式记录诊断但不影响绘制。
- Present 失败不生成落笔成功样本，并保留现有 full-present 恢复语义。
- 自动 SendInput 只证明 Mouse/software 路径，不能替代 Pen/Touch RTS 真机验证。
