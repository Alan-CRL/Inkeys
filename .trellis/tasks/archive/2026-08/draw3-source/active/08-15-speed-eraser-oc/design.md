# 笔速橡皮与 OC 平滑技术设计

## Mode And Ownership

新增 `EraserWidthMode { Fixed, Speed }` 作为窗口线程到绘制线程的低频原子状态，并为 `StrokeWidthMode` 追加 `SpeedEraser`。首个真实 Down 将当前模式锁入批次；同批后续 contact 继承该值，活动 Runtime 与断触候选始终使用创建时的模式。

绘制线程维护 Mouse、普通 Pen 橡皮、倒转 Pen 笔尾三条 Hover lane。Speed Down 复制对应 lane；Touch 新建最小状态。正常 Mouse/Pen Up 将状态交回 lane，断触候选则把权威状态保留在 Runtime，重连成功继续使用。

## OC Controller

`SpeedEraserOcController` 使用固定容量环形段记录最近 `64ms` 的 DIP 路程、方向、累计启动路程和状态期限，不在输入热路径分配内存。目标直径按 `0.75/3/24 DIP -> 20/50/200px` 分段线性映射，再经过 Touch 启动上限、转折保护和非对称 attack/release。

减小候选区分可确认的同向慢速与可能处于转折的近零输入。可靠反向运动取消候选并短暂保留转折前直径；保护消费后须行进 `8 DIP` 才能重装。大于常态的直径先回到 `50px`，稳定后才可跨入小尺寸，从结构上阻止高速大橡皮在单个转折点直接塌到最小。

Controller 提供 Reset、Update、Advance、PauseForReconnect 和 ResumeFromReconnect。Pause 冻结所有时间基准；Resume 平移内部 QPC 并加入旧 Up 到新 Down 的桥接 DIP 段，因此重连既不会把间隙当静止减速，也不会丢失实际位移。

## Input, Geometry And Cursor

OC 在每个原始 snapshot 进入 Stroke Modeler 前更新。新产生的 Eraser 模型点在上一次与本次 OC 直径间按模型时间插值；首点直接使用 Down 直径。Speed Eraser 与固定 Eraser 一样跳过 prediction、L0 笔锋并将真实几何增量提交 L1。

Primary Mouse/Pen 光标从对应 Hover lane或活动 Runtime 取直径；Touch 光标从每个 Runtime 取直径。现有 cursor visual 的旧/新 bounds 合并继续负责尺寸变化脏化。OC 尚未稳定时绘制线程只等待最近的下一状态期限；稳定后回到现有无限阻塞路径。

断触资格改为 Touch，或倒转 Pen 且实际工具为 Eraser。匹配身份继续包含设备、所选工具、实际工具、宽度模式、倒转与压力抑制，确保普通 Pen 不进入候选并防止多 Runtime 串用。

## Compatibility And Rollback

`InkPoint.r`、Stored Stroke、L1/L2、Undo/Redo 和 GPU/HLSL 格式保持不变。固定橡皮默认行为不变；关闭 Speed 模式即可回退新路径。OC 控制器、模式接线、Pen 断触资格和动态 Cursor 可分别回退，不需要数据迁移。

按项目约束不启动可见窗口；真实手感与 D3D Debug Layer 作为未执行的人工验证项如实报告。
