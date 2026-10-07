# 崩溃与恢复设计

- 追当前 `wWinMain`、异常处理、RestartProgram、单实例、PptCOM、Draw3 UInk/索引，画真实 owner 与时序；不把声明当成成功链路。
- 崩溃路径避免业务锁、渲染线程、可能损坏对象与完整正常退出；重入/二次异常最多一次启动，准确定位当前可信 EXE，安全处理空格/中文参数与权限。
- 检查旧/新实例交接和有限重试，区分正常/主动/自动重启，用户取消不再拉起。硬终止/FailFast/断电不承诺普通异常处理。
- 已提交 UInk/索引持久化以原子替换和最后有效恢复点为边界；半写入、跨 workspace/文稿/页串用属于数据损坏风险。
- 将“已提交文件可读且页身份正确”和“新进程实际把内容恢复到可见画布”分为两条链。现有 Desktop SubmitLoad 仅发现进程内 Clear 撤销入口、跨进程 PPT 恢复标未开放；先核当前正式支持范围，不用编译通过或文件存在推断自动可见恢复。
- headless 或静态可先验证合同；真实 GUI 崩溃注入需用户额外授权，使用独立测试数据/进程并与外部强杀分开。
- F-029 正常退出的 Desktop 源选择采用显式 active/parked slot，快照读取同一文档/history/page 身份并走原 Desktop policy 与 worker；不在 PPT 切换点新增同步保存。见 `research/parked-desktop-exit-design.md`，此项优先于故障恢复，因为无需图形故障就可达。
- F-026/F-027 的图形故障链必须将 Host fatal、窗口配对隐藏、输入门禁与受控停止协调起来；仅报告 `ProductRunning=false` 不能消除主循环仍存活、RTS 继续发布或旧可见性回调重显。保存已完成笔迹须在 controller 销毁前排队，活动 contact 的 CPU 封口与跨 HWND 无损恢复是独立、更高风险的合同，不以单一 fallback 布尔值冒充成功。见 `research/draw3-fatal-exit-design.md`。
- F-031 的 PPT 退出/迟到 completion 保存请求必须将 retained map 与文档、history、target 作为同一 slot 的值源；当前 builder 捕获 active retained map 的历史错误可能跨文稿串页。先生产 builder 红测后显式传源，不能仅给 Exit 分支加特判。见 `research/parked-ppt-retained-identity-design.md`。
- F-038 的活动 Presentation 加载安装必须把 materialize 得到的 retained map 与 document/history/fileGuid/revisions 一起迁入活动槽；新 mutation 拒绝安装时不得覆盖现有用户状态。见 `research/ppt-loaded-retained-install-design.md`。
- F-039 同文稿异步冷加载期间 StableSlideId 拓扑删除/重排必须把旧 active 中不再属于最新目标的普通页按原身份保留为 retained；EndScreen、已有 retained 冲突和迟到 completion 的新 mutation 门分别处理，不能把旧页静默丢弃。见 `research/ppt-pending-topology-load-design.md`。
- 用户新增的画布卡死报告须先区分 Draw3 线程还活着但被锁/等待/重试困住，与已退出但全局 offSignal 未观察；前者现有 `ProductRunning()` 轮询不能发现。所有可见性撤销由 Window Service owner 完成，不能把主栏/设置仍能渲染视为 Draw3 健康。15 秒兜底必须在请求被正式接受时启动、由独立 owner 驱动，覆盖 `jthread::join`/WindowService submit/COM/持久化 drain 卡住而主线程无法前进；旧 UInk 最后有效点与强制终止的未保存数据分别声明，不能在卡死线程执行 watchdog。
