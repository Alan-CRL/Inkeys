# Whiteboard v1 验证与交付记录

## 实现范围

已建立父任务及 storage、theme-menu、scene 三个子任务；保留并关联 08-17-ui3-whiteboard 与 08-23-shared-bottom-page-control-windows 历史。实现代理及独立检查代理的记录位于各子任务 research/。

恢复 UI3 白板入口、左下同窗口菜单、每页六色/三纹理主题，复用共享分页 HWND/Scene 动画及窗口组最小化。Draw3 使用进程会话私有的多页 UInk 文档和异步页级存储，离页/离开/退出保存，完整保留已有 CPU undo/redo/Clear 恢复能力；保存失败保留热页并重试。场景协调器保持首次进入前的桌面底栏快照，PPT/白板互切不覆盖，最终回桌面恢复。

完整原始日志保存在仓库忽略目录 `TestResults/whiteboard-v1-2026-10-15/`，下表证据列均相对该目录。

## 已执行验证

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| ARM64 原生 MSBuild，InkeysRepo.sln Debug/ARM64 | 通过，退出码 0 | solution-build-final-ui.log；前序源码编译见 solution-build-verified.log |
| 独立 UInk 测试工程 Debug/ARM64 | 通过，退出码 0 | uink-build-scope.log |
| --uink-page-file-only | 通过，0 failures | uink-page-tests.log |
| --uink-file-only | 沙箱外通过，退出码 0 | uink-tests-unrestricted.log |
| --draw3-whiteboard-storage-test | 通过，退出码 0 | whiteboard-storage-test.log |
| --draw3-parked-desktop-exit-test | 通过，退出码 0 | draw3-parked-desktop-exit-test.log |
| --draw3-parked-ppt-retained-test | 通过，退出码 0 | draw3-parked-ppt-retained-test.log |
| --draw3-control-fence-test | 通过，退出码 0 | draw3-control-fence-test.log |
| InkeysHeadlessTests --no-window | 通过，退出码 0 | headless-final.log |
| --bar-eraser-offscreen-test | 通过，PageControlScene / EraserVisual 均 0 failures | ui3-offscreen-final.log / ui3-offscreen-details.log |
| i18n | en-US、zh-TW 各 364/364 | 子任务独立检查记录 |
| git diff --check / 编码换行 | 通过 | 原有 BOM 保留，工作区 CRLF 与 core.autocrlf=true 一致 |

100 页测试实际执行 worker I/O、冷读、主题与历史恢复、失败写入/索引提交/读取、旧 revision 拒绝卸载，并释放 CPU 笔迹和历史。GPU 使用 128×128 WARP composition swap chain，无 HWND、无 Present；每页实际填充 undo/composition 缓存，先确认资源非零，再调用生产 Discard 路径并确认六项应用缓存引用计数归零。此结论不是驱动显存立即返还系统的证明。

流式文件测试构建 100 个非空页并校验目录不持有笔迹/extra，按页读取、替换中间页、拒绝重复身份/错序/旧修订、识别损坏与安全删除。peakBufferBytes 仅统计序列化工作缓冲高水位，不能作为整个进程 RAM 测量；元数据目录仍随页数增长。

## 环境诊断

- 本机 Codex MSBuild 环境在同一个 PowerShell invocation 规范化 PATH 并关闭 node reuse，使用 Visual Studio 动态定位的 ARM64 原生 MSBuild。未改 SDK、依赖或项目构建方式。
- 初轮并行模块扫描出现 CL exit 4，串行 MSBuild/CL 参数可完成；参数仅用于本轮命令，未写入工程配置。
- 首轮链接目标输出短暂无法打开（LNK1104），确认无运行的 Inkeys 且输出路径可创建后重试链接成功。
- 沙箱内既有 UInk 替换文件断言失败；独立 Win32 ReplaceFileW 探针同样 error 5。经工具批准在沙箱外运行同一无窗口测试后完整通过，未修改文件保存源码来规避权限。

## 人工验收及已知边界

未启动 GUI、Computer Use、真实 PPT 或任务栏窗口。连续快速进入/退出、真实笔与触摸取消菜单、任务栏最小化恢复、跨显示器/DPI、PPT后台结束和最终视觉动画仍属于人工验收项。

失败的索引提交会保留候选 .uink/current-N.tmp 以避免覆盖已提交版本；持续故障可能留下额外磁盘文件，尚无失败候选清理策略。正常提交仅保留当前及一份备份，不清理旧进程会话。

不支持平移、缩放、页面管理、手动导出、历史画布 UI、跨进程自动恢复。用户已授权提交当前改动，未 push；本次提交不结束任务。三个子任务标记 completed，父任务保持 in_progress 以保留人工验收清单，不执行会自动提交的归档流程。

## 截图反馈修复轮（2026-10-15）

已修复入口 2×2、分页标签主文字槽、独立上方菜单和右侧主题矩形、70×70 预览中心勾、共享 Back 动画及收起负尺寸、居中空间换向、最小化主栏/分页状态、PPT 最小化禁止、Inkeys 国际化标题与 exe 图标。独立检查同时修复外部最小化 veto 和最小化后关闭留下 iconic 锚点。

- ARM64 原生 MSBuild / InkeysRepo.sln Debug|ARM64：非增量链接通过，退出 0（revision-build-nonincremental.log）。此前增量链接器 IncrBuildImage 内部错误 LNK1000，结束已失败进程后仅命令传入 LinkIncremental=false；未改工具链或项目设置。最终仅 LNK4075 EditAndContinue 与非增量链接不兼容提示。
- 主程序 --bar-eraser-offscreen-test：显式等待退出码 0，PageControlScene failures=0，EraserVisual failures=0；包含标签像素、独立面板命中、多个倍率、关闭和反向展开中间帧。详见 revision-offscreen-stderr.log 与 Build/eraser-b/offscreen-results.log。
- 无窗口离屏截图 TestResults/whiteboard-menu-revision.png 已查看，确认独立面板、标签、70 DIP 方形预览和中央勾。不是 GUI 截图或动画体验验收。
- 三语 i18n 368/368，diff whitespace、原源文件 BOM/CRLF 检查通过。
- 真实 HWND 的窗口组测试仅编译，不运行；任务栏图标、owned-popup 最小化恢复、PPT/Win+D、跨屏与动画手感仍需人工验收。现有局部 Window Service 测试无法覆盖进程全局 WndProc 路由，该测试边界已记录。
- 本轮不提交、不 push、不结束或归档父任务。
- 最终构建产物 InkeysHeadlessTests --no-window：退出 0（revision-headless-final.log），主栏/分页/场景及动画回归通过。

## UI3 统一实施轮（2026-10-15，当前结果）

本轮直接实施最新授权：菜单操作140×32.5 DIP、More居中、无箭头、所有间隔5 DIP；背景颜色/背景样式分组与共享More分割线；完整2×2按钮及内部标签、30 DIP原colorSelect SVG、中心对称纹理。共享Scene复用现有属性motion/父子pose/FrameAnimationClock，动态呈现crop与命中/光源/脏区同帧。纯内容更新不重开布局，预览复用设备缓存。

卡顿相关的代码证据和修复：移除业务窗口与逐控件弹性叠加；修复无图标opacity1→0、隐藏内容opacity0→0.3每帧目标振荡；内容更新不再重开布局与输入锁；关闭不再等待5秒hover淡出。没有测真实GUI帧率，不能将这些静态及离屏证据等同于实际FPS改善验收。

| 验证 | 结果 | 证据（TestResults/whiteboard-v1-2026-10-15/ 下） |
| --- | --- | --- |
| ARM64原生MSBuild完整 InkeysRepo.sln Debug/ARM64 | 通过，最终退出0；3个既有hashlib++警告 | ui3-unify-build-checked.log；完整依赖重编见ui3-unify-build-verified.log |
| InkeysHeadlessTests --no-window 最终产物 | 通过，退出0，橡皮216布局及撤回动画均零失败 | ui3-unify-headless-final.log |
| 主程序 --bar-eraser-offscreen-test | 通过，显式等待进程退出0；PageControlScene和EraserVisual零失败 | ui3-unify-offscreen-checked-stderr.log、Build/eraser-b/offscreen-results.log |
| i18n sync/check | en-US及zh-TW各373/373 | 本轮工具输出 |
| 原文件BOM/CRLF及git diff --check | 通过 | 工作区格式审计 |

新增生产Scene测试覆盖：冷PPT转白板复用实例、父子同时展开、0.5/1/2倍动画速度、0.75/1.5倍DPI、负原点、逐帧对照共享motion、同帧Advance/Render幂等、viewport包含当前内容、输入坐标、途中换色不重启、禁用动画立即收起、真实绿色SVG尺寸、按钮内标签、纹理镜像像素、纯白背景可读标签。

首轮离屏失败分为两类，均保留严格检查修复后通过：174项参考时间比较缺少冷启动已稳定前置条件；12项面板/分割线实际缺失来自hover清理重置chrome透明度，已调整执行顺序。实际动画曲线及像素阈值未为通过测试而降低要求。

环境：系统Python alias不可执行，使用已有bundled Python；git子模块读权限限制使用ignoreSubmodules只审查本工作区。沙箱内C1902用同一最小C++探针独立复现，批准沙箱外同一探针退出0；完整构建因此在沙箱外复核。配置文件写入测试同样在沙箱外运行相同--no-window通过。未更改SDK/工具链/工程配置。命令行沿用非增量链接避免前轮LNK1000。

没有启动GUI/Computer Use/真实PPT，任务栏、跨屏及真实动画手感仍需人工验收。本轮未commit、未push，父任务保持in_progress，不结束或归档。
