# 任务栏轮询降频验证（2026-10-19）

## 本轮要求与变更
用户在3e5bb005提交后要求保留WinEvent钩子，并将所有任务栏周期轮询间隔调到至少5秒。本轮Display.cpp仅两个数字修改和一句中文注释（3+/2-）：taskbarFallbackTimer从200ms改5000ms；taskbarStructureTimer从2000ms改5000ms。底部autoHide存在时才武装显隐兜底的条件、owner线程与cleanup未变。

WinEvent过滤/投递、TaskbarCreated、WM_DISPLAYCHANGE/WM_DEVICECHANGE/WM_SETTINGCHANGE仍事件触发刷新。25ms计时仅在事件后启动，处理时KillTimer，为单次合并而非周期轮询，保持不变。未改其他应用模块的恢复/重试计时器。

## 验证结果
- 新完整InkeysRepo.sln Debug|ARM64构建exit0，14.34秒，0errors/0warnings。按vswhere定位ARM64原生MSBuild，同一PowerShell规范化PATH并设置MSBUILDDISABLENODEREUSE=1，900秒监督。Inkeys和Headless均重新编译Display.cpp并链接新产物。
- git diff --check通过；9个既有修改文件按Git checkout filters核对原BOM及UTF-8/CRLF，未产生无关格式diff。源码审计确认Display任务栏周期采样仅上述两处5000ms，25ms单次事件合并及其他路径保持。
- Trellis check通过，未发现需修复问题；两处周期定时器及25ms单次事件合并已沿调用和清理路径核对，见research/polling-check.md。
- 本轮未新增只镜像间隔常量的测试；不重跑未受影响的算法全套Headless或配置边界入口，旧测试pass不作为本轮执行证据。现有无窗口算法测试无法验证真实USER32定时器交付时刻，因此本轮以当前source审阅、最小diff及新完整构建为适用验证。
- 没有GUI/Computer Use、真实Shell漏事件或CPU百分比测量。漏事件/钩子不可用时兜底可能等待到下一次5秒采样；系统消息正常时仍走事件路径。

## 日志与状态
本轮日志、process.json及format-audit.json保存于忽略目录.trellis/.runtime/ui3-taskbar-polling-build，与前轮隔离。日志仍有既有vcpkg探测pwsh.exe不可见消息，既有PowerShell回退后完整构建exit0；未修改项目、依赖或环境配置绕过。

上一轮commit授权已用于3e5bb005。本轮追加降频未自动commit/push，任务保持in_progress。

## 最终提交与结案授权
2026-10-19：用户明确要求“为改动提交commit并结束相关任务”。按本轮已通过的Trellis check及完整Debug|ARM64构建提交5秒轮询调整，随后归档本父任务及10-19-taskbar-dock-follow、10-19-canvas-edge-fullscreen-policy两个子任务并记录会话。用户接受当前交付，不将本次结案视为已执行GUI/性能测量；不push。

## 已提交并归档
用户本次明确授权后，降频工作已提交277b4956；父任务及两个子任务均已于2026-10-19归档，状态completed，当前会话任务指针已清除。归档后context manifests已更新为archive路径并重新校验，保留上述自动验证与未实测边界；未push。
