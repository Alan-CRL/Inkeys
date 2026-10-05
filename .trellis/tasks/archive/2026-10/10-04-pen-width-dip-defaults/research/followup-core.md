# 本轮启动恢复修补

- 基线：`79e5fe0835b39c482cd7c87724c4a8ffa21e2ed8`，`chore/publish`。
- Role：已派发 `trellis-implement`，未递归派代理、未构建、未启动GUI、未修改任务状态。
- 已读取完整native hook保存文件及当前task context；本轮`followup-scope.md`覆盖旧设计的恢复优先级。

## 生产链证据与修改

修补前：`IdtMain.cpp`在显示/配置就绪后调用`InitializePenWidthPreferences`；该函数将`DecodePreferences`的MemoryWidth/MemoryColor/AutoDefaultWidth写入旧runtime字段，然后调用`GetMemory`。`GetMemory`读取memory.json并通过`RestorePenWidthSelections(StartupSelection(...))`应用宽度及颜色。旧Draw2另有调用，但未编译。

修补后：`InitializePenWidthPreferences`保留`ApplyPenWidthContext`，明确注释上述解析、flags应用及`GetMemory`调用，本版只在首次初始化调用`RestorePenWidthSelections({}, {}, {})`。三槽共同Medium策略，激光5DIP；一次性guard使后续调用只更新Context，不覆盖手动值或预设身份。未修改Draw3文档/uink历史恢复。

`CaptureSettingJson`只为保存兼容数据，从原JSON解码后编码，不应用runtime。它保留Legacy、已有Startup及未知future schema，避免禁用启动decoder后把旧信息覆盖为默认；AutoClean和既有atomic保存合同保持。`GetMemory`兼容实现及`SetMemory`安全保存保留，本版启动入口不调用恢复函数。

## 真实生产probe调整

`RunPenWidthPreferencesProductionTest`改为当前产品合同：

- 新配置首笔与点击Medium相同。
- 同时存在旧Preset默认、启用记忆/颜色和旧memory宽色：仍Medium、颜色不恢复；初始化不改memory文件。
- 保存仅保留兼容迁移证据；不是本版支持启动恢复的断言。
- 新PenWidth.Startup和memory.Selections同时残留、memory开关true/false：均Medium；原版本化Startup保存不漂移。
- 大屏1440p275%推荐Medium；同进程重复初始化/改DPI保留ExplicitDip，改目标分辨率保留Thick身份并重解析。
- 原atomic失败、futureAutoClean及坏memory不覆盖测试继续保留。

## 已执行检查与限制

- `git diff --check -- Inkeys/IdtConfiguration.cpp Inkeys/IdtConfiguration.h`：退出0。
- 用Python逐字节比对HEAD BOM和换行：两个文件UTF-8 BOM属性不变，原CRLF不变。
- `git diff`只包含启动入口、保存数据冻结及对应生产probe的必要变化。
- 未运行编译或生产probe：按主会话所有权等待统一Solution构建及`Inkeys.exe --pen-width-production-test`。源码准备就绪不是运行PASS。
- `Business/PenWidthPreferences.hpp`无需改动；其纯compatibility codec测试保留，不把其旧优先级helper等同当前生产恢复支持。
