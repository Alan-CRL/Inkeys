# 第二轮运行时调整交接

用户本轮反馈已由 root 记录于 revision-2.md 和 PRD/design。子代理仅调整运行时及已有测试，保留前轮所有未提交代码；Setting/i18n/文档主文件与构建由 root 负责。

## 改动

1. `Inkeys/Inkeys/UI/Bar/Bar.ThemeMaterial.h`：唯一 IdleContentOpacity 由 0.40 改为 0.60，背景仍 0.30。所有现有常驻内容绘制消费者自动使用新值；正常内容低于 60% 时仍取正常值，未改正常配置或其他透明度角色。
2. `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：单独建立 dockEligible，唯一来源为本帧 frame.bottomDockMode==BottomDocked，不检查 center mode，因此 Free 普通底栏与 Centered 居中底栏均可。主栏纯策略 Update 的场景门禁为 sceneEligible&&dockEligible；Floating 得到正常目标和 max 到期期限。Floating 分支复用已有直接清零进度、MainGroup dirty/needRendering，避免旧淡化像素残留；返回底栏因 eligible 边沿完整重计时。
3. 输入清理仍按真实 sceneEligible、PPT可见性、场次变化判断，未把 dockEligible 放进 ResetPptIdleInput 条件。悬停/按下等 live 输入不会仅因停靠形态变化被清除，原附属面板保护保持。
4. `InkeysHeadlessTests/bar_theme_tests.cpp`：增加 0.6 等值样本，将定点验收改为 80%→60%（非48%），50%正常内容保持50%。
5. `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`：已有真实 Scene/D2D 像素期望改为60%内容覆盖30%背景，合成alpha约72%（184/255），背景期望不变。
6. `InkeysHeadlessTests/page_control_tests.cpp`：在现有主栏生命周期策略测试追加底栏到期→Floating清期限→带有效悬停返回→交互结束后等待完整10秒的序列。没有新建 helper 或新测试入口。

## 静态验证与冻结

- 本轮仅上述5个原有源码/测试文件变化。原始字节副本位于忽略目录 Build/ppt-idle-runtime/revision-2-before，列表 revision-2-files.json。
- `git diff --check --ignore-submodules` 通过；Git对 PageControl.cpp 的 autocrlf 预警不是文件转换，该文件保持原LF，其余文件保留原BOM/CRLF。
- 源码已通知 root 冻结；本轮未编译、未运行 EXE、未做 GUI/Computer Use、未 commit/push。
- root 统一执行 solution build 和最小必要测试，实际退出码/像素通过证据以 root 最终验证记录为准。本报告只记录实施与静态核对，不声称运行测试已通过。
