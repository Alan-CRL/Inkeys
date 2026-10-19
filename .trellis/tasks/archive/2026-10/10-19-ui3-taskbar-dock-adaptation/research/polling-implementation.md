# 任务栏轮询间隔追加调整

2026-10-19：按用户最新要求，保留 WinEvent 钩子，并把任务栏全部周期轮询调整为 5000ms。

- `Inkeys/Inkeys/Display/Display.cpp:878`：仅缓存底部 autoHide 存在时武装的显隐 fallback，200ms 改为 5000ms。
- `Inkeys/Inkeys/Display/Display.cpp:898`：结构复查，2000ms 改为 5000ms。
- `Display.cpp:801`：添加简短中文注释，区分周期兜底与 WinEvent 触发后的 25ms 单次合并。
- `Display.cpp:916/929`：事件专用合并定时器仍为 25ms，处理时先 KillTimer，未改动。WinEvent、TaskbarCreated 及系统消息路径保持事件响应；钩子安装/卸载、显隐轮询武装/停止条件及清理均保持。

静态审计确认 Display.cpp 只有以上两个周期 SetTimer 和一个事件合并 SetTimer，无额外 sleep/wait 周期采样路径。源码 diff 为 3 行新增、2 行删除；git diff --check 通过，原 UTF-8 BOM 和 CRLF 保持，无裸 LF/CR。本代理未运行构建、EXE、GUI、commit 或 push，主会话协调 Trellis check 与既有 Debug|ARM64 完整 solution 构建。

兼容边界：漏事件或钩子不可用时，纠正可能等待下一次 5 秒兜底；实际 Shell 时序及 CPU 占用尚无本轮实机测量。
