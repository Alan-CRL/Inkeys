# Work 1 第二单元：绘图状态一致快照与写入边界

## 已证实缺口

StateModeClass 在 IdtState.h:65–112 为普通值字段。模式/PPT 写入使用 IdtState.cpp 的 stateModeTransitionMutex，但 SetPenWidth/SetPenColor、Bar 形状子型裸写未共用该锁；RenderPipeline 是独立线程，Bar.RenderLoop.cpp:994–997 虽已有 BarRenderFrameSnapshot，却逐字段直接读取全局且后续多处绕过它。MouseHook、Bar.Button、配置 SetMemory 等也直接读取。因此仅把 Bar 的子型写入包成 setter 不足以解决整体 data race。

## 最小合同

1. IdtState.h/.cpp 新增 GetStateModeSnapshot()，持已有模式锁复制 StateModeClass 值对象；保留 Select、Target、Echo 三字段不同语义。不能从锁内再调用会取同一锁的公有 getter。
2. 将当前生产写入者 SetPenWidth/SetPenColor 和形状子型命令纳入同锁；新 shape 入口锁内更新子型及必要 revision，锁外发布 Draw3/窗口副作用。配置旧 GetMemory 仅从未编译的 IdtDrawpad.cpp 调用，保留旧入口但 SetMemory 读取用一次快照，避免拆 RGBA 时混代。
3. BarRenderLoopState 初始化持有一次快照，WakeAndSnapshot 每帧更新它并派生已有 BarRenderFrameSnapshot；同帧绘制/动画方法从该值对象读取，不直接混读全局。UI 交互、按钮、MouseHook 在各自一次业务/回调边界取快照；不能把 target/echo 折成 current。
4. GetPenWidth/Color/IsLaser 之类公有读取按需从一致快照派生；IdtState 内已有持锁调用使用不加锁的内部 helper 或当前锁内字段，避免递归 mutex。SetMemory 只在锁外做文件 I/O；Draw3 Bridge 和 HWND/COM/GPU 仍由原 owner 处理。
5. 保留原编码/BOM/CRLF 与 C++20 module 项目登记。没有实测收益不称优化；性能观察沿用 H0 Release 算法/输入子链，但它不覆盖 Bar 整帧或锁等待，真机仍需人工门禁。
6. 长手势要把状态与 StateModeTransitionRevision 在同一锁内取为 StateModeVersionedSnapshot；FineDial 惯性/预设/Slider 以及 Geometry 按压的最终 SetPenWidthIfRevision/SetShapeModeSelectIfRevision 在锁内确认修订。旧候选在新模式下不得继承 visualWidth、velocity 或提交值；取消后让原有 UI 动画收尾。

## 文件分工与顺序

- 主 agent 唯一写 IdtState.h/.cpp、Bar.Interaction.cpp、Bar.Button.cpp、Bar.Layout.cppm、Bar.Main.cpp、Bar.EraserAttribute.cpp、MouseHook.cpp、IdtConfiguration.cpp 和父任务账本。
- Bar.RenderLoop.cpp 可在 GetStateModeSnapshot() API 冻结后交独立 implement agent 唯一写入；主 agent 在 worker 活跃期间不碰该文件。随后由独立 check agent/主 agent 查真实 diff、锁顺序与完整 Solution 构建。
- 先改 IdtState API 和所有生产写，再迁读取者；中间阶段不是线程安全完成态，不可标 PASS。保留第一单元已修正的 FineDial 时序合同。

## 可验证出口

- 搜索已编译第一方对 stateMode 字段的直接写仅位于持锁 IdtState 内或明确启动前路径；渲染/输入/配置读取由快照获得。
- 条件 PPT 旧 revision 不覆盖新用户 Pen/Shape 选择；Draw3 bridge 使用同一工具/宽/色；磁盘/Window/Render 资源操作不在模式锁内。
- Debug|ARM64 完整 Solution 与 InkeysHeadlessTests --no-window 通过；必要 Release 对照。真实 Bar、PPT、笔输入和锁等待尾延迟未获 GUI 授权时保持需要人工，不由编译替代。
