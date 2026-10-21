# 滚动批注 Draw3 与主栏接入实施记录

## 顺序
- [ ] 读取父prd/design/implement及本任务规范上下文。
- [ ] 实现ScrollFollowService、会话绝对目标单槽与可靠结束屏障、Draw3原平移恢复、输入/双窗握手，以及主栏More按钮/SVG/i18n/console CLI。
- [ ] 依赖capture真机与tracker离线通过。验证初始化任一步失败回滚，旧session/scene和重复seq拒绝，退出最终目标不丢，UI保守互斥与生命周期停止。
- [ ] 使用完整InkeysRepo.sln Debug|ARM64及定向测试验证，检查首个意义错误与退出码。MSBuild同PowerShell先规范化PATH、MSBUILDDISABLENODEREUSE=1；超时至少5分钟。
- [ ] trellis-check审查，记录文件、关键实现、命令/退出码、结果及遗留。

## 验证结果
尚未执行。
