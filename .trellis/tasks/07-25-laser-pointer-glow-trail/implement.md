# 发光激光笔实施计划

## Implementation Order

1. 扩展工具枚举、键 4、工具名称/宽度/光标选择和运行时设置 API；加入 Laser session/lifecycle 纯逻辑与单元测试。
2. 扩展 renderer 的 stable/live Laser coverage 资源、MAX coverage 写入、resolve pass、resize/clear/release 和 CPU/GPU 常量契约。
3. 接入多 contact 主循环：Laser 模型输入、stable commit、live prediction、Up 收尾、无 reconnect/no L2、deadline wait 和 dirty rect。
4. 实现 LaserDot Hover/接触笔尖与确定性稀疏粒子，接入粒子开关和动态留存秒数设置。
5. 补充自动测试、D3D/透明路径人工验证和 Release 性能指标；按 Trellis check 修正全部问题。

## Validation Commands

- 使用 ARM64 `MSBuild.exe` 完整构建 `inkStrokeModelerTest.sln /t:Rebuild /p:Configuration=Debug /p:Platform=ARM64`。
- 运行 `ARM64\\Debug\\inkStrokeModelerTestTests.exe`。
- 使用 ARM64 `MSBuild.exe` 完整构建 `Release|ARM64` 并运行 Release 测试。
- 运行应用指标场景，核对 Down-to-Present、活动帧间隔、粒子开/关和静态留存零帧增长。
- 运行 `git diff --check`，检查 shader/source 保持 UTF-8 BOM + CRLF，核对无关 `Vcpkg/` 未被修改。

## Review Gates

- PRD 所有 acceptance criteria 有测试或明确人工证据。
- Laser coverage 与普通 operator layer 没有 SRV/RTV 冲突，失败/resize 路径释放完整。
- 清屏、fade 完成、粒子关闭、tip 隐藏均包含旧 bounds，透明背景无残影。
- 没有把 prediction 或 Laser coverage resolve 到 L2，也没有为 Laser 启用 reconnect。
