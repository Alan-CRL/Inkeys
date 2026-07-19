# Draw3 运行时全量验证

## Goal

为真实 `draw3` 输入、建模和绘制链建立无第三方测试框架的自动化测试入口，并增加默认零开销的 Release 运行指标，使软件落笔延迟、活动帧稳定性、唤醒正确性和空闲阻塞可以重复测量。

## Requirements

### R1. 独立测试工程

- 新增 `inkStrokeModelerTestTests` 控制台工程，直接链接并重新编译生产 `draw3` 模块源码，不复制算法实现。
- 工程加入解决方案 Debug/Release 的 x86、x64、ARM64 配置。
- 使用轻量自有断言/用例注册，不引入测试框架或新的 vcpkg 依赖。
- 测试专用编译宏只暴露容量注入和只读状态检查，不扩大正式运行 API。

### R2. 可中断活动帧等待

- 活动期等待由 Windows 7 可用 event 和原子 wake generation 共同驱动。
- 新 Down、Up/Cancelled 与控制请求必须打断当前帧等待；Move 仍按 120 FPS 合并消费，不能由高频 packet 触发无界渲染。
- 无活动 contact 时继续使用 `BlockingConcurrentQueue` 零自旋阻塞，不引入轮询或 busy wait。

### R3. 可选运行指标

- Release 主程序支持显式参数启用指标会话和 JSON 输出；关闭时不得分配指标缓冲或写文件。
- 记录设备/工具、Down（荧光笔为首次可见资格）到首次成功 Present 的软件延迟、活动帧间隔、工作/Present 耗时、Move 合并/争用、Down/终态/回收、空闲 frame/Present 和等待计数。
- 提供只读诊断快照；原始报告写入忽略的 `TestResults/`，仓库只保存环境、门槛和分位数摘要。

### R4. 自动鼠标基准

- 测试驱动启动 Release 主程序，通过 `SendInput` 执行 200 次落笔，以及直线、曲线、240Hz Move、停笔、普通笔、荧光笔、橡皮、resize、clear 和按键 `9` 正常退出。
- 连续三次有效运行均满足：
  - 落笔软件延迟 p99 不高于 8.33ms；
  - 活动帧间隔 p99 不高于 9.5ms；
  - 超过 16.67ms 的活动帧少于 1%；
  - 空闲 5 秒 frame/Present 计数不增长，绘制线程保持零自旋阻塞。

### R5. 真机矩阵与环境边界

- Pen 100 次、Touch 100 次，并覆盖单指、双指、最大可用多指、同批抬起、慢写、快写、停笔、短划、resize 和 clear。
- 人手路径漂移只记录基线；终态可靠性、视觉连续性与延迟门槛为硬验收。
- Windows 7、D3D11 Debug Layer 安装和持久化协议仍是独立长期事项；当前环境不能执行的项目明确标记“未验证”。

## Acceptance Criteria

- [x] 测试工程在全部解决方案配置中注册，并直接编译真实模块源码。
- [x] Down、终态和 ControlWake 能中断活动等待；Move 保持帧级合并；空闲路径无自旋。
- [x] 指标关闭路径无指标分配和文件写入，开启路径输出可解析 JSON。
- [x] 自动化并发、几何和鼠标基准均可从测试工程运行。
- [x] 连续三次 Release 自动鼠标基准满足严格门槛。
- [x] ARM64 Debug/Release、x64/x86 Release 全解决方案构建及相应测试通过。
- [ ] Pen/Touch 真机矩阵记录设备环境、样本数、分位数和场景结果。
- [x] 未安装的 D3D11 Debug Layer 与未执行的 Windows 7 验证不被伪报通过。

## Out of Scope

- 不引入第三方测试/指标库。
- 不测量从数字化仪到物理像素发光的端到端硬件延迟。
- 不修改 concurrentqueue 版本、vcpkg manifest 或未跟踪 `Vcpkg/`。
- 不在本任务定义正式笔迹持久化协议。
