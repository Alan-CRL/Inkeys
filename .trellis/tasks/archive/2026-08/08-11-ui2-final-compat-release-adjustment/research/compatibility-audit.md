# UI2 / UI3 / Draw2 协调审计

## 审计范围

- 本版本继续同时支持 UI2 与 UI3，不移除 UI2 入口、配置或交互代码。
- Draw2、状态机、PPT 联动与组件配置需要在两套主栏下继续使用同一份业务状态。
- UI3 新增的落笔收面板行为不得回写或改变 UI2 的旧版收起设置。

## 静态结论

| 路径 | UI2 | UI3 | 结论 |
| --- | --- | --- | --- |
| 启动路由 | `IdtMain.cpp` 启动 `floating_main` | `IdtMain.cpp` 启动 `Inkeys::UI::Bar::Initialization` | 两个入口仍由 `useInkeys3UI` 互斥选择，未删除 UI2 |
| 主窗口输入 | UI2 保持系统鼠标拖窗与旧 Hook | UI3 窗口额外注册触摸并使用自身 `WM_TOUCH` 坐标 | 修复只改变 UI3 触摸手势，不改 UI2 |
| Draw2 绘制 | 继续执行原 `MultiFingerDrawing` | 执行同一 `MultiFingerDrawing`，仅由 RAII guard 额外通知 UI3 | 绘制数据面未分叉，通知只在 UI3 开启 |
| 落笔收起 | 保留 `BrushRecover`、`RubberRecover` 等旧设置 | 固定关闭绘制属性、几何、更多及其子浮层，不关闭主栏 | 两套策略互不覆盖；UI3 不提供设置项 |
| 状态切换 | 使用共享 `ChangeStateModeTo*` 与 `stateMode` | 使用同一状态函数与状态对象，UI3 仅刷新投影 | 画笔、橡皮、选择、几何业务状态一致 |
| PPT 联动 | `PPTLinkageMain` 始终启动并操作共享 Draw2/状态 | 同左 | 与主栏框架选择解耦 |
| 组件配置 | 旧配置继续写入并供 UI2 使用 | 同一旧配置经 `SyncLegacyExtensionButtons` 投影到 UI3 | UI3 刷新有运行时门禁，不破坏 UI2 持久化值 |
| 隐藏设置 | UI2 仍可见旧主题与四项收起设置 | UI3 隐藏这些未适配项 | 隐藏仅包住渲染/写入控件，不清空原配置 |
| UI3 光影 | 不显示、不写入 UI3 光影配置 | 总开关在外观页；动态光源在 UI3 与总开关同时启用时显示 | 配置依赖关系明确，不影响 UI2 |

## 关键不变量

1. `useInkeys3UI == false` 时不会调用 UI3 绘制活动通知，UI2 原收起配置仍可执行。
2. UI3 的绘制开始消息只修改 `barState` 的次级面板字段和浮层会话，不修改 `barState.fold`、`stateMode`、Draw2 笔迹或 PPT 状态。
3. 隐藏 UI2 设置不重置 `SetSkinMode`、`BrushRecover`、`RubberRecover`、`moveRecover`、`clickRecover`。
4. UI3 触摸拖动从 DOWN 到 UP 始终使用同一坐标系；鼠标路径与 UI2 的系统光标模型保持不变。
5. 本次未修改问题 2 的动画曲线或批次实现。

## 动态验证

- ARM64 Debug 全解决方案构建：通过。首次 `/m` 并行扫描模块依赖时出现一次 `CL.exe` 瞬时退出；随后使用 ARM64 主机 MSBuild `/m:1` 对完整解决方案复跑，无错误完成。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：通过，输出 `PASS animation correctness`，包含新增滚动 FPS 用例。
- GUI 手工矩阵：用户已完成 UI2/UI3、鼠标/触摸、Draw2/PPT 验证并确认通过。
