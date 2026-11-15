# P2 执行检查表
- [x] 模板/参数模型与状态投影
- [x] Down快照、预测、raw Up、dirty bounds与一次提交
- [x] UI3 11图形、参数、SVG轮播与属性
- [x] v1/v2保存/恢复、桌面/PPT/白板冷页redo
- [x] 全量集成与Trellis check
构建vswhere定位ARM64 MSBuild，同PowerShell规范化PATH，至少五分钟，不并行多次完整构建。GUI子系统CLI用Start-Process Hidden Wait取实际退出码。统一运行core/GPU/UI/保存/Headless/i18n/diff，检查编码换行。保留真机未测风险。不commit/push。

## 完成记录
最终build04和所有CPU/保存/GPU/UI3/Headless/i18n验证通过，详见validation.md；源码经Trellis check。改动保留工作区，不commit/push。
