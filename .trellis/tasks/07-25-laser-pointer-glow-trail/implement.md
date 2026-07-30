# 发光激光笔实施计划

## Implementation Order

1. 扩展工具枚举、键 4、工具名称/宽度/光标选择和运行时设置 API；加入 Laser session/lifecycle 纯逻辑与单元测试。
2. 将 renderer 的两张 Laser RGBA8 资源改为稳定预乘颜色层和单笔 coverage scratch；实现局部覆盖清零、MAX coverage、单笔材质解析、稳定颜色解析、resize/clear/release 和 CPU/GPU 常量契约。
3. 接入多 contact 主循环：按 Down 顺序保留未烘干层，逐笔覆盖到 scratch 后有序 source-over；较早结束的 contact 保留最终 CPU 几何，最后 Up 时依序烘入稳定颜色。保持无 reconnect/no L2、deadline wait 和完整 dirty rect。
4. 保留共享 Laser 尺寸契约、LaserDot Hover/Touch 笔尖和压力实体半径；独立 GPU 粒子模块使用固定 UAV/SRV、真实前缀加可替换 L0 prediction 尾、时间发射、固定 3 秒生命周期、Update/Emit Compute Shader、实例化绘制、线程安全开关与无粒子降级。
5. 完善 Pen authority/leave/re-entry 光标状态，补充可变半径 bounds 与 112-byte shader 契约。
6. 补充自动测试和 D3D11 静态契约审查；人工视觉与 Release 性能指标留待后续专项验证。

## Validation Commands

- 使用 ARM64 `MSBuild.exe` 完整构建 `inkStrokeModelerTest.sln /t:Rebuild /p:Configuration=Debug /p:Platform=ARM64`，只编译测试源码，并确认 VS/PS 与两个 CS 均成功编译并嵌入资源。
- 本轮不运行任何 EXE（包括测试程序），不启动 GUI、指标场景、浏览器或电脑控制。
- 静态核对 CPU/HLSL 镜像尺寸、固定容量、FXC `cs_5_0` 产物、UAV/SRV 解绑、绘制顺序、组合路径来源、时间寿命和开关降级路径。
- 自动测试运行、Release、视觉观感、ULW 强制回退和 Windows 7 运行验证留待用户后续验证。
- 运行 `git diff --check`，检查 shader/source 保持 UTF-8 BOM + CRLF，核对无关 `Vcpkg/` 未被修改。

## Review Gates

- PRD 所有 acceptance criteria 有测试或明确人工证据。
- Laser 稳定颜色与 scratch 在逐笔解析、批次烘干和普通 operator layer 合成中没有 SRV/RTV 冲突，失败/resize 路径释放完整。
- 清屏、fade 完成、粒子关闭、tip 隐藏均包含旧 bounds，透明背景无残影。
- 没有把 prediction 或 Laser coverage resolve 到 L2，也没有为 Laser 启用 reconnect。

## Static Validation Evidence

- 2026-07-29（上一阶段）：ARM64 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Rebuild` 成功，VS、PS、`UpdateCS`、`EmitCS` 四个 Shader 均由 FXC 成功生成资源产物。
- 2026-07-29（上一阶段）：`ARM64\Debug\inkStrokeModelerTestTests.exe` 全部通过；本轮按用户要求不再次运行。
- 2026-07-29（上一阶段）：静态检查确认粒子缓冲在 Compute/VS 间显式解绑、固定 `DrawInstanced(6, 2048, 0, 0)`，且绘制顺序为激光主体之后、尖端之前。
- 2026-07-29（本轮）：ARM64 原生 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Build` 成功；VS、PS、`UpdateCS`、`EmitCS` 均由 FXC 成功生成 `.cso`，新增测试源码成功编译。
- 2026-07-29（本轮）：严格遵守静态验证边界，没有运行主程序或测试 EXE，没有启动窗口或执行电脑控制；运行时视觉观感仍待用户后续验证。
- 2026-07-30（粒子调优）：法线额外发散由 `4.5 DIP` 提高到 `10 DIP`；异常跳变与预测回缩改为保持修正状态，并按当前正常粒子速度约 `2×` 限速追赶至目标。
- 2026-07-30（粒子调优）：ARM64 原生 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Rebuild` 成功，VS、PS、`UpdateCS`、`EmitCS` 四个 Shader 均由 FXC 成功生成资源产物；只编译/链接测试工程，未运行任何 EXE、窗口或电脑控制，运行时观感仍待用户验证。
