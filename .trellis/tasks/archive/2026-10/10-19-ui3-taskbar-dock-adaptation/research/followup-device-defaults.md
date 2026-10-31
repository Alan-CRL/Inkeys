# Research: 按设备缺省分类选择任务栏缩放基准默认值

- Query: 用户要求 `UI.Bar.UseTaskbarHeightAsScaleBase` 在字段缺省时仅对“设备缺省为大屏”的机器默认开启。查明现有设备缺省判定、Mini/完整读取先后关系及不引入静态硬件初始化的最小正确接线。
- Scope: internal
- Date: 2026-10-19

## Findings

### 1. 设备缺省不是保存后的 PaintDevice

- `Inkeys/IdtMain.cpp:2706–2716` 是当前唯一启动默认判定：`SM_DIGITIZER` 同时含 `NID_READY` 与 integrated/external touch 任一位，才认为存在可用触控设备。无此能力时 `paintDevice=1`（普通设备）。
- 有上述触控能力时，取 Display 主屏的 `physicalSize`：主屏/可用物理尺寸缺失，或 `widthCm * heightCm >= 1200`，则 `paintDevice=0`（大屏），同时开启 `liftStraighten`；面积小于 1200 则 `paintDevice=1`。阈值等号必须保留。
- `Inkeys/IdtMain.cpp:2740` 开始新配置 `ReadAll`；`Inkeys/IdtMain.cpp:2822` 附近随后才调用传统 `ReadSetting()`。后者在 `Inkeys/IdtConfiguration.cpp:289–290` 用 `opt/deploy.json` 保存的 `PaintDevice` 覆盖运行时 `setlist.paintDevice`。设置页还能修改此值，白板流程也可切到大屏。
- 因此新开关缺省必须依赖启动硬件分类的独立冻结结果，不能读当前 `setlist.paintDevice`、不能反读旧配置 `PaintDevice`，也不能随设置页的绘图设备调整而重选默认值。

### 2. Mini 阶段先于物理信息就绪

- `Inkeys/IdtMain.cpp:2268–2271` 构造 `startupMiniConfig` 并 `ReadMini`（包括本开关）；预览在 `2335` 用它选择缩放基准。
- `Inkeys/IdtMain.cpp:2545` 才调用 `Display::Initialize()`；设备默认分类在 `2706–2716`，完整配置在 `2740`。只在现有分类处更新缺省值会让预览 Mini 与正式 Bar 对缺省/坏类型开关得出不同结果。
- `Display::QueryTaskbarMonitorInfo`、`QueryUiScaleBase`、`QueryEffectiveDpi` 的公开合同分别在 `Inkeys/Inkeys/Display/Display.cppm:221–234`：不枚举 EDID，不发布正式快照。`QueryTaskbarMonitorInfo` 实现在 `Display.cpp:1286–1306`，只建几何、DPI 和任务栏样本，`physicalSize` 保持未知，不能用于此默认判定。

### 3. 没有现成的轻量、等价主屏物理查询

- `Display.cpp:191–265 ReadMonitorEdid` 是内部 helper；它须先取得活动 target 的 monitor device path，再通过 SetupAPI/注册表读取 EDID。
- `Display.cpp:268–320 QueryActiveDisplayTargets` 枚举活动 DisplayConfig 路径，并对目标读 EDID；`Display.cpp:581–654 EnumerateSnapshot` 建立 target/source/monitor 映射、拓扑及整个映射集的可靠性，最后才计算 `physicalSize`。
- `Display.cpp:1003–1048 ResolvePhysicalSize` 除了 EDID 内容，还依赖 `fallback/topology/uniqueTarget`。Fallback、Unknown、CloneOrMixed、映射不唯一、EDID 失败/缺失、边长不足 5cm 等均使物理尺寸 unavailable。
- 因而用 `GetDeviceCaps(HORZSIZE/VERTSIZE)`、分辨率、DPI、对角线阈值或单屏裸 EDID 直接替换，会改变当前默认设备分类，尤其会把既有“触控但尺寸 unavailable → 大屏”改成不同结果。新增针对单屏的等价查询仍需拓扑及映射检查，工作量和风险超过本次小调整。

### 4. 推荐最小接线

1. 在正常启动的 `startupMiniConfig` 构造/读取前查询一次 `SM_DIGITIZER`。非 READY touch 直接冻结默认普通设备，不提前初始化显示信息。
2. 仅 READY integrated/external touch 设备提前执行一次现有 `Display::Initialize()`，保存其调用/成功结果，并从同一次正式快照取得主屏 `physicalSize`，按既有规则冻结 `defaultLargeScreenDevice`。初始化失败按现有 unavailable 规则保持大屏缺省，不用第二套猜测。
3. 原 `2545` 显示初始化位置若已执行则复用结果与快照，不重复全量枚举；保留该处日志和 `DisplayReady` milestone 报告时序。非触控机器继续在原位置初始化。此方案提前的是现有显示枚举，而非初始化第二套 observer。
4. `2706–2716` 的传统默认赋值复用冻结分类：`paintDevice = defaultLargeScreenDevice ? 0 : 1`，仅大屏时沿现有语义开启 `liftStraighten`。稍后的旧配置覆盖继续有效，但不影响开关默认来源。不要保留第二次独立判定而让热插拔/暂时查询失败造成 Mini/完整读取分叉。
5. 为 Config 提供常量初始化、只读缓存的缺省 getter 与启动 seed；schema 原 `true` 换成此 getter。同一表达式已贯穿默认成员构造、`ApplyDefaults` 和 `ResetToDefaults`。getter 不调用 Win32 硬件 API、不初始化 Display，也不引用 `setlist.paintDevice`。缓存开机安全值可为 `false`，正常启动先 seed、再构造 Mini；静态全局 `config/configOnce` 先安全构造，正式消费前 `ReadAll` 会重新应用已 seed 默认。
6. 类型保持 `IdtAtomic<bool>`、`NoUpload`。显式 JSON bool 始终覆盖当前机器缺省；缺失/非法值回到同一已冻结缺省。复制/冻结继续复制用户值，不改为 H 来源，也不每次输出时重算硬件。

`Display::Initialize()` 在 `Display.cpp:1317–1324` 仅重置 shutdown 状态并调用同步 `Refresh(Startup)`；未使用 COM，也不创建 observer HWND。observer 的定时器/WinEvent 安装从 `DisplayWindowProc(WM_CREATE)` 开始（`Display.cpp:889` 附近），产品在 `IdtMain.cpp:3296` 将该过程交给 Window Service。提前枚举会发布快照和缓存任务栏 owner，但不会提前出现 observer/UI 窗口。

### 5. Config 本身已集中处理缺省

- `Other.Config.cppm:326` 是现有开关 schema；`413` 的声明宏使用 `defaultValue` 构造成员，`446` 的遍历宏构造相同默认值交给 handler。只需同一 schema getter，不应在 Mini、ReadAll 或设置页各补一遍判断。
- `Other.Config.cpp:719–740` 的 `ReadAll/ReadMini` 都经 `ReadImpl`；`824–839` 先 `ApplyDefaults(paths)`，再加载并应用文档。`DefaultValueHandler:498–530` 仅重置选中路径，`ReadDocumentHandler:533–605` 只接受 codec 成功的值，非法 bool 保留刚应用的缺省。`818–821 ResetToDefaults` 也直接走 `ApplyDefaults`。
- `Other.Config.cppm:488–489` 的全局 `config/configOnce` 会在进入主函数前构造。因此绝不能把硬件查询塞进 schema 默认表达式或依赖不确定顺序的动态全局初始化。

### 6. 验证入口与应补的矩阵

- `InkeysHeadlessTests/config_metadata_tests.cpp:329–412` 已覆盖开关 ctor、ReadAll、Mini、非法 bool、冻结复制、显式两值、NoUpload、Reset；其中固定认为缺省 true 的断言需要按 seed 两值矩阵调整。测试须恢复缓存，以免污染随后 startup fixture。
- `InkeysHeadlessTests/startup_preview_state_tests.cpp:99–113` 的缺省 fixture 读取本开关；`162` 的显式 false 用例可继续验证覆盖。检查后续旧固定 true 断言，避免把当前测试机器当所有设备的默认。
- `Inkeys/IdtMain.cpp:524–725 RunConfigBoundaryTest` 使用隔离 JSON 真正走生产 Config；入口在 `1424–1425` 早于正常启动/bootstrap。不能期待该入口触发硬件 seed，测试须自行设置并恢复两个缺省，以验证生产 module 的行为。此入口本身不创建 HWND。
- 分类纯逻辑应覆盖：无触控、未 READY、仅笔能力、READY external/integrated touch；物理 unavailable；面积低于/等于/高于 1200；已保存相反 `PaintDevice` 不影响开关缺省。Config 两 seed 下均验证 ctor、缺字段/坏类型 Mini/All、无文件、显式 true/false、Reset、复制冻结、NoUpload、与预览一致。
- 本 research 未执行构建或 GUI，也未编辑产品源码/测试。

## Files Found

| Path | Purpose |
| --- | --- |
| `Inkeys/IdtMain.cpp` | Mini、显示初始化、默认绘图设备分类、全量与旧配置读取及无窗口边界入口 |
| `Inkeys/IdtConfiguration.cpp` | 旧 PaintDevice 用户覆盖读取 |
| `Inkeys/Inkeys/Display/Display.cppm` | 几何/任务栏 Mini 查询与正式显示快照公开合同 |
| `Inkeys/Inkeys/Display/Display.cpp` | EDID/拓扑/物理可靠性、显示初始化及 observer 安装边界 |
| `Inkeys/Inkeys/Other/Other.Config.cppm` | schema、默认构造/遍历、全局 config 和复制 |
| `Inkeys/Inkeys/Other/Other.Config.cpp` | Mini/All/Reset、严格 bool codec 及路径过滤 |
| `InkeysHeadlessTests/config_metadata_tests.cpp` | 真实 schema、读写、冻结副本与缺省回归 |
| `InkeysHeadlessTests/startup_preview_state_tests.cpp` | 启动 Mini 与预览配置测试 |

## Related Specs

- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：配置归属、严格 bool、Mini/全量/Reset/复制和 NoUpload；当前任务栏基准默认 true 条款需由主会话按新需求更新。
- `.trellis/spec/native-desktop/startup-preview.md`：Mini 冻结及原“不枚举 EDID”启动合同；触控机器在 Mini 前提前正式枚举的必要例外需明确记录，不能继续声称所有正常启动均不预读物理信息。
- `.trellis/spec/native-desktop/cpp-conventions.md:95–111`：物理信息只由 Display Refresh 枚举发布，输入/渲染路径只读快照，Clone/Unknown 等可靠性语义。
- `.trellis/spec/native-desktop/pen-width.md`：paintDevice0 大屏/1 普通设备既有语义；本次不改变笔宽算法或设备偏好覆盖。

## External References

- None required: conclusion is based on current repository startup/configuration contracts. No new OS API or compatibility promise is proposed.

## Caveats / Not Found

- 提前 Display 枚举是保证触控小屏 Mini/正式配置缺省一致所需的启动时序调整；可能增加触控设备的预览出现前时间，不能称为完全零启动影响。非触控设备不需提前 EDID。
- 没有可在保持既有物理可靠性语义下直接使用的公开“只查主屏尺寸”helper。新建此接口并缓存拓扑比复用一次 Initialize 改动更大。
- 启动分类冻结意味着本次启动期间热插拔不重新决定开关的缺省；显式持久化开关仍按用户设置。此处冻结的是启动默认身份，不是永久冻结 Display 几何更新。
- Win7/多屏/真实触控硬件时序及预览感知耗时尚无实机证据；本研究仅确认源代码与无窗口测试接线。
