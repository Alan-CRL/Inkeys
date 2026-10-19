# 追加调整：按设备缺省分类决定任务栏缩放基准默认值

日期：2026-10-19。实施范围为开关缺省及其启动/配置验证；未修改任务栏观察、Bar、Window 或既有画布策略。

## 最终合同

- `UI.Bar.UseTaskbarHeightAsScaleBase` 保持 `IdtAtomic<bool>`、严格 JSON bool、NoUpload 和原键名；明确保存的 true/false 始终优先。
- 字段缺失、非法 bool 类型、读盘失败、构造与 Reset 共用 schema 中的冻结设备缺省：只有设备启动缺省为大屏时 true，否则 false。
- `ConfigDetail::HasReadyTouchDevice`、`ResolveDefaultLargeScreenDevice` 提取既有判定：SM_DIGITIZER 同时 READY 且 integrated/external touch；物理尺寸 unavailable 或主屏面积 >=1200cm² 时大屏。仅笔、未 READY、普通设备及可靠面积较小触控屏均非大屏。
- 不读取用户可变的 `setlist.paintDevice`/deploy.json PaintDevice 决定开关缺省；传统设备默认赋值复用同一冻结结果，随后原有用户配置覆盖仍有效。
- 默认 getter 只读 `constinit std::atomic_bool` 缓存，安全开机值 false；global config/configOnce 静态构造不查询硬件，正常启动先 seed，Mini 与随后 ReadAll 使用相同缺省。显式运行时值、复制/冻结、写盘与 NoUpload 路径保持原配置合同。

## 启动接线与边界

1. 在 Mini 前仅提前执行一次 EnsureProcessDpiAwareness 并保存 bool。原 tracker/SetActiveTracker 建立后的 fatal 处理位置、失败码 0xD001 与信息保留；若 DPI awareness 失败，不执行早期 Display 查询。
2. 读取一次 SM_DIGITIZER。只有可用触控设备在 Mini 前调用现有 Display::Initialize，同步复用正式 EDID/拓扑可靠性快照；其余设备保留原显示初始化时机。早期初始化不创建 observer HWND 或新线程。
3. 从该快照冻结主屏默认分类并 seed cache，然后构造/读取 Mini；预览与正式 Bar 对缺省开关一致。
4. 原显示初始化位置对已执行分支复用结果/快照，避免重复 Initialize/EDID；原日志及 DisplayReady milestone 保留。传统 PaintDevice/liftStraighten 默认赋值复用冻结 bool。

触控设备首次显示预览前会提前承担原有一次显示枚举耗时；并非额外枚举或零耗时承诺。无触控设备不提前读取 EDID。启动期间显示拓扑变化仍刷新 Display，但不重选当前启动已冻结的设备缺省。

## 改动文件

- Inkeys/Inkeys/Other/Other.Config.cppm：纯分类 helper、seed/getter 声明及单一 schema 默认表达式。
- Inkeys/Inkeys/Other/Other.Config.cpp：常量初始化缓存及只读/写入函数。
- Inkeys/IdtMain.cpp：一次启动分类、Mini 前触控 Display 接线、原默认赋值复用及无窗口配置边界测试两 seed 矩阵。
- InkeysHeadlessTests/config_metadata_tests.cpp：生产纯分类边界；两种冻结默认下 ctor、无文件、缺字段、非法 bool、Mini/All、Reset、显式两值、复制写回、NoUpload；用户 PaintDevice 故意与硬件缺省相反；RAII 恢复缓存及 paintDevice 防止污染后续测试。
- InkeysHeadlessTests/startup_preview_state_tests.cpp：一条旧固定 true 断言改为与冻结设备默认一致，不变更其余预览几何。

## 本代理验证

- git -c diff.ignoreSubmodules=all diff --check：通过。
- 将本代理 ownership 文件与 followup-baseline/原文件核对，既有未提交任务变更保留；所有五个源码文件保持原 BOM 状态及 CRLF，无孤立 LF。
- 读源码核对 Config ReadImpl 先 ApplyDefaults，再通过严格 bool codec 应用显式文档；ReadMini 仅重置选中路径，Reset 与默认成员都走同一 schema 表达式。
- 未启动完整构建、Headless 可执行文件或 GUI；完整 ARM64 solution、新 Headless --no-window 与新产品 --config-boundary-test 由主会话唯一执行并补写实际结果。本说明不将新增用例视为已执行通过。

## 风险与后续

- 真实触控设备预览出现前耗时、Win7/多屏实际启动时序未做 GUI/硬件验收；本轮不新增平台支持承诺。
- 未来 PC UI 再全部默认开启仅是产品方向，本轮没有新增 PC UI、样式偏好或其他迁移。
- 无 commit/push；本轮所有修改保留待主会话整体验证与用户验收。
