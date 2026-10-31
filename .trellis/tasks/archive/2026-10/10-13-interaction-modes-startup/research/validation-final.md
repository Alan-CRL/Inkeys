> 上轮验证记录（本轮用户反馈卡死仍存在）：同步桩回放未覆盖生产等待链。本轮结果另写 validation-freeze-fix.md。

# 首批实施与最终验证

- 模式块改为两个直接互斥单选项，每帧读取实时 setlist；双向切换继续调用 SyncDraw3State、FIFO WriteSetting 与绘制参数更新。原组合框现场故障未复现，不把新入口验证当作原故障根因证据。
- 三语标题、模式名称和两条说明采用用户原文；容器根据文字换行高度布局。三语 JSONC、快照和生成头一致。
- i18n sync/check：en-US、zh-TW 334/334，100%，退出0。
- 模式无 HWND 回放：三语 × 0.6/1/1.5/2缩放，12组通过，双向切换保存/同步各两次，重复选择不增加调用，控件裁切0；退出0。默认字体和业务桩函数的限制见 validation-replay.md。
- 完整 InkeysRepo.sln Debug|ARM64：vswhere定位当前 VS 的 ARM64原生 MSBuild；同一PowerShell先规范化 PATH并关闭node reuse。sandbox 内 C1902，多个未修改文件也失败，退出1；自动审批允许sandbox外同命令重试，退出0，生成 Inkeys.exe 与 HeadlessTests.exe。没有工具链/项目配置绕过；既有类型转换warning位于本次改动范围之外。构建输出有pwsh.exe未找到提示，但命令退出0，i18n检查另已通过。
- 新构建的 InkeysHeadlessTests.exe --no-window：sandbox 内配置/原子文件写入等11项失败，退出1；自动审批允许sandbox外同测试复核，退出0，PASS animation correctness。日志 headless-unrestricted.log。
- trellis-check独立审查全部产品文件和规格：无发现，无追加代码修改。
- git diff --check --ignore-submodules通过；产品文件BOM状态和CRLF保持，启动判断/配置读写/共享控件代码无修改。
- 未运行GUI/Computer Use，未验证应用真实重启或字体像素显示；未创建commit/push。当前任务保留供后续产品模式与启动预设工作。
