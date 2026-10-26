# 接力复核与新增定格退场回归

- 当前用户已授权恢复并继续父/子任务的全部实现；旧规划中的仅调查限制属于历史阶段。GUI/Computer Use 仍未授权；不 commit/push。
- 进入会话时 HEAD=fdda877d，6 个 tracked 文件与交接完全一致，未见额外产品 DPI/zoom 修改；dpiZoom/configZoom/合成 monitor DPI 仅用于已有离屏 fixture。原文件 BOM 与 CRLF 保持，incoming-tracked.zip/incoming.patch 保存在忽略目录 TestResults/ui3-task-resume。
- 重新运行居中离屏入口退出 0，6 组全部通过。原系统临时目录下完整 no-window 退出 1（572 个文件持久化失败）；测试使用 fs::temp_directory_path，改为仓库内 TEMP/TMP 后正在同产物复跑，不修改配置或源码。
- Trellis 三任务 validate 通过；大规范有注入截断警告，本会话已直接读取相关章节。已恢复居中子任务为本会话当前任务。

## 新增用户回归与最小修改边界

用户报告有内容桌面→空 PPT、稳定居中主栏收窄时定格按钮向右猛移并渐隐，要求仅隐藏、保留 List 身份，并自动收拢到前一按钮下层。

源码证据：Button.cpp 的 AppendFixedButtons 始终保留 Freeze，ApplyMainPresentationLocked 只设置 hide；RenderLoop 对隐藏按钮不更新位置，且隐藏锚点遗漏 Freeze，因此居中根 X 改变时保留旧局部坐标。先扩展现有真实离屏 fixture 验证身份、前一可见按钮目标、渐隐与联合居中；生产改动仅复用现有 AnchorHiddenBarButton 和绘制层级。预计只修改 Bar.RenderLoop.cpp 与已有 Bar.MultiTarget.Test.cpp，不改变 DPI、业务尺寸、曲线或列表成员。

## 本轮已取得证据

- 全量 `--no-window` 在同一原始产物、仓库内 TEMP/TMP 下退出 0，329.844 秒，末尾 `PASS animation correctness`。首轮 572 个失败由默认系统临时目录写入受限引起；未改代码。
- 新定格回归在旧生产逻辑下退出 1：右展目标 640 DIP、前驱目标 340 DIP；左展目标 40 DIP、前驱 115 DIP。100%/150% 四组均失败；List 身份不变。
- 首次沙箱构建 C1902，沙箱外同命令成功，确认为本机/Codex PDB 服务权限问题；测试 fprintf 新增类型警告已显式转换修正。

## 最终结果

2026-10-26：所有当前产物自动化通过。Freeze 覆盖两种 A2 顺序，共 1248 帧/24 组合；居中原 6 组/72 阶段/2370 成功样本仍通过，最大记录误差 1.13687e-13 px。最终完整 no-window 退出 0、337.813s，二进制未被后续 Solution 改变；两个入口同一最终 EXE 退出 0。完整证据见 `../../10-25-ui3-bar-centering-dock-geometry/research/final-integration-validation.md`。GUI/Office/WPS/真实 ULW/DWM 仍待授权，任务保持 in_progress。

更正构建归因：C1902 在沙箱内出现，沙箱外配合私有临时目录后成功；归类本机/Codex 环境问题，未唯一定位为 PDB 服务权限。
