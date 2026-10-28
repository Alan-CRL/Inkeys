# 实施与验收

1. 记录原工作区、分支与文件格式；整合固定发布基线，分类解决冲突。
2. 并行分工：设置实现负责Setting.cpp/Pages/相关UI；整合负责工程、Window、配置/资源/测试依赖；研究只产出逐页对照。
3. 合并完成后记录配置/UI/FIFO逐页映射，覆盖删除的预设入口及新增功能依赖。
4. ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64，至少900秒；同一PowerShell invocation规范PATH并设置MSBUILDDISABLENODEREUSE=1，/m:1 /nr:false。
5. 执行相关--no-window测试、i18n和git diff --check；独立Trellis check复核。
6. 前置通过后才开始侧栏子任务；不自动提交、不启动GUI，保留整合差异可审查。
