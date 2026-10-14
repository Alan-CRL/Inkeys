# Bug Analysis: 设置交互模式同步卡死

## 1. Root Cause Category
- B Cross-Layer Contract：Setting render持ImGui锁进入SyncDraw3State，Window Service把owner命令发到Setting HWND线程并等待future；HWND WndProc可能正等同一ImGui锁。共享RenderPipeline无法完成当前Settings回调，其他UI随之停住。
- D Test Coverage Gap：上轮控件回放将实际Sync替换为计数器，未覆盖生产跨线程同步。

## 2. Why Fixes Failed
1. 将Combo改为Radio只改变选择入口，仍执行同一阻塞Sync，因此用户现场仍然卡死。
2. 回放证明了选择命中和布局几何，却未证明真实同步调度安全；静态审查此前也没有串起ImGui锁与future等待边。

## 3. Prevention Mechanisms
- P0 DONE：统一Sync入口只在Setting渲染作用域延后到已有FIFO业务worker，覆盖直接与橡皮setter间接调用；非render语义保持。
- P0 DONE：无窗口线程回归提取生产队列、TLS作用域、defer guard和实际Sync/Write分支，加入竞争ImGui锁的有界模拟窗口回调，检查返回/顺序/停止排空。
- P1 DONE：rendering-and-ui.md记录7节跨层合同，配置规格明确控件回放不能替代线程回归。
- P1 DONE：审查补齐延后同步新增的模式读写竞争，复用IdtAtomic<int>及单次load；线程回归增加真实字段类型与设备更新块。

## 4. Systematic Expansion
- 同类入口：模式切换、历史画布保存开关和间接橡皮设置；统一入口guard覆盖这些既有路径，避免逐个setter重写。
- 保留Setting HWND线程所有权、owner desired/applied及既有重试；不以try_lock/跨线程改窗口/绕过失败方式处理。
- 不扩展成整个应用线程架构重构；此轮仅纠正当前生产同步边界和模式区域布局。

## 5. Knowledge Capture
- [x] .trellis/spec/native-desktop/rendering-and-ui.md：Setting绘制期间Draw3同步合同。
- [x] .trellis/spec/native-desktop/configuration-i18n-and-assets.md：Combo及信息卡片，保留启动判定。
- [x] 研究与回归脚本保存在本任务research目录。
- 没有模板同步目录，本仓库不是Trellis模板源；用户禁止自动提交，未commit/push。
