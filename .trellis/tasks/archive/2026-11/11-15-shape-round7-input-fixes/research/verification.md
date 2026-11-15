# 第七轮验证记录

## 构建与环境
- 基线 bc42dd03；本轮未提交或推送，父/子任务保持 in_progress。
- 初次受限 MSBuild 超过八分钟反复启动空闲节点，未启动 cl。核实本轮主进程及命令行后仅终止该构建树。无源码/配置绕过。
- 使用 vswhere 发现 ARM64 原生 MSBuild，同一 PowerShell 应用 PATH workaround，原 InkeysRepo.sln Debug ARM64 正常权限构建 48.80 秒退出 0；最终源码重建 18.45 秒退出 0。
- 最终构建保留 11 项既有数值转换 warning。附属步骤既有 pwsh.exe 不可见信息不影响构建退出码；未改 SDK、工具链或工程配置。

## 自动验证
| 验证 | 结果 | 日志 Build/shape-round7 |
| --- | --- | --- |
| 最终完整 solution | 退出 0 | build-final.log |
| 几何面板离屏及原生 Touch Probe | 1716680 checks / 144 frames / 0 failures | geometry.log |
| 原生输入路由 | 退出 0 | native-input.log |
| 输入 worker（含80条Move合并、来源分界） | 退出 0 | input-worker.log |
| 产品形状核心 | 退出 0 | core.log |
| 工具状态 | 退出 0 | tool-state.log |
| 编辑器离屏最终复测 | 544 scenes / 7602 commits / 317275 checks / 0 failures，退出 0 | editor-final.log |
| Headless --no-window（额外全范围） | 达到 600 秒上限，未取得最终结果 | headless.log |

完整 headless 在速度橡皮阶段达到 600 秒上限；已输出未见失败，但不能声称全套通过。本轮相关生产输入/几何专项均完成且通过，未为额外全量验证反复启动同一测试或修改代码规避超时。

## 验证修正
初次扩展逐柄回归暴露测试夹具没有模拟 Draw3 抬手确认：CancelPointer 正确保留已提交 localPending，同代次快照不能将其清退，旧预览污染后续柄。现按生产方式克隆权威快照推进 generation，并按每次已呈现快照重取柄位置；所有四角和 3D 底面柄均强制命中与实际 geometry/parts 变化。复合立体以 parts 为几何真值，不能只比较空 geometry。单移动钮没有胶囊左右顺序，顺序断言仅用于双按钮胶囊且沿实际停靠边判断，超屏换边合同保留。

## 范围审查
- 独立审查通过：入场输入门、触摸同帧提交、队列身份/终态、翻转停靠及重新选中。
- Circle 两项翻转隐藏；矩形/椭圆等仍保留。离屏截图已检查。
- 保持原 BOM 与 CRLF，无混合换行；Git 索引 LF / 工作树 CRLF 沿原 core.autocrlf。diff --check 通过。
- 本轮不改形状模型、存储和历史格式，产品核心回归覆盖其集成；未无条件重建未修改的共享核心 solution。

## 实机边界
没有启动交互 GUI 或 Computer Use。测试证明已呈现准入、真实接触动作、几何候选、队列合并与终态；真实触控/屏幕笔延迟及预测观感仍需用户实机复核，不以队列计数冒充时延测量。

最终产物：Build/ARM64/Debug/Inkeys.exe
SHA256：476DE64A459D17767ED0C456E12376BA6DF97201548DDB613CFA6E02E71E37E7
