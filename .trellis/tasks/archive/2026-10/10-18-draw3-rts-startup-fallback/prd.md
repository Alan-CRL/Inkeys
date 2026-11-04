# Draw3 产品兼容修复迁移

用户最新指令：从诊断分支建立中转分支，将兆芯兼容及 RTS/鼠标回退等产品修复迁往 dev，不包含新增诊断信息，准备面向 dev 的 PR。沿用当前已获批准的回退任务，不改变输入行为方案。

## 范围

- 基于已通过GitHub只读API核实的dev 5d319bbf创建codex/draw3-compatibility-fixes；原诊断分支保留。
- 保留硬件加速偏好、兆芯兼容设置（默认开）、实际硬件VendorId=0x1D17的结构buffer DEFAULT更新路径、通用上传统一及composite常量256B拆分；迁移所需配置/UI/三语言资源。
- 保留Extended→BasicXY→Win32Mouse及所有回调停止、owner队列屏障、保存/代次隔离；去掉PerformanceSession等新诊断依赖。
- 排除42745199诊断体系、采集器、探针、启动诊断CLI、诊断包脚本、ConsoleOutput.Performance和诊断历史任务/日志。dev原有诊断代码不属于本次删除范围。
- 不运行GUI。完整ARM64 solution与--no-window；新增设备选择/上传测试若有均须无HWND。
- PR以dev为base，不自动merge。按仓库规则，push需用户明确授权；先完成可审查改动、验证和PR说明。
