# 笔迹文档结构与抬笔接入执行计划

## 1. CPU Model

- [x] 新增 renderer-independent C++20 ink document module：GUID、DeviceKey、viewport、Stored point/style、Stroke、Canvas、Page、Collection。
- [x] 实现只读遍历、按 Device 查找/创建 Canvas、Stroke 追加和 Page 追加接口。
- [x] 增加模型不变量、无限坐标、顺序和多 Device Canvas 单元测试。

## 2. Completion Adapter

- [x] 从 ActiveStroke 生成不含 prediction/time 的最终 Pen/Highlighter/Eraser Stroke。
- [x] Pen 合并稳定前缀与真实 taper 尾段并去重连接点，统一把半径转换为直径。
- [x] 实现 `DrawStoredStroke`，复用现有 renderer/geometry 路径。
- [x] 增加 finalization、单点和非法 Stored 类型测试；Cancelled/Laser exclusion 与首次 draw 映射由 Controller 静态审查确认。

## 3. DrawingController Integration

- [x] 绘制线程初始化 Collection、第一页、默认 Canvas 和 current page index。
- [x] completed Stroke 先 append，再逐笔调用 `DrawStoredStroke` 并 resolve 到 L2。
- [x] 移除 completed persistence 对 `retainPredictionOnUp` 的依赖。
- [x] 把用户 clear request 改成 deferred append-and-select page；保留启动透明清屏语义。
- [x] 更新与“同帧只 resolve 一次”冲突的 native runtime spec。

## 4. Validation

- [x] 运行 ARM64 Debug tests。
- [x] 运行 ARM64 Release tests。
- [x] 使用 ARM64 MSBuild 完整构建 `inkStrokeModelerTest.sln` 的 `Debug|ARM64` 与 `Release|ARM64`，单次超时至少 5 分钟。
- [x] 执行 `git diff --check`、Trellis spec/scope review，并确认 `Vcpkg/` 未被修改。

验证结果：Debug/Release 完整解决方案构建成功，两套测试均通过；实体绘制、活动 contact 新建页与 D3D Debug Layer 留作人工验证。

## Out Of Scope

- UInk、history、翻页 UI、多屏运行时、无限画布显示和多图层。
