# 验证记录

## 分支与环境

- theme 基于 4478887cbafe5aaad2a23d76ca5a5490f1c1e655，独立 worktree 位于 Build/theme-worktree；原 draw 未提交文件列表保持原样。
- MSBuild：C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe。
- 完整 InkeysRepo.sln，Debug|ARM64，PreferredToolArchitecture=arm64，单次超时 1800 秒。复用已安装依赖，VcpkgManifestInstall=false；HiMsg/Vcpkg 源由对应 gitlink 的干净 commit 导出。
- 宿主同时提供 Path/PATH 导致 Roslyn 初次启动失败；只规范化构建子进程环境键。沙箱随后拒绝 Roslyn named-pipe 通信，经批准在沙箱外编译，不启动产品窗口。

## 已执行

- 首轮完整 ARM64 构建通过，228.4 秒，0 errors；335 条现有文件/第三方编译警告，没有新增主题代码警告。日志 Build/theme-build.log（后续最终增量构建会更新）。
- verify_bar_theme_svg.py：30 个普通 SVG 保持原样，主笔各段/屏幕原路径、单条轮廓、BOM/CRLF 检查通过。
- 已编译的 headless palette 导出使用生产 DisplayPenColor；sharp/libRSVG 离屏检查六种笔色、四个整笔填充段、两处内部空隙、屏幕独立性通过，联系表 Build/ThemeSvgValidation/logo-resources.png 已人工查看。
- Trellis renderer 初审未发现确认缺陷；全范围审查识别并修正新增禁用动画测试参数，另对激光记忆→图形边缘光、少量颜色角色和非绘图状态指示器进行收尾。

## 最终结果

- 包含全部审查修正与测试数值容差后的完整 Solution 构建通过，exit 0，14.6 秒；0 errors，3 条未修改的 hashlib++ 编译警告。
- 全部 InkeysHeadlessTests.exe --no-window 通过，exit 0，输出 PASS animation correctness；包含材质/颜色/真实源/反向/关闭动画及既有全部无窗口测试。
- Trellis 最终全范围审查通过。修复激光记忆污染 Geometry 光源、非 Pen 深色入口色源、遗漏角色及新增测试的禁用动画参数；对 double 的4%端点沿用既有 Near(1e-6)，保持颜色下界/混色/光强约束。
- 仅本次构建生成的跟踪 Inkeys/PptCOM.dll 已恢复基点内容，不进入主题差异。
- 未执行产品 GUI、真实桌面鼠标光影、系统主题切换或文档背景覆盖的人工体验验收；本轮不声称这些已实机验证。

## 最终静态收尾

- 20 个已跟踪文本改动对照 Git checkout 基线通过 UTF-8/BOM/EOL 检查，新原生源使用 CRLF；git diff --check 通过。
- theme HEAD 仍为 4478887cbafe5aaad2a23d76ca5a5490f1c1e655；原 draw 未提交清单保持原样。
- 当前任务已使用 --no-commit 归档；产品差异继续保持未提交。
