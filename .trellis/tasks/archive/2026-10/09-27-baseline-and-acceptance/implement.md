# 执行清单

前置：父任务规划独立审查通过。出口：四基准定位状态、环境/回归/性能协议、审计集合与基线验证记录均已冻结；不以未知 HC/H2 伪造比较。

- [x] 核对任务前 H0 快照、远端 refs、submodule 与当前活动任务；记录环境元数据。WMI 补丁清单未取到，已在来源研究标缺口。
- [x] 查 HC/H2 的 release、tag、构建元数据、既有日志/任务；写候选和不确定性。用户实际安装 Canary/二进制内层散列未验证。
- [x] 读现有 UI3 diagnostics 与测试覆盖；写 UI3/Draw3 场景、warm-up、三轮以上和统计口径。整帧和真实笔路径仍需后续人工。
- [x] 写动作回归表、性能门槛、审计集合生成方法与初始候选清单。611 行仅为审计候选，逐项审查另计。
- [x] 基线完整 Solution Debug/Release|ARM64、无窗口/PptCOM 测试和算法/输入子链采样；退出码和限制已入父任务 validation/performance。独立规划及 benchmark diff review 已完成，允许 Work 1；GUI/HC/H2 实测仍是门禁。
- [x] 建严格无窗口的 Draw3 生产 ContactInput 基准入口并完成三轮输入子链采样；正式 Host/笔输入/呈现仍待 Work 3 与人工设备。
