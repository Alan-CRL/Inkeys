# UI3 白板执行计划

## Ordered Checklist

1. 扩展 Draw3 bridge、Host snapshot 和 DrawingController workspace/page runtime；加入白板输出目标与平移门控。
2. 扩展 WindowRole、owner/ready/hide 映射和持久 topmost 模式，修正周期性 TopWindow 强制 TOPMOST。
3. 新增 `Inkeys.UI.Whiteboard` UI3 模块、固定左右控件、Freeze 背景客户端和资源加载。
4. 在主线程注册白板窗口和 UI3 client，接入 StateMonitoring 的进入/退出事务与 PPT 同步门控。
5. 扩展 BarButton、A2 migration、动态图标/文字/尺寸和白板专用底栏动画锁。
6. 修改 FreezeFrameWindow 的 legacy 提交门控，并接入白板背景互斥。
7. 添加 headless tests，覆盖 Draw3 workspace/page、Whiteboard layout/state、Window topmost、Bar migration/dock 和 PPT visibility。

## Validation

- `git diff --check`
- 运行 InkeysHeadlessTests 相关测试目标，优先覆盖已有 Draw3、Window、PPT、Bar bottom dock 测试。
- 使用 ARM64 `MSBuild.exe` 编译 `InkeysRepo.sln`，配置 `Debug|ARM64`，超时至少 5 分钟。
- 只执行静态或无窗口测试，不启动可见窗口。

## Risk Gates

- Draw3 workspace 切换必须先通过 active contact 延迟切换测试，再接 UI 事务。
- Window Service owner/topmost 改动必须先通过 Window headless tests，避免周期刷新回退为 TOPMOST。
- Freeze legacy/UI3 双写门控必须在进入、退出和关闭路径分别验证。
- Bar 底栏专用锁必须与普通拖动/收起测试同时验证，避免改变非白板行为。
