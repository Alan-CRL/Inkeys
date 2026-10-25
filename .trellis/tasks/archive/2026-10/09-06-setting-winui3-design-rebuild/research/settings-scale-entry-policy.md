# 设置缩放与开屏动画策略（2026-10-24）

用户明确要求沿用父任务并批准直接实现；不新建任务、不 GUI/commit/push，不结束或归档父任务。保留此前四个插件 PNG 改动及现有两张 CSO 字节。

## 要求

1. 移除 SettingUI 用户缩放选项/配置，用户倍率恒为 100%，有效倍率仅由当前窗口的系统 DPI 决定；主栏缩放不变。
2. 在“显示启动动画”卡片下方新增“显示设置界面开屏动画”下拉卡片，选项为关闭/首次启动时/始终，默认首次启动时。
3. 首次启动时指同进程第一次真正隐藏→显示；即使首次尺寸不满足、总动画关闭或输入取消，后续打开也不补播。再次激活不算新显示；始终仍遵守既有尺寸与总动画约束。
4. 开屏模式只控制现有开屏，不关闭主页其他微动效，也不影响应用启动动画。
5. 配置沿用 SettingDarkMode 所在的 SetListStruct/deploy.json，新增 SettingEntryAnimation=1；旧 SettingGlobalScale 读取忽略、下次保存显式删除（AutoClean=false 也清理）。无效模式回退默认1。

## 实施与验证

- 配置/UI/i18n和对应通用页测试由实现子代理完成；主会话负责有效DPI、ShowSerial策略接入与进程/尺寸边界回归。
- 按 Trellis 流程进行独立审查，核对配置读取/捕获、Show序号、默认值、退出/重建和 i18n；保持原编码/BOM/换行。
- 运行 i18n sync/check、git diff --check、ARM64 原生MSBuild完整 InkeysRepo.sln Debug|ARM64 及已有无窗口测试。前轮 Codex 构建存在 C1902 PDB 管理器环境错误，若复现则记录首个有效错误，不修改工程调试格式或依赖绕过。

## 验证结果

- ARM64 原生 MSBuild 完整 `InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64`：首次与最终文案更新后均 exit 0。主程序、PptCOM、测试目标均已生成；见 settings-scale-entry-build.log / settings-scale-entry-final-build.log。
- 构建在正常主机权限下运行；同 invocation 规范化 PATH、禁用 node reuse。前轮受限构建的 C1902 本轮未复现，未修改工程、工具链或调试格式。
- `InkeysHeadlessTests.exe --no-window` 全套 exit 0，见 settings-scale-entry-tests.log。
- 最终产物 `InkeysHeadlessTests.exe --no-window --setting-home-only` exit 0，见 settings-scale-entry-final-home-tests.log。
- 已覆盖三档及非法模式、未显示预热、重复激活不递增、Hide/Show 和 resident 重建、新进程首开；真实页面验证首开小视口/第二次大视口不补播、首次总动画关闭不补播、首次合适时可播放、始终可重播、关闭仅停止开屏。
- 通用页验证新模式行存在、旧独立缩放行消失、键盘变更一次、主栏拖放保存行为保持；设置 DPI 96/120/144/192 对应 100/125/150/200%，不叠加用户倍率。
- i18n sync/check 通过，英文与繁体均 490/490；独立审查两处文案建议已修复，无未解决意见，见 settings-scale-entry-review.md。
- 原 UTF-8/BOM/CRLF 与 diff 检查通过；两张既有 CSO 的 SHA-256 与本轮基线一致。此前四个插件 PNG 改动保留。
- 构建警告为既有数字转换、第三方头、测试对齐等警告，未为消除无关警告扩展修改。
- 未运行 GUI/Computer Use（本轮未授权）；未 commit/push。父任务继续 in_progress，原有实机视觉验收仍由父任务承载。

## 本轮纯文案修正（2026-10-24）

- 简体：标题“设置界面开屏动画”；描述“控制设置界面开屏动画行为”。
- 繁体：标题“設定介面開屏動畫”；描述“控制設定介面開屏動畫行為”。
- 英文：标题“Settings opening animation”；描述“Control how the Settings opening animation behaves.”。
- 仅修改三语言 SettingEntryAnimation 的 N/E 六个文案键及中文快照对应两行；选项文字和功能逻辑不变，生成头文件字节未变。
- 按既有 i18n sync 生成后补齐翻译；最终 i18n check 英文/繁体均 490/490 PASS，git diff --check 通过，UTF-8/BOM/CRLF 保持。
- 本轮为纯文案修改，未重新编译，未运行 GUI/Computer Use，未 commit/push；此前编译与测试记录保留。

## 提交授权（2026-10-24）

用户明确授权提交本轮设置改动，包含此前插件 PNG 恢复、设置 DPI/开屏策略及最新文案和验证记录；不 push。两张既有 shader CSO 不纳入提交，保留原工作区字节。保持现有 SSH 签名；签名失败时停止，不绕过。父任务继续 in_progress，不结束或归档。
