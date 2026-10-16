# 执行计划

1. 将已批准最终计划及本轮补充落盘；curate manifests；task.py validate/start。
2. 三个trellis-implement代理分别renderer、settings/startup、v3诊断/包；读spec后直接实施，不再嵌套代理。不得并行完整构建。
3. 主会话集成接口与EOL/diff，动态定位VS ARM64 MSBuild；同PS先Remove-Item Env:PATH、MSBUILDDISABLENODEREUSE=1；完整Solution Debug|ARM64，至少5分钟。先首错分类，环境问题不改源码绕过。
4. 运行确认GUI前的config/performance/resource CPU入口、headless --no-window、脚本WindowsPS5.1/pwsh fixtures、有限无HWND离屏上传验证与对照。保留stdout/stderr/自然exit，超时不反复重启。
5. 全范围trellis-check代理自修review；按必要变更复验。
6. Release|ARM64/x64完整solution构建，复用Build-Package生成v3双架构，验证zip七文件/PE/manifest/hash、早期CLI。未经确认无窗口入口不执行。
7. spec更新实际上传/配置/v3合同，写validation与交付说明；任务完成后归档/记录日志，不commit/push。

Trellis Python通过注册表ExecutablePath定位；沙箱WindowsApps denied，已获auto-review允许该本地脚本执行。任务路径.trellis/tasks/10-15-draw3-zhaoxin-diagnostics-v3。