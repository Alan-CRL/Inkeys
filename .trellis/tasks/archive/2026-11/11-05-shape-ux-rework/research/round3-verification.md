# 第三轮验证（基线 ec1d5138）

## 实施范围
已按批准方案修改19项交互与构造问题，并统一inkeysShape v7、删除开发期v1–v6专门兼容。保留已发布线/矩形与通用UInk标准几何导入导出。三个子任务已建立；父任务和子任务保持in_progress，不commit/push/归档。

## 自动验证
- 共享完整solution Debug|ARM64构建退出0，核心测试0失败。
- 主产品最终完整InkeysRepo.sln Debug|ARM64构建退出0（product-delivery-build.log）；以下日志位于Build/shape-round3。
- UInk全套持久化退出0（uink-file-final.log），包含v7、退役版本标准降级、已发布矩形、文件保存重载。
- 产品core、renderer（471项）、whiteboard storage、shape-tool-state、state-mode-production均退出0（delivery-results.json）。
- Headless --no-window、uink-presentation、desktop-autosave均退出0（other-results.json）。
- 几何属性UI：13603 commits、1586610 checks、144 frames、0失败。
- 编辑器UI：最终544 scenes、4196 commits、235295 checks、0失败（verified-results.json、ui-editor-final-results.log）。第二轮10条失败来自新增负例误套通用glyph断言（主栏透明区/首笔仅标记预览），修正oracle后通过，普通帧像素检查不变。
- 二十语言809项全部通过，跟踪文件BOM保持、无混合换行，git diff --check通过。

## 生产链证据
SF既有隐藏窗口fixture扩展到47，窗口visible=false、屏外、自动销毁，不打开交互式GUI。完整Host/Run确认首次编辑器发布时Undo已可用；Desktop/Whiteboard保留编辑/Close+Undo四组合约10–11ms（测试轮询粒度约10ms，不是性能承诺）。构造点编辑→Undo/Redo→物理续画→完成均通过。
已修两处确认问题：构造期主栏历史可用性与命令路由不一致；编辑层alpha/Hit未排除主栏。没有把单一覆盖缺陷宣称为全部实机延迟根因。

## 环境与测试纠正
沙箱UInk文件身份/硬链接测试失败，正常进程环境同一测试通过；无源码绕过。运行中测试占用输出导致LNK1104，等待退出或命令行OutDir隔离后重链；不改工程配置/依赖/工具链。保留既有第三方warning。
核心旧断言仍允许立体二维旋转、要求向下圆锥必有隐藏环，均与批准的新固定视角冲突；替换为模型几何断言。首轮SF白板遗漏真实主题token ACK导致input admission=2048，补产品相同ACK而非放宽准入。首笔只读提示转编辑的缓存条件须排除drawingPreview，并由同坐标Move/Up回归覆盖。

## 验证边界
未启动应用主窗口、设置窗口或Computer Use。离屏/隐藏测试不替代实机笔触控手感、动画观感、边缘遮挡与PPT场景验收。

最终缓存修正后完整SF再次退出0（verified-draw3-shape-focus-hidden-test.log），同坐标Move/Up外框和旋转柄恢复断言通过。

最终所有计划自动验证已通过。验证后的delivery/Inkeys.exe及PDB已复制回Build/ARM64/Debug，EXE SHA256一致。最终57个跟踪文件BOM保持、无混合换行；shader临时cso已清除。未提交、未推送、任务保持in_progress。
