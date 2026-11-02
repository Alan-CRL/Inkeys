# Implement

1. i18n：zh-CN 新增 `SettingsUI.Linkage.{N,PPT,CoreVersion}`、`SettingsUI.PlugIn.PPTHelper.WidgetDisplay.{IdleFade,IdleFadeE,IdleFadeOff,IdleFadeSeconds}`；补齐 en-US / zh-TW；`Scripts/i18n.ps1 sync` 生成 `IdtI18nKeys.g.h`，`check` 通过。
2. 配置：`Other.Config.cppm` PPTHelper 组新增 `IdtAtomic<int> IdleFadeSeconds = 10`。
3. Ppt：`ConfigGroup::IdleFade`；`PublishSnapshot` 写入 `state.idleFadeSeconds`。
4. PageControl.cppm：`PptState.idleFadeSeconds`、等价比较、纯策略函数。
5. PageControl.cpp：原子信号、输入钩点、`InvokeDirection` 标记、`PublishPptState` 外部翻页检测、`ConfigureSurface` 透明度决策、`PageControlInputTick` 唤醒、生命周期清理；离屏测试补纯策略用例。
6. Setting：PageId/导航/联动页迁移、详情页“核心版本”、下拉框。
7. 验证：`Scripts/i18n.ps1 check`；MSBuild `InkeysRepo.sln` Debug|x64（本机原生架构）；`InkeysHeadlessTests.exe --no-window`。

回滚点：每步独立，UI 迁移（6）与淡化（2-5）互不依赖。
