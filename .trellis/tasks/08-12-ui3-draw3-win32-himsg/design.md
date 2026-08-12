# UI3 / Draw3 接入准备设计

## 边界

本任务建立三个基础模块：上游 HiMsg、`Inkeys.Graphics.Surface` 与 `Inkeys.Window`。传统业务模块继续决定何时绘制和执行 PPT/画板动作；Window 只拥有 HWND 生命周期和窗口状态，Surface 只拥有 CPU/GDI 图像资源。

## HiMsg

- `EnqueueMessage(ExMessage)` 在队列既有同步边界内推导消息分类并入队，容量满时沿用丢弃新消息和 dropped 计数语义。
- `ClearMessages(MessageFilter)` 与生产/消费操作共享确定的线性化边界，只删除调用时已入队且匹配的消息。
- 不新增 peek；`GetMessage` / `TryGetMessage` 保持成功即消费。
- Inkeys 子模块仅消费 HiMsg 源码和公开头，不递归 HiMsg 自带 vcpkg。

## Surface

`DibSurface` 持有 memory DC、DIB bitmap、选入前旧对象和 BGRA 像素指针。析构顺序固定为恢复旧对象、删除 bitmap、删除 DC。构造/resize 先在临时对象完成全部 Win32 分配，再交换；复制通过新 Surface + `BitBlt`/像素复制实现；移动只转移所有权。

加载与保存继续复用 GDI+，避免引入新的图像依赖。对旧 `IMAGE` 的最小兼容只暴露实际使用的成员能力，不复制 EasyX API 或全局状态。

## Window

Window service 维护角色到窗口记录的映射、两个拥有线程和命令队列：

- Overlay 线程：Mag host/child、Freeze、Drawpad、PPT、Bar、DisplayObserver。
- Setting 线程：唯一 Setting HWND。
- Hook 线程：低级输入 Hook，自有安装/卸载和停止事件。

`Start` 创建 jthread，并以 promise/future 报告窗口组创建结果。公开状态操作封装为命令并唤醒所属消息泵；WndProc 只转发/记录原生消息，HiMsg 在创建线程 bind、销毁后 unbind。`Handle` 是非拥有借用句柄，调用方不得销毁或改 owner/style。

退出先停止仍访问 HWND 的渲染/交互线程，再由拥有线程逆序 DestroyWindow、unbind、注销窗口类，最后 join。停止使用 event + `MsgWaitForMultipleObjectsEx`，event 创建失败时有界轮询 stop token。

## Owner 与 Z 序

覆盖层以 owned popup 形成一次性链：Mag 为根，其上依次 Freeze、Drawpad、PPT、Bar；无 Mag 时 Freeze 为根。只对根窗口刷新 `HWND_TOPMOST`，由 owned-window 关系维持相对层级，不做周期逐窗口 SetWindowPos。

Setting 是无 owner 的普通 app window，与覆盖层状态、ready 和 TopMost 维护完全隔离。

## 兼容与回滚

- Draw2 的呈现入口继续接收 HDC 并调用 layered-window present，便于按模块逐个迁移。
- 每一阶段先保持可构建：HiMsg、Surface、业务抽离、Window、EasyX 删除、UI3 收口。
- HiMsg 使用独立 commit；Inkeys 只记录子模块指针与接入变更。若 Inkeys 集成失败，可回退子模块接入而不撤销上游 API commit。
