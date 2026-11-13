# 续轮接续验证（2026-11-13）

## 边界与交接

接续当前分支的未提交改动，按父任务 C1–C14 实施。仅按用户授权进行本地 commit、不 push；未启动 GUI，也未运行会创建窗口的 hidden Host 测试。父任务保持 in_progress，实机触控/笔/PPT 手感由用户验收。

## 接续中补齐的修复

- 13/14 数字按钮曾与色板 ID 重叠；后续 ID 改从 GeometrySidesCount 派生。分隔线按 ToolbarItems 显示顺序判断，解决非连续 ID 的分组错误。
- 主栏辅助文案、类别分段和新增约束分别使用测宽缓存 0–2、3–5、6–9；三角形宽度仅计三项。字号目标考虑最终工作区 fit。
- 底面轴使用未投影外接圆半径，水平首顶点可调投影；恢复不支持的平面 creationMode 校验。
- 正常完成沿携带原构造 session，在文档提交和首次绘制成功后发布；提示接收后锁存到退场结束，.24 秒走满与 .4 秒收缩重叠。取消、跨会话不走满。
- 新增 13 边图标原有 14 个顶点；13/14 均改为与既有图标相同半径和居中规则的规则多边形。
- 修正倾斜等腰梯形关闭约束被旧水平底边校验拒绝的问题，画布顶点不变，继续支持旧格式。
- 恢复三处 UTF-8 BOM，统一任务 design/implement 新增段落与原文件的 CRLF。

## 已完成的验证

- ARM64 原生 MSBuild 从 vswhere 定位；同一 PowerShell 中规范化 PATH，MSBUILDDISABLENODEREUSE=1、/nr:false。
- InkeysRepo.sln Debug|ARM64 完整 Rebuild：0 error；inkStrokeModelerTest.sln Debug|ARM64 完整 Rebuild：0 error。C++ modules 及 shader/资源链完成。最初增量构建缺模块，完整 Rebuild 后消除，未修改工程或依赖。
- 共享 shape-core-only、生产 draw3-shape-core-test：0 failure，包含约束、水平投影轴、弧跨端点与开放点工具。
- draw3-shape-renderer-test：通过；新增起点实际像素断言，保留普通笔/荧光笔/Laser 基线。
- uink-presentation-only、desktop-autosave-only、draw3-whiteboard-storage-test：通过。
- uink-file-only：补齐 TestResults/release-hardening 前置目录后，正常权限环境完整通过。受限运行中的目录租约/硬链接清理失败属于本机测试环境，未修改文件协议源码。实际包含 260 个构造图形 / 264 个标准部件及 v5/旧版本用例。
- shape-tool-state-test、state-mode-production-test、pen-width-production-test、bar-eraser-offscreen-test：通过。
- ui3-shape-editor-test：544 scenes / 4192 commits / 232101 checks，0 failure；包含实时跟手、换边粘性、主栏遮挡、双侧不足、12 个数字字形、深浅/DPI 与透明度。
- InkeysHeadlessTests --no-window：通过（首轮 5 分钟不足，扩至 15 分钟后完成；未改测试实现）。
- Scripts/i18n.ps1 check：19 个非基准语言 806/806，全部通过。
- ui3-geometry-attribute-test：5760 个完整生产场景 / 13629 次实际提交 / 1626442 次检查 / 144 动画帧，0 failure；窄屏英文正多边形裁剪及最终 fit 字号均通过。共享 fit 的 exact-size 比较曾因浮点尾差跳过 Y 钳制；已在共享函数修复，新增 nextafter 边界、负屏坐标及真正过大的负例。
- 最终 shape renderer：435 cases，0 failure；折线纯填充作为非法输入拒绝，其余实际可用点工具样式有真实起点像素。
- 倾斜等腰梯形关闭约束：保留画布顶点、局部倾斜归入 transform，保持旧 v3 水平底边协议；核心与 UInk 编解码回归通过。

日志、截图和过程脚本放在忽略目录 Build/shape-continuation、Build/shape-p2-ui、Build/shape-p34-ui；既有未跟踪 cso 保留，不纳入源码提交。

## 防回归复盘

根因包括变更传播遗漏（边数循环更新但 ID/面板尺寸未同步）、跨层完成沿未被 UI 消费、隐式缓存索引共享，以及贴边计算依赖浮点精确相等。只改循环或提示目标无法验证整个链路。防线已落到真实生产命中/像素/逐帧进度、水平首顶点测试及 spec 的明确身份/缓存合同中。

## 提交边界

用户已授权提交本轮改动，但不结束任务。本轮核心、产品 UI、资源/i18n、回归及 spec/task artifacts 作为一个完整续轮修复批次；Build/TestResults/cso 不包含在源码批次中。不运行 finish/archive，不 push，任务继续保持 in_progress。

最终交付增量构建日志：product-delivery-build.log、core-delivery-build.log；最终相关复跑：delivery-results.json、uink-delivery-results.json、headless-verified-results.json、verified-results.json（renderer 早期非法夹具失败由 delivery-results.json 中最终成功结果取代）。全范围 git diff --check 通过，BOM/换行审计通过。
