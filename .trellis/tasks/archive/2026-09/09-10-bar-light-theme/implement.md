# 执行计划

1. [x] 用户同意任务和 4478887c 基点，创建 theme 独立 worktree。
2. [x] 读取主题、收展、主按钮 SVG、替换槽和渲染接口；写入规划。
3. [x] 配置 implement/check 上下文；用户批准最终规划后已激活任务。
4. [x] Trellis implement 子代理实施角色/材质渲染、Bar 集成、Logo 结构；先约定跨文件接口和所有权。只能修改 theme，不回退他人改动。
5. [x] 覆盖系统主题更新、全材质收展、快速反向、关闭动画和 dirty/viewport 外扩。
6. [x] ARM64 host MSBuild 完整构建 InkeysRepo.sln Debug|ARM64，持续允许至少五分钟。
7. [x] 运行 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window，以及 SVG 几何、路径、编码、CRLF 静态检查。
8. [x] Trellis check 子代理审查最终全范围差异，修复任务内失败并更新规范。
9. [x] 记录验证与未执行 GUI 的限制，按完成状态记录/归档，不提交 commit。

## 验证重点

材质端点和 0.25/0.5/0.75 的全部参数；反向当前值接续；Dark 参数回归；Light 反射色不来自边框，笔色混合有限；真实绘图色与 UI 显示色隔离。普通 SVG 几何不变，主笔空隙无横线，新增层同时参与脉冲/父继承/底栏变换/dirty/设备回收。主题消息只发布目标，idle 无轮询。git diff --check、BOM/CRLF 与项目登记有效。

## 构建环境与回滚

在本 worktree 使用原生 ARM64 MSBuild 完整 Solution 带入 PptCOM。可复用原 worktree 的已安装第三方依赖，不能复用未提交产品源码或 object；输出写入本 worktree ignored Build/Cache。三个审查单元为颜色/材质 helper、renderer、主按钮资源与集成；禁止 stash/reset/移动 draw 现场。
