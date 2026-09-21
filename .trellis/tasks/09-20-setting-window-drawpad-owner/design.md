# 设置页整理设计

## Change Boundary

- 行为差距：当前设置页仍展示多项只服务旧 Draw2 或未实现功能的条目，部分卡片顺序、文案、字体和高度与现有产品行为不一致，旧 JSON 也会保留已删除字段。
- 实际归属：界面布局位于 Setting 渲染代码；传统配置清理位于 `IdtConfiguration` 的读取/捕获路径；UI3 动画与边缘光影继续使用 `Inkeys.Other.Config`；文案沿用既有 i18n 生成链。
- 必要修改：Setting 页面布局和局部状态、传统/PPT 配置序列化、三种语言资源、生成键头文件及当前任务文档。
- 明确不做：不修改工程中 `None` 的 Draw2 源文件，不迁移两套配置体系，不改 Setting 的渲染/线程/窗口合同，不处理无关硬编码页面。

## Decisions

- `Save.Enable` 继续控制 Draw3 桌面画布自动保存，只更新标题与说明。
- 删除项在读取端停止兼容、写入端停止输出；传统 `deploy.json` 保存前无条件移除遗留键，PPT 配置通过重建输出对象自然丢弃 `FixedHandWriting`。
- 避免全屏不再可配置，已编译的冻结/放大路径固定使用显示器高度减 1 像素。
- 卡片高度由实际剩余行数和统一常量计算，避免继续维护与内容脱节的魔法总高度。
- 只国际化本轮新增或变更的区块；不可见 ImGui ID 保持稳定，不作为用户文案翻译。
- 外观页把动画速率和动态边缘光影视为各自总开关的条件子卡片；隐藏只影响渲染和容器高度，不写回或重置 `SpeedRate` / `EdgeLighting.Dynamic`。

## Setting Owner 收敛补充

- `SyncDraw3State()` 是模式事实源，先发布最新的 owned/unowned 期望值，再尝试通过 Window Service 应用。
- 提交失败时保留 desired/applied 版本差异作为独立 retry pending，由既有 `StateMonitoring()` 250ms 节拍重试；不把 owner 重试耦合到 Draw3 surface revision。
- 同步提交期间模式可能再次变化，因此完成后必须重新比较最新期望值；旧请求即使成功，也不能清除新目标的重试状态。
- Window Service 继续负责 HWND 所属线程、回滚和去重失败日志；本补充不增加窗口样式或公共 API。

## 双画布 Owner 链收敛

- 旧结构中 DrawpadPresentation 和 Drawpad 同为 Freeze 的 owned popup，Bar/PPT 只归属 Drawpad；因此选择态显示 DrawpadPresentation 时，Owner 关系无法保证 Bar 位于它上方。
- 基础链收敛为 `MagnifierHost -> Freeze -> DrawpadPresentation -> Drawpad -> Bar/PPT`；绘制模式下 Setting 仍以 Drawpad 为 owner，选择模式下解除 owner。
- Drawpad 只设置 `GW_OWNER`，仍保持顶层 `WS_POPUP`；不引入 `WS_CHILD` 的坐标、剪裁、激活或输入语义。
- 静态分组创建和动态创建必须共用同一 Owner 拓扑；反向销毁顺序继续为 Bar/PPT、Drawpad、DrawpadPresentation、Freeze。
- 不新增表面切换时的 `SetWindowPos` 重排命令；通过 Owner 传递关系统一保证 Bar/PPT/owned Setting 高于两套画布表面。
