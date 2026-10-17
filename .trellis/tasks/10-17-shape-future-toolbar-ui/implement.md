# 执行与验证计划（已完成）

## 进入实施前

- [x] 用户授权提交旧修正；完成 commit 384c5838，无 push。
- [x] 用户确认本轮只补 UI，未实现工具禁用，允许建立子任务并规划。
- [x] 建立 PRD、设计与执行草案。
- [x] 完成目录、公共 UI3 禁用/布局与测试接线调查，保存 research/ui-catalog-and-layout.md。
- [x] 配置真实 implement.jsonl/check.jsonl 并通过 task.py validate（各 5 项真实上下文）。
- [x] 用户审核 36 类按钮清单，选择 A 方案，并明确批准开始实现。
- [x] task.py start 本子任务；未激活总任务或 P3–7 的绘制实现任务。

## 实施顺序与责任

1. trellis-implement：UI 目录/派生索引/只读可用性合同与新增 SVG、资源、三语标签；复用公共 UI3，不扩大业务枚举。适当拆开测试文件所有权，避免并发覆盖。
2. trellis-implement：六列目录扩容、现有参数/属性/关闭布局接线、禁用视觉/命中/键盘/动作入口；保持属性 list 与 5 DIP。按用户批准的 A/B 方案处理小工作区；若为 A，局部实现类别视口/共享动画偏移/滚轮与滚动条/裁剪命中，不扩散至其他 list。
3. trellis-check：独立全范围审查和自修复；扩展真实生产场景离屏验证，重点检查未实现工具无法触发业务命令、小工作区、旧工具与动画。
4. 主会话统一构建、运行相邻回归并记录 validation.md；复查 diff/编码/i18n/资源。
5. 更新必要的 .trellis/spec 与总任务进度。无自动 commit/push；新实现的提交需另行用户授权。

## 构建与命令验证

- 用 vswhere 定位 ARM64 原生 MSBuild；同一次 PowerShell invocation 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue，设置 MSBUILDDISABLENODEREUSE=1。
- 构建 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64，允许至少 5 分钟，读取真实退出码与资源编译证据。沿用已确认的本机构建参数，不修改工程配置绕过环境问题。
- 生产离屏入口：Inkeys.exe --ui3-geometry-attribute-test，需扩展至完整目录与禁用矩阵；通过隐藏进程等待真实 ExitCode，不创建 HWND。
- 相关回归：--shape-tool-state-test、--pen-width-production-test、--state-mode-production-test、--bar-eraser-offscreen-test；InkeysHeadlessTests.exe --no-window。
- 运行仓库既有 i18n sync/check；XML 解析所有新增 SVG，检查 rc 资源和正式加载成功；git diff --ignore-submodules=all --check，核对 BOM/CRLF 和改动边界。
- 保留原工具绘制路径；本轮不改几何/shader/UInk，除非审查发现直接影响再决定是否扩大相关回归，不无条件跑全部持久化套件。

## 回退点与交付

目录与资源先保持局部可回退；业务值/序列化不迁移。完成验证后给出新旧工具可用性清单、实际构建/测试结果、截图、已知限制及后续逐阶段启用方式。无法证明的小工作区可读性或禁用输入正确性须回到设计处理，不降低测试阈值掩盖问题。

## 上下文加载注意

task.py validate 已通过，但 rendering-and-ui.md / configuration-i18n-and-assets.md 超过原生注入单文件长度限制。子代理不能把截断注入当成已读全文；实施/审查前必须主动读取 rendering-and-ui.md 的 P2 末节与本任务 research，并读取配置资源规范相关 i18n/资源章节。研究记录含精确接线与公共禁用合同。当前仓库没有安装独立 trellis-start/SKILL.md；本轮已直接读取 .trellis/workflow.md 与相关 spec，未修改流程或工具配置。

## 完成状态

36 类目录/25 SVG/三语/A 方案已实施并验收；trellis-check 全范围审查通过，首轮工作区事实失效问题已修。最终构建和六项 CLI 回归均退出 0，详情见 validation.md。已更新 spec 和总任务进度；保留未提交改动，不执行自动归档/日志提交。
