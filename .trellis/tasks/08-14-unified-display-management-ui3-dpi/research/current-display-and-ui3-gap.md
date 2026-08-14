# 现有显示管理与 UI3 缺口

## 当前事实

- `IdtDisplayManagement.cpp` 已枚举全部活动显示器并从注册表读取 EDID，但通过多个独立原子字段暴露主屏状态，消费者容易混用不同刷新时刻的数据。
- DisplayObserver 已接收 `WM_DISPLAYCHANGE`/`WM_DEVICECHANGE`，但刷新后主要只更新旧全局和绘图参数。
- PPT UI3 窗口过程已监听 `WM_DPICHANGED`、`WM_DISPLAYCHANGE`、`WM_SETTINGCHANGE`；其布局范围仍来自 Drawpad HWND，而 Drawpad 的主屏范围在启动阶段确定，因此消息监听不能保证分辨率变化后的正确重排。
- PPT 的越界约束只在主动拖动中执行，显示变化不会约束已保存偏移；当前 DPI 变化还会立即替换 backing size，没有尺寸/位置的统一过渡。
- 旧版 PPT 控件循环持续读取 `MainMonitor` 宽高并重算目标，所以屏幕尺寸改变后会自然获得新目标。
- Bar 的 `dpiZoom` 和 `barWindow.w/h` 只在初始化阶段确定；WndProc 没有显示/DPI 变化入口，直接拖动约束也使用启动时范围。

## 相关规范

- `.trellis/spec/native-desktop/index.md`
- `.trellis/spec/native-desktop/rendering-and-ui.md`
- `.trellis/spec/native-desktop/directory-structure.md`
- `.trellis/spec/native-desktop/cpp-conventions.md`
- `.trellis/spec/native-desktop/build-and-compatibility.md`
- `.trellis/spec/ppt-interop/index.md`

## 已确定边界

- 动态 DPI 仅覆盖 PPT 和 Bar；Setting 只迁移信息读取。
- 所有 UI 继续定位到主屏；未来 Draw3、多屏绘图和物理距离业务接口另行设计。
- 过渡统一为 0.4 秒，位置和尺寸同时动画。
