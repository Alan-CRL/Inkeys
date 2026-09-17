# 显示物理尺寸获取与有效性判定

## Goal

让 `Inkeys.Display` 能可靠识别真实活动拓扑、将每个活动输出 target 的 EDID 与逻辑显示器正确关联，并向业务明确提供物理尺寸是否可用及其失效原因。

## Background

- 当前模块用 `EnumDisplayMonitors` 枚举逻辑屏幕，并通过 `EnumDisplayDevices + 注册表 Driver` 间接寻找 EDID。
- 当前 `EdidInfo::valid` 只检查头、最短 23 字节和非零尺寸，业务直接读取旋转后的 `edid.physicalWidthCm/physicalHeightCm`。
- 当前实现无法识别复制或部分复制拓扑，也无法保留同一逻辑 source 后多个物理 target 的原始 EDID。
- 现有不可变快照、generation、订阅队列和 DisplayObserver 消息刷新机制继续作为唯一显示信息来源。

## Requirements

- 使用 `QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)` 获取实际活动路径，以 `(adapterId, sourceId)` 的复用关系识别复制；不得用逻辑屏幕数量或分辨率相同判断复制。
- 一个 source 关联多个 target 时，无论是否还存在独立扩展 target，都将拓扑标为 `CloneOrMixed`，并对所有逻辑屏禁用物理标尺。
- 真正单屏要求一个活动 target 且与一个逻辑显示器唯一匹配；纯扩展要求所有活动 source 唯一。纯扩展下逐屏判断，单屏 EDID 无效不影响其他可靠屏。
- 使用 DisplayConfig source GDI 名称关联 `MONITORINFOEX::szDevice`，使用 target `monitorDevicePath` 精确关联 SetupAPI 监视器实例并读取该实例的 EDID。
- DisplayConfig 查询失败、source/target 信息缺失、路径与逻辑屏数量不一致、映射缺失或不唯一时保守禁用相关物理标尺。
- 原始 EDID 与业务物理尺寸分离：保留设备路径、实例 ID、原始字节、版本和未旋转厘米值；业务尺寸另含旋转后的宽高、可用标志和稳定失效枚举。
- EDID 基础块不足 128 字节、头错误或校验和错误为解析失败；尺寸为零为缺失，任一边小于 5 cm 为过小，恰好 5 cm 可用。
- 物理尺寸无效时业务宽高保持为零，不以 DPI 或系统指标反推；像素范围、工作区、DPI、方向等快照字段继续正常提供。
- 拓扑、活动 target、EDID 状态/内容、映射和物理可用状态都参与快照语义比较；插拔、复制/扩展切换和旋转后必须重新发布变化。
- 后续完整枚举失败时保留上一代几何/DPI兼容信息，但发布物理标尺失效状态，不能沿用旧的有效状态。
- 迁移现有停留拉直、触控设备分类、硬件信息和设置诊断到新业务尺寸接口，不改变现有公式及阈值。
- 保持 Windows 7 API 可用范围、现有线程和订阅生命周期以及首次 fallback 行为兼容，并添加必要的简短中文注释。

## Acceptance Criteria

- [ ] 纯策略测试覆盖单屏、等分辨率纯扩展、复制、部分复制加扩展、未知拓扑及映射歧义。
- [ ] 纯扩展中一屏有效、一屏 EDID 失败时，仅失败屏不可用。
- [ ] 所有复制/混合拓扑的逻辑屏均返回 `CloneOrMixed` 失效原因，即使各 target 原始 EDID 正常。
- [ ] EDID 测试覆盖读取失败、短数据、错误头、校验失败、零尺寸、4 cm、5 cm 和正常尺寸，并保留读取到的原始字节。
- [ ] 90/270 度只交换业务宽高，不改变原始 EDID 尺寸。
- [ ] 拓扑或物理状态变化推进 generation 并通知订阅者；等价刷新不推进 generation。
- [ ] 全部现有物理尺寸消费者检查 `available`，仓库不再直接把原始 EDID 尺寸用于业务计算。
- [ ] `InkeysHeadlessTests.exe --no-window`、`InkeysRepo.sln Debug|ARM64`、`git diff --check` 和目标文件编码/EOL检查通过。
- [ ] 实机验收清单覆盖插拔、旋转、同型号多屏、同分辨率扩展、复制及部分复制；未由代理执行的项目如实记录。

## Out of Scope

- 不修改 Draw3 笔速橡皮、手掌橡皮、GPU/HLSL、UInk 文档或输入算法。
- 不新增可见 UI；设置页只扩充现有诊断文本。
- 不使用代理切换本机显示拓扑或启动可见窗口。
