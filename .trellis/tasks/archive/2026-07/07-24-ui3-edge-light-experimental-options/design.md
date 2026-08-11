# UI3 边缘光影实验开关技术设计

## Configuration

- 在 `INKEYS_CONFIG_SCHEMA` 的 `Experimental.Inkeys3.UI3` 下新增 `EdgeLighting` 组，包含原子布尔值 `Enable` 与 `Dynamic`，默认均为 `true`。
- 复用 schema 驱动的 `ReadAll`、`Write`、复制与默认值逻辑；`Other.Config.cpp` 无需增加针对字段的分支。

## Settings UI

- `Setting.cpp` 的本地快照从 `Inkeys::config` 初始化两个布尔值。
- 在 UI3 动画和速度卡片之后加入总开关卡；仅当总开关为真时绘制动态开关卡。
- Inkeys3 子容器高度按 UI3、总开关状态计算，避免条目裁切。
- 每次开关变化先更新原子配置，再调用 Bar 的运行时入口，最后 `config.Write()`。

## Runtime Contract

- 新增 `SetEdgeLightingOptions(bool enable, bool dynamic)` 作为 Setting 与 Bar 的唯一运行时边界。
- Bar 使用原子快照保存总开关与动态开关，避免设置线程、窗口线程和渲染线程之间的数据竞争。
- 总开关关闭时，`DrawPointLightFrame` 仍绘制基础灰边，但跳过两束点光与 Gaussian；`PrepareFrameLighting` 停止仅用于光影的动画推进。
- 动态开关参与 `ActivateBorderCursorTracking`、帧光标可见性和绘制判定。其有效值为 `enable && dynamic`。
- 任一开关使动态光失效时，通过既有窗口消息在 Bar 线程执行 `SuspendBorderCursorTracking`；重新开启不主动获取全局鼠标，等待自然进入。
- Bar 初始化时从 `Inkeys::config` 同步开关，确保首次绘制前使用持久化值。

## Compatibility and Rollback

- 新字段默认开启，不改变已有用户和缺失字段配置的视觉。
- 回滚只需删除两个 schema 叶子、Setting 卡片与 Bar 运行时入口；不影响现有配置解析器。
