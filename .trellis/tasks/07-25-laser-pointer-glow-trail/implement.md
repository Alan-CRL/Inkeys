# 发光激光笔实施计划

## Implementation Order

1. 扩展工具枚举、键 4、工具名称/宽度/光标选择和运行时设置 API；加入 Laser session/lifecycle 纯逻辑与单元测试。
2. 将 renderer 的两张 Laser RGBA8 资源改为稳定预乘颜色层和单笔 coverage scratch；实现局部覆盖清零、MAX coverage、单笔材质解析、稳定颜色解析、resize/clear/release 和 CPU/GPU 常量契约。
3. 接入多 contact 主循环：按 Down 顺序保留未烘干层，逐笔覆盖到 scratch 后有序 source-over；较早结束的 contact 保留最终 CPU 几何，最后 Up 时依序烘入稳定颜色。保持无 reconnect/no L2、deadline wait 和完整 dirty rect。
4. 实现共享 Laser 尺寸契约、LaserDot Hover/Touch 接触笔尖、压力实体半径和确定性曲线粒子；接入粒子开关和动态留存秒数设置。粒子采用 12-18 枚起笔、8-12px 弧长间隔、48 枚上限和 8-36px/s 流速，Up 最近路径点收束。
5. 完善 Pen authority/leave/re-entry 光标状态，补充可变半径 bounds 与 112-byte shader 契约。
6. 补充自动测试、D3D/透明路径人工验证和 Release 性能指标；按 Trellis check 修正全部问题。

## Validation Commands

- 使用 ARM64 `MSBuild.exe` 完整构建 `inkStrokeModelerTest.sln /t:Rebuild /p:Configuration=Debug /p:Platform=ARM64`。
- 运行 `ARM64\\Debug\\inkStrokeModelerTestTests.exe`。
- 使用 ARM64 `MSBuild.exe` 完整构建 `Release|ARM64` 并运行 Release 测试。
- 运行应用指标场景，核对 Down-to-Present、活动帧间隔、粒子开/关和静态留存零帧增长。
- 运行 `git diff --check`，检查 shader/source 保持 UTF-8 BOM + CRLF，核对无关 `Vcpkg/` 未被修改。

## Review Gates

- PRD 所有 acceptance criteria 有测试或明确人工证据。
- Laser 稳定颜色与 scratch 在逐笔解析、批次烘干和普通 operator layer 合成中没有 SRV/RTV 冲突，失败/resize 路径释放完整。
- 清屏、fade 完成、粒子关闭、tip 隐藏均包含旧 bounds，透明背景无残影。
- 没有把 prediction 或 Laser coverage resolve 到 L2，也没有为 Laser 启用 reconnect。
