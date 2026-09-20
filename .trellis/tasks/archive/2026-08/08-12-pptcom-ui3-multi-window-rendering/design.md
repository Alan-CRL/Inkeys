# Technical Design

## Architecture

新增 UI3 公共串行渲染调度器，以及 PPT 窗口、状态、交互和绘制模块。调度器拥有唯一渲染线程、共享 D3D11/D2D 1.1 device、一个 manual-reset wake event 与客户端请求位集合；Bar 和五个 PPT 窗口通过相同的单帧客户端契约接入。

PPT 模块拥有五个窗口的运行时状态和独立目标资源。`IdtPlug-in.cpp` 通过窄桥读取放映状态、提交业务命令并通知 UI3，不再管理 UI 布局、命中、动画或 D2D 绘制。

## Window Model

- 窗口角色拆为五个明确的 PPT 角色，窗口服务负责注册、创建、查找、显隐和销毁。
- Bar 与五个 PPT 窗口均为 `Drawpad` 的直接 owned popup，使用 layered、tool-window、no-activate 语义。
- Z 序不依赖激活状态：公共层级函数先保证 Bar 在顶端，再将最近交互的 PPT 窗口插到 Bar 后，其余 PPT 窗口保持相对顺序。
- 每个 PPT 窗口的屏幕矩形即控件范围。ULW backing 尺寸独立于位置，只有尺寸/DPI/缩放变更触发重建。

## Render Client Contract

客户端 ID 使用稳定枚举：Bar、BottomLeft、BottomRight、MiddleLeft、MiddleRight、ExitShow，并预留 Settings。每个 ID 对应一个原子位。

调度器公开按位请求接口和客户端单帧入口。请求使用 `fetch_or` 后设置 wake event；渲染线程在帧边界交换请求位，并结合上一帧返回的 `continue`/`retry` 集合产生本帧工作集。固定处理顺序为 Bar、BottomLeft、BottomRight、MiddleLeft、MiddleRight、ExitShow。

单帧结果为：

- `idle`：当前状态稳定，不自动续帧。
- `continue`：动画、按压或诊断收尾仍需下一帧。
- `retry`：目标提交失败，仅该客户端在下一帧重试。

交换请求位后必须再次检查并在 reset event 前使用无丢失协议，避免请求发生在“判断 idle”与“进入等待”之间。全局帧截止时间按 `steady_clock` 控制到最多 60 FPS；同一帧期间的新请求合并到下一截止时间，不突破节拍。

## Graphics Ownership

- 共享资源：D3D11 device、D2D device、必要的设备级工厂和设备丢失代次。
- 每窗资源：D2D device context、目标 bitmap/surface、GDI interop/DC、DIB/ULW 数据及 dirty-rect 事务。
- 所有 D2D context 使用和 ULW 提交只发生在共享渲染线程；消息线程只写状态快照、交互队列和请求位。
- 单窗目标失败销毁并重建该窗资源；确认共享设备丢失后统一重建设备、递增代次并请求全部现有客户端。

## State And Input Flow

- PPT 状态桥将页码、放映可用性、模式等写入线程安全快照；页码版本变化请求四个页码窗口。
- 五个 HWND 的窗口过程将鼠标、触摸和拖动事件转为带客户端 ID 的交互记录。全局滚轮钩子直接提交同一队列，不再向旧窗口发消息。
- 渲染线程按目标窗口消费交互、推进 400 ms 长按和动画，再产生精确脏区并提交 ULW。
- 成对配置仍是单一事实来源；拖动任一侧更新共享配置，并请求该对两个窗口。
- Settings 的现有三个 PPT 配置通知点改为按受影响控件映射请求位，不改变 JSON 字段。

## Dirty Diagnostics

沿用 Bar 的脏区事务定义，但 PPT 不采样或绘制 FPS。活动提交绘制红色 dirty rect；客户端从活动转 idle 时安排一次绿色最终 dirty rect；蓝色框严格描绘各自 HWND backing 边界。诊断覆盖也必须包含在该窗提交脏区中。

## Compatibility And Rollback

- 保持所有外部 ABI、JSON 配置和业务命令不变，迁移可在 C++ 内部完成。
- 先引入调度契约并让 Bar 接入，再加入五窗，最后切断旧 `ppt_window`；每阶段保持可编译，便于按阶段回退。
- 不修改历史 `None` 文件，不扩展到 Settings 或 flip-model，控制本任务回滚范围。
