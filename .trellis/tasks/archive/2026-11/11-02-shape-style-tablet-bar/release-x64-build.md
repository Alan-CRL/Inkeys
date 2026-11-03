# Release x64 测试构建

用户明确要求Release|x64，覆盖本任务平时Debug|ARM64验证之外的手动测试产物。使用既有InkeysRepo.sln及vswhere定位的ARM64原生MSBuild，Configuration=Release/Platform=x64，未修改vcxproj、依赖或工具链；单次构建上限1800s。同调用规范重复Path/PATH并设置MSBUILDDISABLENODEREUSE=1。

- 构建退出码0，353.94s，日志Build succeeded / 0 Error(s)。正常LTCG完整生成59689函数；保留项目Release默认Harness关闭。
- 产物：Build/x64/Release/Inkeys.exe，39836672字节，PE Machine=0x8664（AMD64/x64）。
- SHA256：7B5AA92265E7DC73D25A87F0A87375FC967CE28FD6C1E37B89A1841C683A60B9。
- 静态检出ui_schema、ui_input v1、Move限频及lease拒绝诊断标识。完整schema常量在Release优化后可能被拆分，整串ASCII查找不是功能缺失判据；未为字符串查找改产品代码。
- 详细构建/结构化结果/产物元数据在ignored TestResults/tablet-ui-diagnostics-x64/。
- 本次未运行GUI或启动x64主程序；此前相同源码已通过Debug ARM64及无窗口测试，不将它写作x64实机验证。
- CMD启动指令和三步采集流程见release-x64-capture.md。InitDebugConsole保留文件/管道标准输出与错误句柄，两个流均重定向时不新建诊断控制台。
- 本轮只构建与写操作说明，无产品代码改动，未commit/push。
