# UI3 边缘光影实验开关实施清单

## Implementation

- [x] 在 `Other.Config.cppm` schema 中加入 `EdgeLighting.Enable/Dynamic`。
- [x] 在 `Bar.Main.cppm/.cpp` 增加线程安全的运行时选项入口，并接入初始化、光影计算、绘制和第三光源跟踪门禁。
- [x] 在 `Setting.cpp` 增加两个实验选项卡片、条件显示、动态容器高度、即时应用与持久化。
- [x] 同步 native desktop 配置与 UI3 光影契约。

## Validation

- [x] 搜索所有新配置和运行时入口的生产者/消费者，确认没有重复状态源。
- [x] 静态检查总开关关闭仍绘制基础灰边，动态关闭仅影响第三光源。
- [x] 静态检查关闭路径注销 Raw Input，重新开启不主动注册。
- [x] 运行 `git diff --check`、编码/CRLF 检查和完整 `InkeysRepo.sln` `Debug | ARM64` 构建。

> 2026-07-24 使用 ARM64 Host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`，构建通过且未新增编译错误；`Other.Config.cpp` 随 schema 自动重新编译，无需字段特判。

## Risk and Rollback

- 设置 UI 高度是局部布局风险；按现有 70px 卡片与 5px 间距机械扩展。
- Bar 设置切换跨线程；只通过原子状态和现有窗口消息更新，不从 Setting 线程直接修改 D2D 或跟踪状态。
