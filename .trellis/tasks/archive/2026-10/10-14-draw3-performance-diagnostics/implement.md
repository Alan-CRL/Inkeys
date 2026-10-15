# Draw3 性能诊断执行计划

## 任务状态与授权

用户在2026-10-14请求中已批准规划、实现至最终；无需额外规划确认。需求/设计/实现计划落盘并自审后才 task.py start。当前工作树由多个实现子代理共享，不回滚他人编辑；主代理统一构建和收尾。不commit、不push。

## 实施顺序与文件所有权

1. [x] 完成research/render-pipeline.md、input-latency.md、package-and-config.md；核对关闭路径、proof、SuperTop及PS兼容合同。
2. [x] 主会话完成PRD收敛、design/implement和implement.jsonl/check.jsonl，validate并start。
3. [x] telemetry implementer独占Inkeys/Inkeys/Drawing/Draw3下诊断新文件、Host、Controller、RTS、ContactInput、Renderer/TransparentPresentation/GraphicsInitialization/Diagnostics相关接线；新增纯CPU probe由该代理实现。先发布文件和CSV/JSON接口合同给其他代理。不编辑IdtMain、工程/i18n/配置或Scripts。
4. [x] startup/config implementer独占IdtMain.cpp、Other.Config schema/设置实验页面、i18n相关文件与Inkeys.vcxproj必要新增登记；消费telemetry契约。实现argv/实验路径、SuperTop透传、最终进程launch/completion握手、早期无GUIprobe分发。不编辑Draw3或Scripts。
5. [x] package implementer独占Scripts/Draw3Performance下CMD/PS/README/离线测试。先实现硬件、启动生命周期与离线报告，随telemetry/schema更新解析。不编辑产品源码或工程。
6. [x] 主会话核对跨文件接口/输出文件、保持编码换行、原有设置不受显式启用污染；静态检查与必要集成修复交回所属代理。
7. [x] Trellis check子代理核查默认关闭、生命周期、计量真值、数组满、失败/异常/缺失、脚本安全/兼容和真实生产调用，安全局部自修复。
8. [x] 主会话完整Solution构建及所有适当无窗口验证，修复本次源码问题后仅复测有变化或失败的检查。
9. [x] 主会话补native-desktop性能诊断规范、验收记录与任务状态，record-session/archive均用--no-commit。

## 最小验证

- git diff --check，并比较修改文件BOM/CRLF与HEAD，禁止大面积格式变化。
- PowerShell Parser.ParseFile解析所有新ps1；脚本使用Windows PowerShell兼容API，不只在pwsh下验证。
- Scripts/Draw3Performance离线合成fixtures测试（成功/长帧/idle/失败/掉样/不完整/过期输入/无贡献）。
- 既有i18n工具sync/check；仅修改这次新键/生成资源。
- 当前VS安装vswhere定位ARM64 MSBuild，同一invocation执行 Remove-Item Env:PATH -ErrorAction SilentlyContinue 和 MSBUILDDISABLENODEREUSE=1，然后 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /nr:false。允许至少5分钟；保留日志/退出码，定位第一个实际错误。
- Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window。
- Build/ARM64/Debug/Inkeys.exe --draw3-performance-test（计量器/序号/proof/容量/QPC/null门控，初始化GUI前返回）。
- 若支持小型普通CPU模块独立测试可附加，不能替代solution构建。

## 风险与集成审查

SuperTop最终进程身份和会话握手、startup失败的多Host导出、shutdown先停RTS再join读取、diagnostics off不能读新时钟/分配、live/Up实际内容proof、ULW Readback计时不引入额外copy/query、GPU query不Flush/等待、连续活动与idle间隔区分、输出容量drop分母、release中正式性能开关可用。不得为验证启动任何GUI。任何本机工具链问题单列，不更改SDK/依赖/签名配置绕过。
