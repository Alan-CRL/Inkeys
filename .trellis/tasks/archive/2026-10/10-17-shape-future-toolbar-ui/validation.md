# 实施与验证记录（2026-10-17）

## 完成范围

用户批准的 A 方案已完成：36 类 UI 目录，现有 11 类可用、新增 25 类禁用；25 份粗线 SVG 与三语标签；正常六列六行，小空间仅类别区滚动；参数/独立属性 list/关闭固定且可达。无新绘制算法、业务枚举、Shape 核心、shader 或 UInk 变更。
生产修改限定 Bar.Main.cppm 与 Bar.GeometryAttribute.cpp，扩展原 Bar.GeometryAttribute.Test.cpp 验证。没有创建 commit/push，未归档或运行自动提交脚本。

## 构建

通过 vswhere 定位当前安装的 ARM64 原生 MSBuild；在同一 PowerShell invocation 先移除 Env:PATH 并设置 MSBUILDDISABLENODEREUSE=1。
实际调用为 InkeysRepo.sln /m /nologo /nr:false /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false。LinkIncremental=false 仅沿用本机既有命令行恢复参数，没有修改项目配置。过程允许超过五分钟；未启动 GUI。

| 轮次 | 退出码 | 结果 | 日志 |
| --- | --- | --- | --- |
| 首次完整构建 | 0 | 149 warnings，0 errors，1:48.85 | Build/shape-future-toolbar-ui/first-pass/build.log |
| workArea 修复后完整复建 | 0 | 142 warnings，0 errors，1:40.55 | Build/shape-future-toolbar-ui/build.log |

本轮两个生产文件/测试未报告新源码 warning；已有第三方与设置模块警告未做无关清理。新增 rc 资源经正式编译链嵌入，并由生产测试实际加载/栅格验证。

## 真实生产离屏与相邻回归

所有进程使用 Start-Process -WindowStyle Hidden -Wait -PassThru，等待实际 ExitCode；测试入口在主窗口初始化之前早退，不创建 HWND。

| 测试入口 | 最终退出码 | 证据 |
| --- | --- | --- |
| Inkeys.exe --ui3-geometry-attribute-test | 0 | 1,584 生产配置、621,101 检查、4,186 实际提交、48 原有 raster 场景、0 failures |
| Inkeys.exe --shape-tool-state-test | 0 | 原有 11 类业务工具状态回归 |
| Inkeys.exe --pen-width-production-test | 0 | 共享粗细状态与按钮路径回归 |
| Inkeys.exe --state-mode-production-test | 0 | 主栏状态与工作区接线回归 |
| Inkeys.exe --bar-eraser-offscreen-test | 0 | 相邻属性面板回归 |
| InkeysHeadlessTests.exe --no-window | 0 | 无窗口算法/输入/动画回归，末尾 PASS animation correctness |

正式日志和所有退出码：Build/shape-future-toolbar-ui/ui-results.log、test-exit-codes.json 及各入口 stdout/stderr.log。

生产配置保留 11 个真实预设 × 3 样式 × 3 语言 × 4 缩放 × 2 工作区 × 2 展开方向；每帧验证完整 36 目录（根据实际视口裁剪）。新增禁用图标使用真实 ULW 源像素及同尺寸空白负例，不以弱化纯前景阈值证明显示。
额外回放：全部 25 项禁用动作/鼠标接触/双击/取消/强制键盘与直接 Execute 不能改完整业务快照或 revision；轮滚动累积/clamp、共享动画、scrollbar 鼠标/接触拖动、键盘跳过/滚入视口、滚动期间旧快照输入门禁、失败呈现、工作区/业务/捕获失效、固定参数/属性/关闭、底部 5 DIP、退场重排、设备恢复与隐藏休眠。
最终稳定面板 Advance CPU 中位 0.6478 ms / p95 0.8167 ms，仅为本机 CPU 推进耗时，不是实际用户呈现延迟。

## 首轮发现与修复

首轮 621,101 项中只有 1 项失败：工作区从 460×420 改为 460×380 时，面板位于上侧，轨道几何未变化，旧滚动条拖动没有取消。Shape/Width/State/Eraser/Headless 首轮均 exit 0。
修复在同帧 presentation 增加 normalized workArea，把其变化加入 changed_ 和旧 drag 资格。即使绘制位置未变，也经成功呈现发布工作区事实后取消旧手势。保留原断言与测试阈值；最终重跑全部通过。首轮日志保留在 first-pass/。

## 资源、规范与格式

- i18n.ps1 sync（含 check）退出 0；en-US/zh-TW 各 416/416、100%。生成头和基准快照由工具生成。
- 新增 25 SVG 的 XML、24×24、2.6 主描边、主题语义色、唯一 rc/目录映射通过，八种函数轮廓独立。
- 主会话已查看真实 panel-light/dark、panel-scroll-top/bottom 图像；最终副本位于 Build/shape-future-toolbar-ui/。
- git diff --ignore-submodules=all --check 通过；修改 tracked 文本保持 UTF-8/BOM 与工作树原 CRLF。仓库 core.autocrlf=true，格式比较使用 git cat-file --filters 后的原工作树表示，避免将 Git blob 的 LF 规范化误判为编辑改变。
- trellis-check 完成整个 native-desktop 影响范围审查；工作区身份问题已修，无其他待修代码发现。主会话补记 rendering-and-ui.md 的目录、禁用、视口/共享滚动、成功快照和工作区事实合同。

## 已知限制与后续

新增 25 类仅 UI 禁用占位；其绘制功能按原阶段继续实现，不可把本任务当成 P3–6 功能完成。极小到不足一行的工作区仍依照既有 fit 兜底。
本轮没有实际 GUI、实体笔/触摸手感或 Win7/Win10 真机验证；本机 ARM64 无窗口结果不替代这些人工验收。下一步仍为 P3 选择/浮动编辑/提示基础，再按依赖实现逐点与高级几何、坐标和函数，逐项启用目录。

## 提交与收尾

用户授权后，功能提交为 1d4b5472798bab332d3a5a6e02585053586b1f36。子任务已归档至 archive/2026-10；未 push。前述未提交描述记录的是验收当时状态。
