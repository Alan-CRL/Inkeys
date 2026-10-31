# 分页窗口启动显隐修复与验证记录

日期：2026-10-12

## 已完成修改

- `Inkeys/Inkeys/Window/Window.Legacy.cpp:151` 删除四个分页窗口的无条件 Show，改为两行中文注释，产品差异 +2/-5。
- PageControl 客户端注册与 RequestAll、PPT/白板状态发布、布局、呈现成功后的 SetBounds/Show 和隐藏分支均保留。
- `.trellis/spec/native-desktop/rendering-and-ui.md` 增加一条启动业务显隐归属合同。

## 已通过验证

- 实现代理精确替换可逆检查和独立 trellis-check 审查通过，无待修代码问题。
- `git diff --ignore-submodules=all --check` 通过；UTF-8 无 BOM、CRLF 保持，无全文件格式变更。
- Scheduler 的 Register/Request 通过请求位和 event 唤醒，无 HWND 可见性门禁；RenderSurface 要求句柄存在，不要求提前显示。

## 编译验证未完成

- 复用 `InkeysRepo.sln`，`/t:Build /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false /v:minimal /nologo`。
- 从 vswhere 定位当前安装的 ARM64 原生 MSBuild；同一 PowerShell invocation 先移除 Env:PATH，并设置 MSBUILDDISABLENODEREUSE=1。
- 10 分钟超时停止 MSBuild，退出码 124。最后阶段输出：模块依赖扫描、Window.Legacy.cpp；PptCOM DLL 和 TLB 生成成功。
- 没有报告具体源码、链接或环境错误。当前输出不足以判定超时根因，不将超时认作源码错误，也不声称本机环境已确定故障。
- 未重复启动相同构建；未改 SDK、工具链、依赖或项目配置。

## 验证限制与后续

未新增复刻四次调用删除的测试。未运行 GUI、真实笔输入和 PPT/白板界面回归，遵守用户默认禁止 GUI 的规则。静态结论支持消除显隐竞争；真实落笔恢复仍需要运行验证。编译确认仍待完成，保留任务 in_progress，不归档、不提交。

## 用户验收与结束任务

2026-10-12 用户明确反馈“当前问题确认解决”，并要求结束任务和提交 commit。按该反馈关闭本次缺陷任务。用户验收不是 AI 的 GUI 测试，也不替代完整编译成功证据；此前 10 分钟构建超时仍如实保留。不执行 push。
