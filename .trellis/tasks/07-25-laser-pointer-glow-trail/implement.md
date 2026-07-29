# 发光激光笔实施计划

## Implementation Order

1. 扩展工具枚举、键 4、工具名称/宽度/光标选择和运行时设置 API；加入 Laser session/lifecycle 纯逻辑与单元测试。
2. 将 renderer 的两张 Laser RGBA8 资源改为稳定预乘颜色层和单笔 coverage scratch；实现局部覆盖清零、MAX coverage、单笔材质解析、稳定颜色解析、resize/clear/release 和 CPU/GPU 常量契约。
3. 接入多 contact 主循环：按 Down 顺序保留未烘干层，逐笔覆盖到 scratch 后有序 source-over；较早结束的 contact 保留最终 CPU 几何，最后 Up 时依序烘入稳定颜色。保持无 reconnect/no L2、deadline wait 和完整 dirty rect。
4. 保留共享 Laser 尺寸契约、LaserDot Hover/Touch 笔尖和压力实体半径；用独立 GPU 粒子模块替换 CPU 粒子，接入固定 UAV/SRV、追加真实路径、Update/Emit Compute Shader、实例化绘制、线程安全开关与无粒子降级。
5. 完善 Pen authority/leave/re-entry 光标状态，补充可变半径 bounds 与 112-byte shader 契约。
6. 补充自动测试和 D3D11 静态契约审查；人工视觉与 Release 性能指标留待后续专项验证。

## Validation Commands

- 使用 ARM64 `MSBuild.exe` 完整构建 `inkStrokeModelerTest.sln /t:Rebuild /p:Configuration=Debug /p:Platform=ARM64`，确认 VS/PS 与两个 CS 均成功编译并嵌入资源。
- 运行 `ARM64\\Debug\\inkStrokeModelerTestTests.exe`。
- 静态核对 CPU/HLSL 镜像尺寸、固定容量、FXC `cs_5_0` 产物、UAV/SRV 解绑、绘制顺序、真实路径来源和开关降级路径。
- 本轮按用户补充要求不启动 GUI 或指标场景；Release、视觉观感、ULW 强制回退和 Windows 7 运行验证留待对应环境完成。
- 运行 `git diff --check`，检查 shader/source 保持 UTF-8 BOM + CRLF，核对无关 `Vcpkg/` 未被修改。

## Review Gates

- PRD 所有 acceptance criteria 有测试或明确人工证据。
- Laser 稳定颜色与 scratch 在逐笔解析、批次烘干和普通 operator layer 合成中没有 SRV/RTV 冲突，失败/resize 路径释放完整。
- 清屏、fade 完成、粒子关闭、tip 隐藏均包含旧 bounds，透明背景无残影。
- 没有把 prediction 或 Laser coverage resolve 到 L2，也没有为 Laser 启用 reconnect。

## Static Validation Evidence

- 2026-07-29：ARM64 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Rebuild` 成功，VS、PS、`UpdateCS`、`EmitCS` 四个 Shader 均由 FXC 成功生成资源产物。
- 2026-07-29：`ARM64\Debug\inkStrokeModelerTestTests.exe` 全部通过，覆盖默认开启、Down 无爆发、距离发射/96 粒限额、速度平滑、弧长无追赶、generation/溢出、75/25 收束及保守脏区到期。
- 2026-07-29：静态检查确认粒子缓冲在 Compute/VS 间显式解绑、固定 `DrawInstanced(6, 2048, 0, 0)`、仅上传 `realPoints`，且绘制顺序为激光主体之后、尖端之前。
- 2026-07-29：`git diff --check` 通过；源码/HLSL/工程保持 UTF-8 BOM + CRLF，资源脚本保持 ASCII + CRLF，Trellis 文档保持 UTF-8 + LF。
