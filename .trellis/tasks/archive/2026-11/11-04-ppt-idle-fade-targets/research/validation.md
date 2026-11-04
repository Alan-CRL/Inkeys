# 实现验证记录

## 构建与工具

- 本机 Windows 11 ARM64，动态 vswhere 定位 ARM64 原生 MSBuild；完整 InkeysRepo.sln，Debug|ARM64，/m:1 /nr:false，10 分钟预算。
- 同一 PowerShell invocation 先规范化 Path/PATH 并保留唯一路径，设置 MSBUILDDISABLENODEREUSE=1。
- 首轮退出 1：两处新增三元表达式的 IdtAtomic<double> 类型歧义，已显式读为 double；原日志保存 Build/ppt-idle-fade-targets/build-01.*。
- 第二轮完整构建退出 0；离屏发现旧正常主题期望固定 80% 而非浅色 88%，更正该测试后第三轮完整构建再次退出 0。最终 3 个既有 hashlib++ 警告，0 error；未改第三方、项目配置或依赖。
- 最终构建日志 Build/ppt-idle-fade-targets/build.log / build.stdout.log / build.stderr.log，第二轮日志 build-02.*。

## 无窗口验证

| 范围 | 实际入口 | 退出码 |
| --- | --- | --- |
| 设置真实复选块，20语言×3倍率×窄宽 | INKEYS_SETTING_PPT_IDLE_ONLY=1，InkeysHeadlessTests.exe --no-window --setting-home-only | 0 |
| 原分页策略与新增主栏计时/保护 | InkeysHeadlessTests.exe --page-control-only | 0 |
| 三bool八组合/严格类型/旧值保持 | Inkeys.exe --config-boundary-test | 0 |
| 真实 mailbox/frozen hit/Raw/期限 | Inkeys.exe --ui3-input-worker-test | 0 |
| 真实 Scene/Shape 像素及既有离屏套件 | Inkeys.exe --bar-eraser-offscreen-test | 0 |
| 完整无窗口回归 | InkeysHeadlessTests.exe --no-window | 0 |

实际命令、stdout/stderr 和阶段退出码位于 Build/ppt-idle-fade-targets/。offscreen 首次旧断言退出 1 的日志仍保留 attempt-01-* 和 offscreen-01-details.log；最终 EraserVisual 与 PageControlUI3 均 failures=0。

- i18n sync/check 实际通过，所有翻译语言 724/724。
- task.py validate 通过，implement/check 各 7 个真实上下文引用。
- git diff --check 通过；44 个跟踪改动文件 BOM 与修改前一致，运行时字节备份与 root 格式检查均通过。PageControl.cpp 原 LF 继续 LF，其余相关源保持 CRLF；Git autocrlf 预警不是本次全量格式转换。
- 静态全量审阅及修复见 check-review.md；冻结后 delta 仅 Scene 条件类型和正常主题测试期望，已分别复核/构建/离屏验证。

## 证据边界

未创建或操控 GUI/Computer Use，未运行 hidden-window 入口；真实 Office、桌面合成观感、各输入设备及跨平台运行未测。主栏逐面板保护由实际字段/退场聚合源码审查及纯保护时序回归共同覆盖，不把该证据写成逐面板 GUI 验收。单独 presentationVisible 退出的完整 RenderLoop 专项未新增；已核对直接清零分支显式贡献 dirty/needRendering，相关真实绘制链与状态回归通过。

本任务未创建 commit、未 push，任务保持 in_progress 等待用户实际使用确认。

## 最终身份

六组无窗口验证全部退出 0；完整回归最终输出 PASS animation correctness。产物 SHA-256、最终 44 个文件源身份、各命令结果及未执行 GUI/commit/push 的事实保存在 Build/ppt-idle-fade-targets/validation.json 与 final-source-state.json。所有源码在最后一次构建与对应运行期间保持冻结；最后仅变更正常主题离屏断言，因此前四项已通过的运行时专项未无谓重跑。

完整回归的既有橡皮演示测试（setting_eraser_demo_tests.cpp:406）把 10 张 PNG 写入旧硬编码任务路径。已确认该目录仅含本轮生成的未跟踪图像、无 task.json/源码/跟踪文件后，整体保留移动到 Build/ppt-idle-fade-targets/headless-exported-task-artifacts；未修改无关测试路径或删除图像，避免工作区出现伪活动任务。
