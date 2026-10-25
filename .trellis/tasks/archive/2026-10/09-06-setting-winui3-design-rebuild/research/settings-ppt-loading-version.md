# PPT 加载页禁用与组件版本（2026-10-24）

本轮为设置父任务中用户明确授权的第 2、7 项；不改变 PPT/WPS 支持范围或 COM ABI，不 GUI/Office/构建/commit/push，子代理仅修改源码。

## 已确认的活动入口

- `IdtConfiguration.h` 原已有 `kPptLoadingPageEnabled=false`；`IdtPlug-in.cpp::PPTLinkageMain` 的唯一 `FreezePPT=true` 分支已用此常量门禁。该门禁保留，不重复新增策略函数。
- 原 `PptComReadSetting` 仍可将旧 `ShowLoadingScreen=true` 读回内存，`CapturePptComSettingValue` 原样保存该字段。
- `PptSettings::WriteJournal` 的 Initialize 实际只收到 CapturePptComSettingValue，而非未解析旧 updateVal；位置单独保存与完整设置保存共用其 private Capture。
- `IdtFreezeFrame.cpp` 的加载图形、进度、计时和循环实现保留。本轮不删除其实现，不改变普通冻结功能。
- 两处设置页的 PPT 组件版本均来自真实 `pptComVersion`，该变量由 `CheckPptCom()` 读取托管 `CheckCOM()` 返回值。没有第二个活动硬编码版本标签。

## 实施

1. `PptComReadSetting()` 入口设置 showLoadingScreen=false，随后不再读取旧 key。缺文件/坏文件/错误类型/旧 true 均不能重新启用。
2. CapturePptComSettingValue 固定输出 ShowLoadingScreen=false；保留字段和 key，使功能实现/兼容结构仍在，不新增迁移架构。
3. 共同序列化边界 WriteJournal::Capture 同样规范 false，防止未来原始快照或仅位置写回重新带回 true；其位置、remember、revision、retry 和提交规则不变。
4. `PptCOMServer.CheckCOM()` 从 20260627a 改为 20261024a。AssemblyVersion/FileVersion 仍为 1.0.0.0，接口顺序/GUID/manifest/项目配置不变，避免无关 ABI 或部署格式变化。
5. UI 由主会话另一代理移除 toggle 并保留说明块，不在本子任务重复写 Setting.cpp。

## 回归与交接

- 在既有 ppt_ui_tests.cpp 增加实际 WriteJournal 流程：缺项/旧 true/旧 false/错误类型分别经过 CapturePositions 和 CaptureSettings，再通过 Prepare/Commit writer 读取真正持久化快照；确认字段为布尔 false，位置坐标、记忆位置开关、控件显示字段及未知键仍保持。
- 未新增只比较版本常量或门禁常量的镜像测试。主会话将运行现有 native/managed 测试和完整 ARM64 Solution。
- PptCOM 构建不会自动同步 native 资源 DLL/TLB；主会话须先构建托管同次产物，手动同步到 Inkeys/PptCOM.dll 与 Inkeys/PptCOM.tlb，再完整 solution 构建。实际显示应读取新 DLL 的 CheckCOM 返回值，不在 UI 硬填版本。
- 静态搜索原常量现仅在历史文档中存在；所有活动 ShowLoadingScreen 读取/捕获均明确固定禁用，运行期 gate 仍为原 k=false。源文件原 UTF-8/BOM（PptCOM.cs 为 BOM）与 CRLF 保持，git diff --check 通过。
- 子代理未运行任何构建或测试，不把新增回归源码当作已通过。真实 Office/WPS、COM 激活及 UI 可视验证本轮均未执行。
