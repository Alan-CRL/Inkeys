# 显示物理尺寸获取与有效性判定技术设计

## Public Contract

`Inkeys.Display` 增加 `DisplayTopology`、`EdidStatus`、`PhysicalSizeUnavailableReason`、`ActiveDisplayTargetInfo` 和 `PhysicalSizeInfo`。

- `EdidInfo` 表示原始设备事实：状态、设备路径、实例 ID、原始字节、EDID 版本和未旋转的厘米尺寸。保留 `valid` 作为“原始 EDID 已成功解析”的兼容字段，但不再代表业务可用。
- `PhysicalSizeInfo` 表示业务事实：`available`、旋转后的 `widthCm/heightCm` 和失效原因。不可用时宽高固定为零。
- `Snapshot` 保存 `topology` 和每条活动 target；`MonitorInfo` 保存唯一 target 索引、精确匹配后的原始 `edid` 副本和业务 `physicalSize`。
- `ClassifyTopology` 与 `ResolvePhysicalSize` 是无硬件副作用的纯入口，供快照构造和 headless 测试共用。

失效原因固定为：`None`、`SnapshotFallback`、`TopologyUnknown`、`CloneOrMixed`、`DisplayTargetAmbiguous`、`EdidUnavailable`、`EdidReadFailed`、`EdidParseFailed`、`MissingDimensions`、`DimensionsBelowMinimum`。

## Enumeration And Mapping

枚举先使用现有 `EnumDisplayMonitors` 构造逻辑几何、DPI和方向，再用 `GetDisplayConfigBufferSizes/QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)` 取得活动路径。`ERROR_INSUFFICIENT_BUFFER` 时重新取得容量并重试，避免切换过程中的数组竞争。

每条路径通过 `DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME` 获得 GDI source 名称，通过 `DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME` 获得 target 设备路径。拓扑只比较 source 的 adapter LUID 与 source ID：一条路径为单屏，多条且 source 全唯一为扩展，任何 source 重复均为复制或混合。

逻辑 `MonitorInfo::deviceName` 与 source GDI 名称做不区分大小写的完整匹配。每个逻辑屏只有恰好一个 target 时才允许物理尺寸；路径数量、source 名称或匹配基数异常均进入保守失效。

EDID 读取使用 `GUID_DEVINTERFACE_MONITOR`、`SetupDiGetClassDevs`、`SetupDiEnumDeviceInterfaces` 和 `SetupDiGetDeviceInterfaceDetail` 枚举当前 monitor interface，完整匹配 target `monitorDevicePath`，再以 `SetupDiOpenDevRegKey(DICS_FLAG_GLOBAL, DIREG_DEV)` 读取该实例 `EDID`。所有 SetupAPI、注册表和临时缓冲区使用局部 RAII 清理。

## Validation Policy

解析始终保存已读取原始字节。基础块至少 128 字节，8 字节头和第一个 128 字节块校验和必须有效；否则 `EdidParseFailed`。成功解析后保留字节 21/22 的原始厘米值。

业务判定按以下优先级执行：fallback、拓扑未知、存在复制、target 映射不唯一、EDID 未获取/读取/解析失败、尺寸为零、任一边小于 5 cm、可用。只有最后一种情况按 90/270 度方向交换业务宽高。

纯扩展逐屏判定：某个 target 的 EDID 失败只影响关联逻辑屏。复制或部分复制是全局拓扑禁用，原始 target EDID 仍保留在快照用于诊断。

## Publication And Failure

新字段全部进入 `SemanticallyEqual`。现有 `Refresh` 锁、原子快照、串行 publication 队列和 RAII 订阅协议不变。

逻辑显示器枚举成功但 DisplayConfig/EDID 失败时仍发布完整像素/DPI快照，并携带保守失效原因。后续逻辑枚举完全失败时复制上一代几何/DPI和原始诊断数据，将拓扑改为 `Unknown`、清空所有业务尺寸并发布一次失效 generation；重复同类失败不重复通知。首次失败继续发布现有 `fallback`。

## Existing Consumers And Compatibility

停留拉直、触控设备分类和硬件信息只读取 `PhysicalSizeInfo`；不可用时沿用各自现有无尺寸分支。公式、面积阈值和设置行为不变。

设置页现有诊断文本增加拓扑、逐 target EDID 状态/原始尺寸，以及主逻辑屏业务可用状态/原因。模块不引入 UI 文本依赖。

QueryDisplayConfig 与目标名称接口从 Windows 7 起可用；SetupAPI 更早可用。工程显式链接 `Setupapi.lib`，不改变当前 DPI awareness、绘制线程或窗口生命周期。
