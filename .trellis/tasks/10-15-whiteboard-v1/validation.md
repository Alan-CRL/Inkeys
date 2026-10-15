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
