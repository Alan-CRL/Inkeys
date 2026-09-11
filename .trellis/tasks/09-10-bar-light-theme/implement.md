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

## 续作：设置页主题模式

10. [x] 在 `Other.Config.cppm` 的 `Experimental.Inkeys3.UI3` 注册 `IdtAtomic<int> ThemeMode`，默认 `1`；增加或复用可测试的 `1/2` 归一化合同。
11. [x] 将 Bar 初始化和运行时主题请求改为配置驱动：启动读取 `ThemeMode`，公开实时设置入口，停止用 Windows 主题消息覆盖显式选择，同时保留现有串行渲染线程和连续材质动画。
12. [x] 在“常规 > 外观”加入深色/浅色下拉卡片，严格映射 `1/2`，选择时更新配置、请求 Bar 动画并通过 `QueueConfigWrite()` 异步保存；调整容器高度但不改其他布局。
13. [x] 更新简体中文、繁体中文、英文 `Appearance.Theme` 文案并运行 `Scripts/i18n.ps1` 同步生成键。
14. [x] 扩展无窗口测试，覆盖默认/非法值归一、主题模式映射、实时请求端点，以及现有主题动画和 Logo 行为不回归。
15. [x] 使用 ARM64 `MSBuild.exe` 构建完整 `InkeysRepo.sln` 的 `Debug|ARM64`，超时至少五分钟；运行 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`、i18n 校验、`git diff --check` 和编码/CRLF 静态检查。
16. [ ] 执行 Trellis 全范围检查，记录未进行 GUI 人工视觉验证的限制；更新稳定规范和开发日志，保留未提交差异等待用户后续指示。

## 验证重点

材质端点和 0.25/0.5/0.75 的全部参数；反向当前值接续；Dark 参数回归；Light 反射色不来自边框，笔色混合有限；真实绘图色与 UI 显示色隔离。普通 SVG 几何不变，主笔空隙无横线，新增层同时参与脉冲/父继承/底栏变换/dirty/设备回收。主题设置只发布目标，idle 无轮询；系统 `WM_THEMECHANGED` 不覆盖配置。下拉框值、JSON 值和 Bar `darkStyle` 映射唯一：`1 -> Dark/true`、`2 -> Light/false`，其余值回退 `1`。git diff --check、i18n 生成结果、BOM/CRLF 与项目登记有效。

## 构建环境与回滚

在本 worktree 使用原生 ARM64 MSBuild 完整 Solution 带入 PptCOM。可复用原 worktree 的已安装第三方依赖，不能复用未提交产品源码或 object；输出写入本 worktree ignored Build/Cache。三个审查单元为颜色/材质 helper、renderer、主按钮资源与集成；禁止 stash/reset/移动 draw 现场。
