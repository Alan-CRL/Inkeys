# 当前 Bar 拖动与显示合同

## 已确认事实

- `Bar.Interaction.cpp::Seek` 的鼠标路径轮询 `GetCursorPos`，触摸路径消费保留屏幕相对坐标的队列消息；两者最终更新 `directWindowDragTranslationX/Y`。
- 交互线程通过 `try_lock(directWindowDragMutex)` 调用 `SetWindowPos`，不能等待包含 ULW 的慢提交；渲染线程在松手后把 translation 吸收到主按钮布局。
- ULW 的 `pptDst` 始终叠加 desired translation，成功后发布 committed HWND bounds 与 presented translation；吸收时需保留 HWND 尚未追上布局的残余补偿。
- 普通窗口消息在入队时已经从屏幕坐标转换为 layout 坐标，避免 resize 后重解释旧 client 坐标；拖动状态切换不能只修改 HWND y。
- `Inkeys::Display::MonitorInfo` 已提供 `bounds`、`workArea` 和有效 DPI；Bar 当前发布 bounds/DPI，尚未发布 workArea。
- `BarStateClass::PositionUpdate` 目前按主按钮所在半屏自动选择主栏左右和菜单上下；底栏需要覆盖为主栏向右、面板向上。
- 主按钮和主栏基准高度为 80 DIP，边框居中绘制。稳定底边必须使用几何底端加半个 stroke，而不是 viewport/dirty/HWND 底端。
- 首次完整主栏宽度在 RenderLoop 布局后才可得，初始整体居中应在该时点完成。

## 实施约束

- 保持现有共享串行渲染调度器、直移几何锁、idle 唤醒和 device epoch 合同。
- 复杂弹性逻辑先放入纯 helper 并由 Headless 测试覆盖，不直接堆入 `Seek` 或 D2D 绘制分支。
- 只在弹性活跃时请求续帧；次级面板保持自身动画和布局，不参与主体纵向 scale。

## 实施后确认的并发合同

- dock mode、phase、抓取点、弹性偏移和 direct translation 必须在吸收直移之后、`ApplyDisplayTransition` 之前按同一偶数 transition serial 锁存；显示过渡、布局、动画和 ULW 都消费该帧快照，不能在阶段中重新读取实时原子值。
- Display 回调可能在交互发布 presentation barrier 前已完成新 displaySerial/zoom 的 ULW。每个绝对采样必须先检查 presented tuple；真实上屏的 bounds/workArea/zoom 无论形态 barrier 是否完成都立即接管并重基准，barrier serial 只决定何时释放形态/位移等待。随后再读取 pending、用新 dock 线判断捕获或脱离；不能用 pending 是否变化作守卫，否则配置 zoom 快速回转会漏掉中间已呈现状态。
- 初始或循环中的指针采样失败、触摸取消、模式改变都属于取消点击；即使窗口没有产生最终位移，也不能折叠或展开主栏。

## 基线验证

- 基线提交：`47acefc5 wip(ui3): checkpoint unified display adaptation`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：退出码 0。
- ARM64 host MSBuild 完整 `InkeysRepo.sln` `Debug | ARM64`：0 errors，140 个既有 warning。
