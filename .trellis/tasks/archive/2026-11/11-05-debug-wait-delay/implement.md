# 实施计划

1. 用户已批准最终方案；已 task.py start，通过 Trellis 实施/检查代理执行。
2. 加载配置、启动预览、白板、PPT 会话与线程/呈现规范；追踪接纳、等待、真实回执及取消路径，确定注入点。
3. 最小增加配置、设置开关与国际化文案，接入启动慢速成功入口；文案简短正式，不扩展 SuperTop 适配。
4. 加入带请求身份的白板/PPT 非阻塞延迟；仅覆盖主动点击入口及有效新增页，不修改业务结果和正常时序。
5. 用单调时钟可控的无窗口测试核对关闭、10s 截止、取消/旧会话和失败；复用现有 harness，避免 GUI。
6. 定位实际 VS ARM64 原生 MSBuild，在同一 PowerShell invocation 先 Remove-Item Env:PATH 和设置 MSBUILDDISABLENODEREUSE=1，再构建 InkeysRepo.sln Debug|ARM64，允许至少 5 分钟。
7. 检查退出码、关键输出及 diff 的编码/换行/范围；不提交、不 push。

风险：人为延迟不能消耗真实故障超时预算；不能堵塞共享 worker；真实完成前不得伪造 ready。
