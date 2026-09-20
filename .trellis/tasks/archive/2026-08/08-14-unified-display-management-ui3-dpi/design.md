# 统一显示器管理与 UI3 动态显示适配设计

## 模块边界

新增 `Inkeys.Display`，独立于 `IdtMain.h`。模块拥有显示器枚举、EDID 读取、DPI 查询、不可变快照发布和订阅生命周期；DisplayObserver HWND 只把系统消息转换为 `Refresh`。

公开类型包括 `EdidInfo`、`MonitorInfo`、`Snapshot`、`SnapshotPtr`、`ChangeReason` 和移动式 RAII `Subscription`。公开入口为 `Initialize`、`Refresh`、`GetSnapshot`、`Subscribe`、`Shutdown` 与 `WindowProc`。`Snapshot::Primary()` 和 `Find(HMONITOR)` 返回快照内只读记录。

枚举阶段在局部对象中完成。与当前快照语义比较后，仅变化时递增 generation 并原子发布；回调在内部锁之外串行取得并调用。订阅回调只能保存目标或请求 UI 客户端，不能直接操作 D2D/窗口资源。

## 数据与兼容

- EDID 保留字节 21/22 的原始厘米值；无效头、长度不足或零尺寸均保持 unknown。
- `MonitorInfo` 同时包含完整屏幕范围和工作区；UI3 使用完整屏幕范围。
- DPI 优先动态调用 `GetDpiForMonitor`，不可用时回退设备上下文，再回退 96 DPI；不改变进程 DPI awareness。
- 首次枚举失败时用系统指标建立 `fallback=true` 的主屏记录；后续失败保留最后成功快照。
- 传统消费者每次取得一个 `SnapshotPtr`。旧绘图尺度在显示快照发布后由主程序桥接更新。

## PPT 重排

PPT 布局输入由 Drawpad HWND 改为当前主屏快照。每个客户端保存当前与目标显示布局；显示/DPI 变化时从当前呈现状态建立 0.4 秒 ease-in-out 过渡。动画中收到新目标时直接从当前插值状态重定向。

运行时配置先按底部按钮组、退出按钮、中部按钮组的优先级约束。低优先级组碰撞时恢复默认偏移再约束；目标尺寸本身无法放入主屏时仅降低运行时有效缩放。纠偏副本不进入配置写盘路径。

拖动期间只记录待处理显示 generation；捕获结束后由渲染线程应用。目标位图按起止端点最大尺寸扩容并复用，绘制保持 96-DPI 逻辑坐标，通过当前插值 scale 变换，避免逐帧重建文字和位图资源。

## Bar 重排

Bar 保存当前/目标主屏范围、DPI scale 和主按钮屏幕中心。新快照保留主按钮相对主屏的局部像素中心；若按钮完整范围越界，将目标中心夹到最近的可见位置。位置与 scale 使用 0.4 秒 ease-in-out 同步插值。

所有状态应用在共享渲染线程；直接拖动期间延后。动画中命中、布局、窗口范围和脏区均使用当前插值 scale，并为端点最大尺寸预留渲染容量。

## 失败与回滚

- 显示枚举失败：保留上次快照并记录失败，不触发 UI 跳变。
- 订阅者退出：RAII 注销等待正在执行的回调，防止悬空访问。
- 设备 target 创建失败：沿用各 UI 客户端现有 Retry/DeviceLost 协议。
- 可按模块、PPT、Bar 三批回滚；旧文件在所有消费者迁移完成后才删除。
