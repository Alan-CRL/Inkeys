# 默认折叠与父行开合追加验证（2026-10-08）

基线 `6afcd41`，直接在 fluent 原工作区按用户追加要求完成；保持父任务 in_progress 和当前会话指针，不结束/归档，不 GUI/Computer Use，不自动 commit/push。

- 新会话 generalExpanded=false、groupReveal=0，不出现首次展开闪现；常驻会话 Hide/Show 不重置偏好。
- 非紧凑父行点击 toggle：打开进入最近子页，首次 Basic；收起保留当前正文与非首子页记忆，不产生冗余 Navigate 或重启正文淡入。Enter/Space 相同，箭头/Left/Right 显式开合、紧凑菜单规则保留。
- 窄窗父行打开保留 overlay，使子项和重复收起可操作；实际子项/单页选择仍关闭 overlay。复用原160ms/软件速率及反向补间，禁用动画立即归位，无新线程/定时器。
- 仅两处产品文件调整：Setting.Design.h 和 Setting.Shell.cpp；设置控件、配置、翻译、公开接口与原业务路径未改。

## 实际验证

| 检查 | 结果 |
| --- | --- |
| 完整 InkeysRepo.sln Debug/ARM64 | 动态定位 ARM64 原生 MSBuild，同一 PowerShell PATH workaround；1800秒超时，最终增量 Solution exit0，25.06秒 |
| 最终 --no-window | 进程 exit0，PASS animation correctness；真实产品 Shell 连续帧与输入，非另写的导航模拟 |
| 三 DPI 导航 | 100%、150%、187.5% 各684帧/24次导航，无失败；原720×520/960×700/1440×900、浅深/展开/紧凑/覆盖回归继续通过 |
| 本次输入/动画 | 默认零展开、父行开合、Appearance/Storage保留、Enter/Space与子项回焦、窄窗开合/叶项关闭、实际group高度快速反向、正文fade不重置及动画关闭归位 |
| 悬停颜色 | 两倍率各315样本，0 violations |
| 独立审查 | 初始状态、父行Navigate门控、overlay路径、焦点及裁剪检查通过，无未解决本轮源码问题 |
| 格式 | git diff --check、UTF-8/BOM/CRLF检查通过；未改翻译，无需重新生成i18n |

新增窄窗断言曾出现三倍率各三项失败。实际状态诊断确认 overlay、父行开合与导航次数都正确，但前序旧“菜单外点击正文”测试的完整按钮中心落在菜单Basic项，提前改变了记忆值。修复真实测试点为正文按钮在popup之外的可达部分，并严格验证点在按钮内且在popup外、正文值/目的地/最近子页/导航次数保持。窄窗Interaction原断言未放宽、未通过重设记忆值掩盖错误；产品没有因此改动。失败时诊断输出保留以便复查。

原始日志：忽略目录 Inkeys/Cache/hierarchical-nav-20261008/build-parent-toggle-verified.* 与 headless-parent-toggle-verified.log；机器摘要在同目录 parent-toggle-validation.json。原工作区 Build/ARM64/Debug/Inkeys.exe 已重编。原有PptCOM/IdtMain及测试产物继续按备份核对保护。

本轮未执行真实GUI鼠标、截图、HWND/DX11动画观感；这些实机验收及父任务其它剩余验收继续保留。
