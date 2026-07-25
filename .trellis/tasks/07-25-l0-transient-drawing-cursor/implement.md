# L0 瞬态绘制光标实施

## Ordered Checklist

- [x] 将 `draw3.pen_cursor` 从 GDI bitmap/HCURSOR 模块改为 appearance、sample、visual、policy 和 bounds 模块。
- [x] 扩展 RTS InAir/Down/Packets/Up 发布完整 Pen cursor sample，并保留 disabled/error/removal/shutdown 清理。
- [x] 扩展 WindowController 的 pointer authority、Pen/Mouse 快照、Mouse leave 跟踪、系统光标决策和合并唤醒。
- [x] 扩展 HLSL 和 InkRenderer，增加圆、矩形、EraserGripCircle 的瞬态 backbuffer 绘制入口。
- [x] 在 DrawingController 中解析 Pen/Mouse visual，并为每个活动 Touch eraser contact 生成不透明 visual；维护全部旧/新 bounds，接入 idle/active/full-present 路径，并保证 L2 resolve 前后都不包含 cursor。
- [x] 替换 cursor 自动测试，覆盖策略矩阵、多 Touch 橡皮集合、参数、bounds、RTS 最新包和静止无重复呈现约束。
- [x] 更新 native/shader 规范，记录应用渲染路径和硬件光标退役原因。

## Validation

1. `git diff --check`
2. 静态搜索 `CreateIconIndirect|DestroyCursor|SetSystemCursor|ShowCursor`。
3. 使用 ARM64 MSBuild 构建 `InkeysRepo.sln` 的 `Debug|ARM64`，超时不少于 5 分钟。
4. 运行 `.\\ARM64\\Debug\\inkStrokeModelerTestTests.exe`。
5. 真机验证 Pen/Highlighter/Eraser、笔尾、Mouse、Touch、窗口边界、SDR/HDR 和白/红/黑背景。

## Risk And Rollback Points

- HLSL 常量布局必须保持 C++/HLSL 16 字节对齐；shader 编译和嵌入 CSO 是独立检查点。
- dirty rect 必须包含旧 cursor bounds，否则出现残影；必须在所有 L2 resolve 之外绘制。
- RTS 与窗口消息可并发写样本，读取必须使用 sequence 一致性协议。
- 若 Hover pacing 影响 Down 延迟，优先恢复即时 Down 唤醒，不恢复彩色 `HCURSOR`。
