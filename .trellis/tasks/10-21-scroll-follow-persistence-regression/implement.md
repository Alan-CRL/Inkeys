# 滚动批注保存与完整回归实施记录

## 顺序
- [ ] 读取父prd/design/implement及本任务规范上下文。
- [ ] 针对已接入功能补齐UInk往返与编辑历史、ARM64/x64完整构建及真实受控GUI矩阵，记录最终参数/实测/未测和Console方法。
- [ ] 依赖产品接入。A->scroll->B->eraser/Undo/Redo->Clear或Exit保存->加载->再scroll；完整检查与回归按父implement。
- [ ] 使用完整InkeysRepo.sln Debug|ARM64及定向测试验证，检查首个意义错误与退出码。MSBuild同PowerShell先规范化PATH、MSBUILDDISABLENODEREUSE=1；超时至少5分钟。
- [ ] trellis-check审查，记录文件、关键实现、命令/退出码、结果及遗留。

## 验证结果
尚未执行。
